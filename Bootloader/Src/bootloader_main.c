#include <stddef.h>
#include <string.h>

#include "bootloader.h"
#include "bootloader_config.h"
#include "ota_layout.h"
#include "stm32f4xx_hal.h"

#define BOOT_CRC32_INIT       0xFFFFFFFFUL
#define BOOT_CRC32_XOROUT     0xFFFFFFFFUL

typedef void (*BootJumpFunc)(void);

static int Boot_IsValidVector(uint32_t app_addr);
static int Boot_IsValidMetadata(const OtaMetadata *metadata);
static int Boot_ShouldApplyUpdate(const OtaMetadata *metadata);
static int Boot_VerifyImage(uint32_t address, uint32_t size, uint32_t expected_crc32);
static int Boot_CopyStagingToApp(const OtaMetadata *metadata);
static int Boot_EraseAppForSize(uint32_t image_size);
static int Boot_EraseSectors(uint32_t first_sector, uint32_t sector_count);
static int Boot_WriteBytes(uint32_t address, const uint8_t *data, uint32_t len);
static int Boot_WriteMetadata(const OtaMetadata *metadata);
static void Boot_MarkCopyDone(const OtaMetadata *metadata);
static void Boot_JumpToApp(uint32_t app_addr);
static uint32_t Boot_Crc32Calculate(const uint8_t *data, uint32_t len);
static uint32_t Boot_Crc32UpdateRaw(uint32_t crc, const uint8_t *data, uint32_t len);
static uint32_t Boot_MetadataCrc32(const OtaMetadata *metadata);

void Bootloader_Run(void)
{
  const OtaMetadata *metadata = (const OtaMetadata *)OTA_METADATA_ADDR;

  if (Boot_ShouldApplyUpdate(metadata))
  {
    if (Boot_CopyStagingToApp(metadata))
    {
      Boot_MarkCopyDone(metadata);
    }
  }

  if (Boot_IsValidVector(OTA_APP_BASE_ADDR))
  {
    Boot_JumpToApp(OTA_APP_BASE_ADDR);
  }
}

static int Boot_IsValidVector(uint32_t app_addr)
{
  uint32_t initial_sp;
  uint32_t reset_handler;

  initial_sp = *(const uint32_t *)app_addr;
  reset_handler = *(const uint32_t *)(app_addr + 4U);

  if (initial_sp < BOOTLOADER_SRAM_BASE_ADDR ||
      initial_sp > BOOTLOADER_SRAM_END_ADDR)
  {
    return 0;
  }

  if ((reset_handler & 1U) == 0U)
  {
    return 0;
  }

  reset_handler &= ~1UL;
  return reset_handler >= OTA_APP_BASE_ADDR &&
         reset_handler < OTA_APP_BASE_ADDR + OTA_APP_SIZE;
}

static int Boot_IsValidMetadata(const OtaMetadata *metadata)
{
  if (metadata == NULL ||
      metadata->magic != OTA_METADATA_MAGIC ||
      metadata->metadata_version != OTA_METADATA_VERSION)
  {
    return 0;
  }

  if (metadata->image_size == 0U ||
      metadata->image_size > OTA_APP_SIZE ||
      metadata->staging_addr != OTA_STAGING_BASE_ADDR ||
      metadata->app_addr != OTA_APP_BASE_ADDR)
  {
    return 0;
  }

  return Boot_MetadataCrc32(metadata) == metadata->metadata_crc32;
}

static int Boot_ShouldApplyUpdate(const OtaMetadata *metadata)
{
  if (!Boot_IsValidMetadata(metadata))
  {
    return 0;
  }

  if ((metadata->flags & OTA_FLAG_PENDING) == 0U)
  {
    return 0;
  }

  if ((metadata->flags & OTA_FLAG_COPY_DONE) != 0U)
  {
    return 0;
  }

  return 1;
}

static int Boot_VerifyImage(uint32_t address, uint32_t size, uint32_t expected_crc32)
{
  if (size == 0U || size > OTA_APP_SIZE)
  {
    return 0;
  }

  if (!Boot_IsValidVector(address))
  {
    return 0;
  }

  return Boot_Crc32Calculate((const uint8_t *)address, size) == expected_crc32;
}

