#include "ota_update.h"

#include <stddef.h>
#include <string.h>

#include "main.h"
#include "ota_layout.h"
#include "stm32f4xx_hal.h"

#define OTA_CRC32_INIT        0xFFFFFFFFUL
#define OTA_CRC32_XOROUT      0xFFFFFFFFUL

typedef struct {
  OtaStatus status;
  uint32_t expected_size;
  uint32_t received_size;
  uint32_t expected_crc32;
  uint32_t image_version;
  uint32_t crc_state;
  uint32_t sequence;
} OtaUpdateContext;

static OtaUpdateContext ota_ctx = {
  OTA_STATUS_IDLE,
  0,
  0,
  0,
  0,
  OTA_CRC32_INIT,
  0
};

static int Ota_EraseSectors(uint32_t first_sector, uint32_t sector_count);
static int Ota_EraseStagingForSize(uint32_t image_size);
static int Ota_EraseMetadata(void);
static int Ota_WriteBytes(uint32_t address, const uint8_t *data, uint32_t len);
static int Ota_WriteMetadata(const OtaMetadata *metadata);
static int Ota_IsImageSizeValid(uint32_t image_size);
static int Ota_HexToNibble(char ch);
static uint32_t Ota_MetadataCrc32(const OtaMetadata *metadata);

int Ota_Begin(uint32_t image_size, uint32_t image_crc32, uint32_t image_version)
{
  if (!Ota_IsImageSizeValid(image_size))
  {
    ota_ctx.status = OTA_STATUS_ERROR;
    return 0;
  }

  if (!Ota_EraseStagingForSize(image_size))
  {
    ota_ctx.status = OTA_STATUS_ERROR;
    return 0;
  }

  ota_ctx.status = OTA_STATUS_RECEIVING;
  ota_ctx.expected_size = image_size;
  ota_ctx.received_size = 0;
  ota_ctx.expected_crc32 = image_crc32;
  ota_ctx.image_version = image_version;
  ota_ctx.crc_state = OTA_CRC32_INIT;

  return 1;
}

int Ota_WriteHexChunk(uint32_t offset, const char *hex)
{
  uint8_t chunk[128];
  uint32_t chunk_len = 0;
  uint32_t i;
  int high;
  int low;

  if (ota_ctx.status != OTA_STATUS_RECEIVING || hex == NULL)
  {
    ota_ctx.status = OTA_STATUS_ERROR;
    return 0;
  }

  if (offset != ota_ctx.received_size)
  {
    ota_ctx.status = OTA_STATUS_ERROR;
    return 0;
  }

  for (i = 0; hex[i] != '\0'; )
  {
    high = Ota_HexToNibble(hex[i++]);
    if (high < 0 || hex[i] == '\0')
    {
      ota_ctx.status = OTA_STATUS_ERROR;
      return 0;
    }

    low = Ota_HexToNibble(hex[i++]);
    if (low < 0)
    {
      ota_ctx.status = OTA_STATUS_ERROR;
      return 0;
    }

    if (chunk_len >= sizeof(chunk))
    {
      ota_ctx.status = OTA_STATUS_ERROR;
      return 0;
    }

    chunk[chunk_len++] = (uint8_t)((high << 4) | low);
  }

  if (chunk_len == 0 ||
      ota_ctx.received_size + chunk_len > ota_ctx.expected_size)
  {
    ota_ctx.status = OTA_STATUS_ERROR;
    return 0;
  }

  if (!Ota_WriteBytes(OTA_STAGING_BASE_ADDR + ota_ctx.received_size,
                      chunk,
                      chunk_len))
  {
    ota_ctx.status = OTA_STATUS_ERROR;
    return 0;
  }

  ota_ctx.crc_state = Ota_Crc32UpdateRaw(ota_ctx.crc_state, chunk, chunk_len);
  ota_ctx.received_size += chunk_len;

  return 1;
}

int Ota_Finish(void)
{
  OtaMetadata metadata;
  uint32_t actual_crc;

  if (ota_ctx.status != OTA_STATUS_RECEIVING ||
      ota_ctx.received_size != ota_ctx.expected_size)
  {
    ota_ctx.status = OTA_STATUS_ERROR;
    return 0;
  }

  actual_crc = Ota_GetCurrentCrc32();
  if (actual_crc != ota_ctx.expected_crc32)
  {
    ota_ctx.status = OTA_STATUS_ERROR;
    return 0;
  }

  memset(&metadata, 0xFF, sizeof(metadata));
  metadata.magic = OTA_METADATA_MAGIC;
  metadata.metadata_version = OTA_METADATA_VERSION;
  metadata.image_size = ota_ctx.expected_size;
  metadata.image_crc32 = ota_ctx.expected_crc32;
  metadata.image_version = ota_ctx.image_version;
  metadata.flags = OTA_FLAG_PENDING;
  metadata.sequence = ++ota_ctx.sequence;
  metadata.staging_addr = OTA_STAGING_BASE_ADDR;
  metadata.app_addr = OTA_APP_BASE_ADDR;
  metadata.metadata_crc32 = Ota_MetadataCrc32(&metadata);

  if (!Ota_EraseMetadata() || !Ota_WriteMetadata(&metadata))
  {
    ota_ctx.status = OTA_STATUS_ERROR;
    return 0;
  }

  ota_ctx.status = OTA_STATUS_READY;
  return 1;
}

