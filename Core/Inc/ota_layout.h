#ifndef __OTA_LAYOUT_H
#define __OTA_LAYOUT_H

#include <stdint.h>

/*
 * STM32F407ZG 1 MB Flash layout used by the planned bootloader OTA flow.
 *
 * Sectors 0-3:  0x08000000 - 0x0800FFFF  Bootloader, 64 KB
 * Sector 4:     0x08010000 - 0x0801FFFF  OTA metadata, 64 KB
 * Sectors 5-7:  0x08020000 - 0x0807FFFF  Runtime App, 384 KB
 * Sectors 8-10: 0x08080000 - 0x080DFFFF  OTA staging image, 384 KB
 * Sector 11:    0x080E0000 - 0x080FFFFF  Reserved, 128 KB
 */
#define OTA_FLASH_BASE_ADDR        0x08000000UL
#define OTA_FLASH_SIZE             0x00100000UL

#define OTA_BOOT_BASE_ADDR         0x08000000UL
#define OTA_BOOT_SIZE              0x00010000UL

#define OTA_METADATA_ADDR          0x08010000UL
#define OTA_METADATA_SIZE          0x00010000UL

#define OTA_APP_BASE_ADDR          0x08020000UL
#define OTA_APP_SIZE               0x00060000UL

#define OTA_STAGING_BASE_ADDR      0x08080000UL
#define OTA_STAGING_SIZE           0x00060000UL

#define OTA_SLOT_SECTOR_SIZE       0x00020000UL

#define OTA_RESERVED_BASE_ADDR     0x080E0000UL
#define OTA_RESERVED_SIZE          0x00020000UL

#define OTA_METADATA_MAGIC         0x3141544FUL /* "OTA1" little-endian */
#define OTA_METADATA_VERSION       1UL

#define OTA_FLAG_PENDING           0x00000001UL
#define OTA_FLAG_CONFIRMED         0x00000002UL
#define OTA_FLAG_COPY_DONE         0x00000004UL

typedef struct {
  uint32_t magic;
  uint32_t metadata_version;
  uint32_t image_size;
  uint32_t image_crc32;
  uint32_t image_version;
  uint32_t flags;
  uint32_t sequence;
  uint32_t staging_addr;
  uint32_t app_addr;
  uint32_t reserved[6];
  uint32_t metadata_crc32;
} OtaMetadata;

#endif /* __OTA_LAYOUT_H */
