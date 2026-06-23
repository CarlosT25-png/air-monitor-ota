#pragma once

#include <stdint.h>
#include <stddef.h>

typedef void (*flash_progress_cb_t)(uint32_t written, uint32_t total);

int flash_init(void);
int flash_erase_slot1(void);
int flash_write_slot1(uint32_t offset, const uint8_t *data, uint32_t len);
int flash_read_slot1(uint32_t offset, uint8_t *data, uint32_t len);
int flash_finalize_slot1(void);
void flash_set_progress_callback(flash_progress_cb_t cb, uint32_t total);
