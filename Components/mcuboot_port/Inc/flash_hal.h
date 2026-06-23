#pragma once

#include <stdint.h>
#include <stddef.h>

#define FLASH_ERASED_BYTE  0xFFU

int flash_hal_init(void);
int flash_hal_erase(uint32_t address, uint32_t size);
int flash_hal_write(uint32_t address, const uint8_t *data, uint32_t len);
int flash_hal_read(uint32_t address, uint8_t *data, uint32_t len);
uint32_t flash_hal_page_size(void);
uint32_t flash_hal_write_align(void);
