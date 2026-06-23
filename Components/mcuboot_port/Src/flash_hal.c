#include "flash_hal.h"
#include "flash_layout.h"

#include "stm32g4xx_hal.h"
#include <string.h>

static int flash_hal_unlock(void)
{
    if (HAL_FLASH_Unlock() != HAL_OK)
    {
        return -1;
    }
    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_ALL_ERRORS);
    return 0;
}

static void flash_hal_lock(void)
{
    (void)HAL_FLASH_Lock();
}

int flash_hal_init(void)
{
    return 0;
}

uint32_t flash_hal_page_size(void)
{
    return MCUBOOT_FLASH_PAGE_SIZE;
}

uint32_t flash_hal_write_align(void)
{
    return MCUBOOT_FLASH_WRITE_ALIGN;
}

int flash_hal_read(uint32_t address, uint8_t *data, uint32_t len)
{
    if (data == NULL || len == 0U)
    {
        return -1;
    }

    memcpy(data, (const void *)(uintptr_t)address, len);
    return 0;
}

int flash_hal_erase(uint32_t address, uint32_t size)
{
    FLASH_EraseInitTypeDef erase = {0};
    uint32_t page_error = 0U;
    uint32_t end_address = address + size - 1U;
    HAL_StatusTypeDef status = HAL_OK;

    if (size == 0U)
    {
        return 0;
    }

    if (address < FLASH_DEVICE_BASE || end_address < address)
    {
        return -1;
    }

    if (flash_hal_unlock() != 0)
    {
        return -1;
    }

    while (address <= end_address && status == HAL_OK)
    {
        erase.TypeErase = FLASH_TYPEERASE_PAGES;
        if (address >= (FLASH_DEVICE_BASE + 0x40000U))
        {
            erase.Banks = FLASH_BANK_2;
            erase.Page = (address - (FLASH_DEVICE_BASE + 0x40000U)) / MCUBOOT_FLASH_PAGE_SIZE;
        }
        else
        {
            erase.Banks = FLASH_BANK_1;
            erase.Page = (address - FLASH_DEVICE_BASE) / MCUBOOT_FLASH_PAGE_SIZE;
        }
        erase.NbPages = 1U;

        status = HAL_FLASHEx_Erase(&erase, &page_error);
        address += MCUBOOT_FLASH_PAGE_SIZE;
    }

    flash_hal_lock();
    return (status == HAL_OK) ? 0 : -1;
}

int flash_hal_write(uint32_t address, const uint8_t *data, uint32_t len)
{
    HAL_StatusTypeDef status = HAL_OK;
    uint32_t offset = 0U;

    if (data == NULL || len == 0U)
    {
        return -1;
    }

    if ((address % MCUBOOT_FLASH_WRITE_ALIGN) != 0U)
    {
        return -1;
    }

    if (flash_hal_unlock() != 0)
    {
        return -1;
    }

    while (offset < len)
    {
        uint64_t dword = 0xFFFFFFFFFFFFFFFFULL;
        uint32_t chunk = len - offset;

        if (chunk > MCUBOOT_FLASH_WRITE_ALIGN)
        {
            chunk = MCUBOOT_FLASH_WRITE_ALIGN;
        }

        memcpy(&dword, data + offset, chunk);
        status = HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD, address + offset, dword);
        if (status != HAL_OK)
        {
            break;
        }

        offset += MCUBOOT_FLASH_WRITE_ALIGN;
    }

    flash_hal_lock();
    return (status == HAL_OK) ? 0 : -1;
}