static int Boot_CopyStagingToApp(const OtaMetadata *metadata)
{
  if (!Boot_IsValidMetadata(metadata))
  {
    return 0;
  }

  if (!Boot_VerifyImage(metadata->staging_addr,
                        metadata->image_size,
                        metadata->image_crc32))
  {
    return 0;
  }

  if (!Boot_EraseAppForSize(metadata->image_size))
  {
    return 0;
  }

  if (!Boot_WriteBytes(OTA_APP_BASE_ADDR,
                       (const uint8_t *)OTA_STAGING_BASE_ADDR,
                       metadata->image_size))
  {
    return 0;
  }

  return Boot_VerifyImage(OTA_APP_BASE_ADDR,
                          metadata->image_size,
                          metadata->image_crc32);
}

static int Boot_EraseSectors(uint32_t first_sector, uint32_t sector_count)
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

static int Boot_EraseAppForSize(uint32_t image_size)
{
  uint32_t sector_count;

  if (image_size == 0U || image_size > OTA_APP_SIZE)
  {
    return 0;
  }

  sector_count = (image_size + OTA_SLOT_SECTOR_SIZE - 1U) / OTA_SLOT_SECTOR_SIZE;
  return Boot_EraseSectors(FLASH_SECTOR_5, sector_count);
}

static int Boot_WriteBytes(uint32_t address, const uint8_t *data, uint32_t len)
{
  uint32_t i;
  HAL_StatusTypeDef status;

  if (data == NULL)
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

static int Boot_WriteMetadata(const OtaMetadata *metadata)
{
  return Boot_WriteBytes(OTA_METADATA_ADDR,
                         (const uint8_t *)metadata,
                         (uint32_t)sizeof(*metadata));
}

static void Boot_MarkCopyDone(const OtaMetadata *metadata)
{
  OtaMetadata updated;

  if (!Boot_IsValidMetadata(metadata))
  {
    return;
  }

  updated = *metadata;
  updated.flags &= ~OTA_FLAG_PENDING;
  updated.flags |= OTA_FLAG_COPY_DONE | OTA_FLAG_CONFIRMED;
  updated.metadata_crc32 = Boot_MetadataCrc32(&updated);

  if (Boot_EraseSectors(FLASH_SECTOR_4, 1U))
  {
    Boot_WriteMetadata(&updated);
  }
}

static void Boot_JumpToApp(uint32_t app_addr)
{
  uint32_t initial_sp;
  uint32_t reset_handler;
  uint32_t i;
  BootJumpFunc jump_to_app;

  initial_sp = *(const uint32_t *)app_addr;
  reset_handler = *(const uint32_t *)(app_addr + 4U);

  __disable_irq();
  SysTick->CTRL = 0;
  SysTick->LOAD = 0;
  SysTick->VAL = 0;

  for (i = 0; i < (sizeof(NVIC->ICER) / sizeof(NVIC->ICER[0])); i++)
  {
    NVIC->ICER[i] = 0xFFFFFFFFUL;
    NVIC->ICPR[i] = 0xFFFFFFFFUL;
  }

  HAL_RCC_DeInit();
  HAL_DeInit();

  SCB->VTOR = app_addr;
  __set_MSP(initial_sp);
  __DSB();
  __ISB();

  jump_to_app = (BootJumpFunc)reset_handler;
  jump_to_app();
}

static uint32_t Boot_Crc32Calculate(const uint8_t *data, uint32_t len)
{
  return Boot_Crc32UpdateRaw(BOOT_CRC32_INIT, data, len) ^ BOOT_CRC32_XOROUT;
}

static uint32_t Boot_Crc32UpdateRaw(uint32_t crc, const uint8_t *data, uint32_t len)
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

static uint32_t Boot_MetadataCrc32(const OtaMetadata *metadata)
{
  return Boot_Crc32Calculate((const uint8_t *)metadata,
                             (uint32_t)offsetof(OtaMetadata, metadata_crc32));
}
