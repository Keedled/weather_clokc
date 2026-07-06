#ifndef __OTA_UPDATE_H
#define __OTA_UPDATE_H

#include <stdint.h>

typedef enum {
  OTA_STATUS_IDLE = 0,
  OTA_STATUS_RECEIVING,
  OTA_STATUS_READY,
  OTA_STATUS_ERROR
} OtaStatus;

int Ota_Begin(uint32_t image_size, uint32_t image_crc32, uint32_t image_version);
int Ota_WriteHexChunk(uint32_t offset, const char *hex);
int Ota_Finish(void);
void Ota_Abort(void);

OtaStatus Ota_GetStatus(void);
uint32_t Ota_GetExpectedSize(void);
uint32_t Ota_GetReceivedSize(void);
uint32_t Ota_GetExpectedCrc32(void);
uint32_t Ota_GetCurrentCrc32(void);
const char *Ota_GetStatusText(void);

uint32_t Ota_Crc32Calculate(const uint8_t *data, uint32_t len);
uint32_t Ota_Crc32UpdateRaw(uint32_t crc, const uint8_t *data, uint32_t len);

#endif /* __OTA_UPDATE_H */
