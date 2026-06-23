#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "bootutil/image.h"

bool mcuboot_app_is_pending(void);
int mcuboot_app_confirm(void);
int mcuboot_app_set_pending(void);
int mcuboot_app_get_running_version(struct image_version *ver);
int mcuboot_app_format_version(const struct image_version *ver, char *buf, size_t len);
