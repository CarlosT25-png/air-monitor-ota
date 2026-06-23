#include "flash_map_backend/flash_map_backend.h"
#include "flash_layout.h"
#include "flash_hal.h"

#include <string.h>

static struct flash_area s_slot0 = {
    .fa_id = FLASH_AREA_IMAGE_0,
    .fa_device_id = 0,
    .fa_off = SLOT0_FLASH_ADDRESS,
    .fa_size = SLOT0_FLASH_SIZE,
};

static struct flash_area s_slot1 = {
    .fa_id = FLASH_AREA_IMAGE_1,
    .fa_device_id = 0,
    .fa_off = SLOT1_FLASH_ADDRESS,
    .fa_size = SLOT1_FLASH_SIZE,
};

static const struct flash_area *lookup_area(uint8_t id)
{
    switch (id)
    {
    case FLASH_AREA_IMAGE_0:
        return &s_slot0;
    case FLASH_AREA_IMAGE_1:
        return &s_slot1;
    default:
        return NULL;
    }
}

int flash_area_open(uint8_t id, const struct flash_area **fa)
{
    const struct flash_area *area = lookup_area(id);

    if (area == NULL || fa == NULL)
    {
        return -1;
    }

    *fa = area;
    return 0;
}

void flash_area_close(const struct flash_area *fa)
{
    (void)fa;
}

int flash_area_read(const struct flash_area *fa, uint32_t off, void *dst, uint32_t len)
{
    if (fa == NULL || dst == NULL || (off + len) > fa->fa_size)
    {
        return -1;
    }

    return flash_hal_read(fa->fa_off + off, dst, len);
}

int flash_area_write(const struct flash_area *fa, uint32_t off, const void *src, uint32_t len)
{
    if (fa == NULL || src == NULL || (off + len) > fa->fa_size)
    {
        return -1;
    }

    return flash_hal_write(fa->fa_off + off, (const uint8_t *)src, len);
}

int flash_area_erase(const struct flash_area *fa, uint32_t off, uint32_t len)
{
    if (fa == NULL || (off + len) > fa->fa_size)
    {
        return -1;
    }

    return flash_hal_erase(fa->fa_off + off, len);
}

uint32_t flash_area_align(const struct flash_area *fa)
{
    (void)fa;
    return flash_hal_write_align();
}

uint8_t flash_area_erased_val(const struct flash_area *fa)
{
    (void)fa;
    return FLASH_ERASED_BYTE;
}

int flash_area_get_sectors(int fa_id, uint32_t *count, struct flash_sector *sectors)
{
    const struct flash_area *fa = lookup_area((uint8_t)fa_id);
    uint32_t total = 0U;
    uint32_t off;

    if (fa == NULL || count == NULL || sectors == NULL)
    {
        return -1;
    }

    for (off = 0U; off < fa->fa_size; off += MCUBOOT_FLASH_PAGE_SIZE)
    {
        sectors[total].fs_off = off;
        sectors[total].fs_size = MCUBOOT_FLASH_PAGE_SIZE;
        total++;
    }

    *count = total;
    return 0;
}

int flash_area_get_sector(const struct flash_area *fap, uint32_t off, struct flash_sector *fs)
{
    if (fap == NULL || fs == NULL || off >= fap->fa_size)
    {
        return -1;
    }

    fs->fs_off = (off / MCUBOOT_FLASH_PAGE_SIZE) * MCUBOOT_FLASH_PAGE_SIZE;
    fs->fs_size = MCUBOOT_FLASH_PAGE_SIZE;
    return 0;
}

int flash_area_id_from_multi_image_slot(int image_index, int slot)
{
    (void)image_index;

    if (slot == 0)
    {
        return FLASH_AREA_IMAGE_0;
    }
    if (slot == 1)
    {
        return FLASH_AREA_IMAGE_1;
    }
    return -1;
}

int flash_area_id_from_image_slot(int slot)
{
    return flash_area_id_from_multi_image_slot(0, slot);
}

int flash_area_id_to_multi_image_slot(int image_index, int area_id)
{
    (void)image_index;

    if (area_id == FLASH_AREA_IMAGE_0)
    {
        return 0;
    }
    if (area_id == FLASH_AREA_IMAGE_1)
    {
        return 1;
    }
    return -1;
}
