/*
 * MCUboot first-stage bootloader for STM32G474RE.
 */
#include "stm32g4xx_hal.h"
#include "bootutil/bootutil.h"
#include "bootutil/image.h"
#include "bootutil/bootutil_log.h"
#include "bootutil/fault_injection_hardening.h"
#include "flash_hal.h"

#include <stdio.h>

static UART_HandleTypeDef s_huart3;

static void bl_clock_init(void)
{
    RCC_OscInitTypeDef osc = {0};
    RCC_ClkInitTypeDef clk = {0};

    HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1_BOOST);

    osc.OscillatorType = RCC_OSCILLATORTYPE_HSI;
    osc.HSIState = RCC_HSI_ON;
    osc.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
    osc.PLL.PLLState = RCC_PLL_ON;
    osc.PLL.PLLSource = RCC_PLLSOURCE_HSI;
    osc.PLL.PLLM = RCC_PLLM_DIV4;
    osc.PLL.PLLN = 85;
    osc.PLL.PLLP = RCC_PLLP_DIV2;
    osc.PLL.PLLQ = RCC_PLLQ_DIV2;
    osc.PLL.PLLR = RCC_PLLR_DIV2;
    (void)HAL_RCC_OscConfig(&osc);

    clk.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK |
                    RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    clk.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    clk.AHBCLKDivider = RCC_SYSCLK_DIV1;
    clk.APB1CLKDivider = RCC_HCLK_DIV1;
    clk.APB2CLKDivider = RCC_HCLK_DIV1;
    (void)HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_4);
}

static void bl_uart_init(void)
{
    GPIO_InitTypeDef gpio = {0};
    RCC_PeriphCLKInitTypeDef periph = {0};

    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_USART3_CLK_ENABLE();

    periph.PeriphClockSelection = RCC_PERIPHCLK_USART3;
    periph.Usart3ClockSelection = RCC_USART3CLKSOURCE_PCLK1;
    (void)HAL_RCCEx_PeriphCLKConfig(&periph);

    gpio.Pin = GPIO_PIN_10 | GPIO_PIN_11;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    gpio.Alternate = GPIO_AF7_USART3;
    HAL_GPIO_Init(GPIOB, &gpio);

    s_huart3.Instance = USART3;
    s_huart3.Init.BaudRate = 115200;
    s_huart3.Init.WordLength = UART_WORDLENGTH_8B;
    s_huart3.Init.StopBits = UART_STOPBITS_1;
    s_huart3.Init.Parity = UART_PARITY_NONE;
    s_huart3.Init.Mode = UART_MODE_TX_RX;
    s_huart3.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    s_huart3.Init.OverSampling = UART_OVERSAMPLING_16;
    (void)HAL_UART_Init(&s_huart3);
}

int __io_putchar(int ch)
{
    uint8_t c = (uint8_t)ch;
    (void)HAL_UART_Transmit(&s_huart3, &c, 1U, HAL_MAX_DELAY);
    return ch;
}

static void do_boot(const struct boot_rsp *rsp)
{
    uint32_t vtable = rsp->br_image_off + rsp->br_hdr->ih_hdr_size;
    uint32_t msp = *(uint32_t *)(uintptr_t)vtable;
    uint32_t reset = *(uint32_t *)(uintptr_t)(vtable + 4U);
    void (*reset_handler)(void) = (void (*)(void))(reset);

    printf("Booting app at 0x%08lx\r\n", (unsigned long)vtable);
    fflush(stdout);

    __disable_irq();

    SysTick->CTRL = 0U;
    SysTick->LOAD = 0U;
    SysTick->VAL = 0U;

    for (uint32_t i = 0U; i < 8U; i++)
    {
        NVIC->ICER[i] = 0xFFFFFFFFU;
        NVIC->ICPR[i] = 0xFFFFFFFFU;
    }

    (void)HAL_RCC_DeInit();
    (void)HAL_DeInit();

    __set_BASEPRI(0U);
    __set_FAULTMASK(0U);
    __enable_irq();

    __set_MSP(msp);
    SCB->VTOR = vtable;
    __DSB();
    __ISB();

    reset_handler();

    while (1)
    {
    }
}

int main(void)
{
    struct boot_rsp rsp;
    FIH_DECLARE(fih_rc, FIH_FAILURE);

    HAL_Init();
    bl_clock_init();
    bl_uart_init();

    BOOT_LOG_INF("MCUboot STM32G474 starting");

    (void)flash_hal_init();

    FIH_CALL(boot_go, fih_rc, &rsp);
    if (FIH_EQ(fih_rc, FIH_SUCCESS))
    {
        do_boot(&rsp);
    }

    BOOT_LOG_ERR("No bootable image found");
    while (1)
    {
    }
}

void SysTick_Handler(void)
{
    HAL_IncTick();
}

void Error_Handler(void)
{
    __disable_irq();
    while (1)
    {
    }
}
