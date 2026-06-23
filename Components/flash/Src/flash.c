#include "flash.h"
#include "flash_hal.h"
#include "flash_layout.h"

#include <string.h>

static flash_progress_cb_t s_progress_cb;
static uint32_t s_progress_total;
static uint32_t s_progress_written;
static uint8_t s_write_tail[MCUBOOT_FLASH_WRITE_ALIGN];
static uint32_t s_write_tail_len;

static void flash_report_progress(void)
{
    if (s_progress_cb != NULL)
    {
        s_progress_cb(s_progress_written, s_progress_total);
    }
}

int flash_init(void)
{
    s_write_tail_len = 0U;
    s_progress_written = 0U;
    return flash_hal_init();
}

void flash_set_progress_callback(flash_progress_cb_t cb, uint32_t total)
{
    s_progress_cb = cb;
    s_progress_total = total;
}

int flash_erase_slot1(void)
{
    s_write_tail_len = 0U;
    s_progress_written = 0U;
    return flash_hal_erase(SLOT1_FLASH_ADDRESS, SLOT1_FLASH_SIZE);
}

int flash_read_slot1(uint32_t offset, uint8_t *data, uint32_t len)
{
    if ((offset + len) > SLOT1_FLASH_SIZE || data == NULL)
    {
        return -1;
    }

    return flash_hal_read(SLOT1_FLASH_ADDRESS + offset, data, len);
}

static int flash_flush_tail(void)
{
    if (s_write_tail_len == 0U)
    {
        return 0;
    }

    while (s_write_tail_len < MCUBOOT_FLASH_WRITE_ALIGN)
    {
        s_write_tail[s_write_tail_len++] = 0xFFU;
    }

    if (flash_hal_write(SLOT1_FLASH_ADDRESS + s_progress_written, s_write_tail, MCUBOOT_FLASH_WRITE_ALIGN) != 0)
    {
        return -1;
    }

    s_progress_written += MCUBOOT_FLASH_WRITE_ALIGN;
    s_write_tail_len = 0U;
    flash_report_progress();
    return 0;
}

int flash_write_slot1(uint32_t offset, const uint8_t *data, uint32_t len)
{
    uint32_t pos = 0U;

    if (data == NULL || (offset + len) > SLOT1_FLASH_SIZE)
    {
        return -1;
    }

    if (offset != s_progress_written)
    {
        return -1;
    }

    if (s_write_tail_len > 0U)
    {
        while (s_write_tail_len < MCUBOOT_FLASH_WRITE_ALIGN && pos < len)
        {
            s_write_tail[s_write_tail_len++] = data[pos++];
        }

        if (s_write_tail_len == MCUBOOT_FLASH_WRITE_ALIGN)
        {
            if (flash_hal_write(SLOT1_FLASH_ADDRESS + s_progress_written, s_write_tail, MCUBOOT_FLASH_WRITE_ALIGN) != 0)
            {
                return -1;
            }
            s_progress_written += MCUBOOT_FLASH_WRITE_ALIGN;
            s_write_tail_len = 0U;
            flash_report_progress();
        }
    }

    while ((pos + MCUBOOT_FLASH_WRITE_ALIGN) <= len)
    {
        if (flash_hal_write(SLOT1_FLASH_ADDRESS + s_progress_written, data + pos, MCUBOOT_FLASH_WRITE_ALIGN) != 0)
        {
            return -1;
        }
        pos += MCUBOOT_FLASH_WRITE_ALIGN;
        s_progress_written += MCUBOOT_FLASH_WRITE_ALIGN;
        flash_report_progress();
    }

    if (pos < len)
    {
        s_write_tail_len = len - pos;
        memcpy(s_write_tail, data + pos, s_write_tail_len);
    }

    return 0;
}

int flash_finalize_slot1(void)
{
    return flash_flush_tail();
}
