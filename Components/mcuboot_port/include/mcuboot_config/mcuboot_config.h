/*
 * MCUboot configuration for STM32G474 dual-slot (overwrite-only / upgrade-only).
 */
#ifndef __MCUBOOT_CONFIG_H__
#define __MCUBOOT_CONFIG_H__

#include "flash_layout.h"

#define MCUBOOT_OVERWRITE_ONLY
#define MCUBOOT_OVERWRITE_ONLY_FAST

#define MCUBOOT_USE_TINYCRYPT
#define MCUBOOT_SHA256

#undef MCUBOOT_VALIDATE_PRIMARY_SLOT

#define MCUBOOT_IMAGE_NUMBER        1
#define MCUBOOT_MAX_IMG_SECTORS     128
#define MCUBOOT_BOOT_MAX_ALIGN      8

#define MCUBOOT_USE_FLASH_AREA_GET_SECTORS
#define MCUBOOT_DEV_WITH_ERASE
#define MCUBOOT_USE_TLV_ALLOW_LIST  1

#ifndef MCUBOOT_HAVE_LOGGING
#define MCUBOOT_HAVE_LOGGING        1
#endif

#define MCUBOOT_WATCHDOG_FEED()     do { } while (0)
#define MCUBOOT_CPU_IDLE()          do { } while (0)

#endif /* __MCUBOOT_CONFIG_H__ */