void Ota_Abort(void)
{
  ota_ctx.status = OTA_STATUS_IDLE;
  ota_ctx.expected_size = 0;
  ota_ctx.received_size = 0;
  ota_ctx.expected_crc32 = 0;
  ota_ctx.image_version = 0;
  ota_ctx.crc_state = OTA_CRC32_INIT;
}

OtaStatus Ota_GetStatus(void)
{
  return ota_ctx.status;
}

uint32_t Ota_GetExpectedSize(void)
{
  return ota_ctx.expected_size;
}

uint32_t Ota_GetReceivedSize(void)
{
  return ota_ctx.received_size;
}

uint32_t Ota_GetExpectedCrc32(void)
{
  return ota_ctx.expected_crc32;
}

uint32_t Ota_GetCurrentCrc32(void)
{
  return ota_ctx.crc_state ^ OTA_CRC32_XOROUT;
}

const char *Ota_GetStatusText(void)
{
  switch (ota_ctx.status)
  {
    case OTA_STATUS_IDLE:
      return "idle";
    case OTA_STATUS_RECEIVING:
      return "receiving";
    case OTA_STATUS_READY:
      return "ready";
    case OTA_STATUS_ERROR:
    default:
      return "error";
  }
}

uint32_t Ota_Crc32Calculate(const uint8_t *data, uint32_t len)
{
  return Ota_Crc32UpdateRaw(OTA_CRC32_INIT, data, len) ^ OTA_CRC32_XOROUT;
}

uint32_t Ota_Crc32UpdateRaw(uint32_t crc, const uint8_t *data, uint32_t len)
{
  uint32_t i;
  uint32_t bit;

  if (data == NULL)
  {
    return crc;
  }

  for (i = 0; i < len; i++)
  {
    crc ^= data[i];
    for (bit = 0; bit < 8U; bit++)
    {
      if ((crc & 1U) != 0U)
      {
        crc = (crc >> 1) ^ 0xEDB88320UL;
      }
      else
      {
        crc >>= 1;
      }
    }
  }

  return crc;
}

static int Ota_EraseSectors(uint32_t first_sector, uint32_t sector_count)
{
  FLASH_EraseInitTypeDef erase;
  uint32_t sector_error = 0;
  HAL_StatusTypeDef status;

  memset(&erase, 0, sizeof(erase));
  erase.TypeErase = FLASH_TYPEERASE_SECTORS;
  erase.Sector = first_sector;
  erase.NbSectors = sector_count;
  erase.VoltageRange = FLASH_VOLTAGE_RANGE_3;

  HAL_FLASH_Unlock();
  status = HAL_FLASHEx_Erase(&erase, &sector_error);
  HAL_FLASH_Lock();

  return status == HAL_OK && sector_error == 0xFFFFFFFFUL;
}

static int Ota_EraseStagingForSize(uint32_t image_size)
{
  uint32_t sector_count;

  if (!Ota_IsImageSizeValid(image_size))
  {
    return 0;
  }

  sector_count = (image_size + OTA_SLOT_SECTOR_SIZE - 1U) / OTA_SLOT_SECTOR_SIZE;
  return Ota_EraseSectors(FLASH_SECTOR_8, sector_count);
}

static int Ota_EraseMetadata(void)
{
  return Ota_EraseSectors(FLASH_SECTOR_4, 1U);
}

static int Ota_WriteBytes(uint32_t address, const uint8_t *data, uint32_t len)
{
  uint32_t i;
  HAL_StatusTypeDef status;

  if (data == NULL ||
      address < OTA_STAGING_BASE_ADDR ||
      address + len > OTA_STAGING_BASE_ADDR + OTA_STAGING_SIZE)
  {
    return 0;
  }

  HAL_FLASH_Unlock();
  for (i = 0; i < len; i++)
  {
    status = HAL_FLASH_Program(FLASH_TYPEPROGRAM_BYTE,
                               address + i,
                               data[i]);
    if (status != HAL_OK)
    {
      HAL_FLASH_Lock();
      return 0;
    }
  }
  HAL_FLASH_Lock();

  return 1;
}

static int Ota_WriteMetadata(const OtaMetadata *metadata)
{
  const uint8_t *data = (const uint8_t *)metadata;
  uint32_t i;
  HAL_StatusTypeDef status;

  if (metadata == NULL)
  {
    return 0;
  }

  HAL_FLASH_Unlock();
  for (i = 0; i < sizeof(*metadata); i++)
  {
    status = HAL_FLASH_Program(FLASH_TYPEPROGRAM_BYTE,
                               OTA_METADATA_ADDR + i,
                               data[i]);
    if (status != HAL_OK)
    {
      HAL_FLASH_Lock();
      return 0;
    }
  }
  HAL_FLASH_Lock();

  return 1;
}

static int Ota_IsImageSizeValid(uint32_t image_size)
{
  return image_size > 0U && image_size <= OTA_APP_SIZE;
}

static int Ota_HexToNibble(char ch)
{
  if (ch >= '0' && ch <= '9')
  {
    return ch - '0';
  }
  if (ch >= 'a' && ch <= 'f')
  {
    return ch - 'a' + 10;
  }
  if (ch >= 'A' && ch <= 'F')
  {
    return ch - 'A' + 10;
  }
  return -1;
}

static uint32_t Ota_MetadataCrc32(const OtaMetadata *metadata)
{
  return Ota_Crc32Calculate((const uint8_t *)metadata,
                            (uint32_t)offsetof(OtaMetadata, metadata_crc32));
}
