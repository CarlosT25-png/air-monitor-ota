#include "mcuboot_app.h"
#include "bootutil/bootutil_public.h"
#include "flash_map_backend/flash_map_backend.h"
#include "sysflash/sysflash.h"

#include <stdio.h>

bool mcuboot_app_is_pending(void)
{
    int swap_type = boot_swap_type();

    return (swap_type == BOOT_SWAP_TYPE_TEST || swap_type == BOOT_SWAP_TYPE_PERM ||
            swap_type == BOOT_SWAP_TYPE_REVERT);
}

int mcuboot_app_confirm(void)
{
    int rc = boot_set_confirmed();

    if (rc == 0)
    {
        printf("MCUboot image confirmed\r\n");
    }
    else
    {
        printf("MCUboot confirm failed: %d\r\n", rc);
    }

    return rc;
}

int mcuboot_app_set_pending(void)
{
    int rc = boot_set_pending(0);

    if (rc != 0)
    {
        printf("boot_set_pending failed: %d\r\n", rc);
    }

    return rc;
}

int mcuboot_app_get_running_version(struct image_version *ver)
{
    const struct flash_area *fa = NULL;
    struct image_header hdr;
    int rc;

    if (ver == NULL)
    {
        return -1;
    }

    rc = flash_area_open(FLASH_AREA_IMAGE_0, &fa);
    if (rc != 0)
    {
        return rc;
    }

    rc = boot_image_load_header(fa, &hdr);
    flash_area_close(fa);
    if (rc != 0)
    {
        return rc;
    }

    *ver = hdr.ih_ver;
    return 0;
}

int mcuboot_app_format_version(const struct image_version *ver, char *buf, size_t len)
{
    if (ver == NULL || buf == NULL || len == 0U)
    {
        return -1;
    }

    return snprintf(buf, len, "%u.%u.%u", ver->iv_major, ver->iv_minor, ver->iv_revision);
}
