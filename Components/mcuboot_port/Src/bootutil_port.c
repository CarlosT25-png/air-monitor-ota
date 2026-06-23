#include "bootutil/image.h"
#include "bootutil_priv.h"
#include "flash_map_backend/flash_map_backend.h"

int boot_read_image_header(struct boot_loader_state *state, int slot,
                           struct image_header *out_hdr, struct boot_status *bs)
{
    const struct flash_area *fap;

    (void)bs;

    fap = BOOT_IMG_AREA(state, slot);
    if (fap == NULL)
    {
        return BOOT_EFLASH;
    }

    if (flash_area_read(fap, 0, out_hdr, sizeof(*out_hdr)) != 0)
    {
        return BOOT_EFLASH;
    }

    return 0;
}

int boot_slots_compatible(struct boot_loader_state *state)
{
    (void)state;
    return 1;
}
