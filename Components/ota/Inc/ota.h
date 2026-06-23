#pragma once

#include <stdbool.h>
#include <stdint.h>

bool ota_is_active(void);
int ota_init(void);
void ota_process(void);
