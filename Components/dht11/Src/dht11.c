#include "dht11.h"

#include "main.h"
#include "stm32g4xx_hal.h"

#define DHT11_GPIO_PORT          GPIOC
#define DHT11_GPIO_PIN           GPIO_PIN_12
#define DHT11_MIN_INTERVAL_MS    2000U

static uint32_t dht11_last_read_ms;

static void dht11_delay_us(uint32_t us)
{
  uint32_t start = DWT->CYCCNT;
  uint32_t ticks = (SystemCoreClock / 1000000U) * us;

  while ((DWT->CYCCNT - start) < ticks)
  {
  }
}

static void dht11_delay_init(void)
{
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CYCCNT = 0U;
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

static void dht11_set_output_low(void)
{
  GPIO_InitTypeDef gpio = {0};

  gpio.Pin = DHT11_GPIO_PIN;
  gpio.Mode = GPIO_MODE_OUTPUT_OD;
  gpio.Pull = GPIO_PULLUP;
  gpio.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(DHT11_GPIO_PORT, &gpio);
  HAL_GPIO_WritePin(DHT11_GPIO_PORT, DHT11_GPIO_PIN, GPIO_PIN_RESET);
}

static void dht11_set_output_high(void)
{
  HAL_GPIO_WritePin(DHT11_GPIO_PORT, DHT11_GPIO_PIN, GPIO_PIN_SET);
}

static void dht11_set_input(void)
{
  GPIO_InitTypeDef gpio = {0};

  gpio.Pin = DHT11_GPIO_PIN;
  gpio.Mode = GPIO_MODE_INPUT;
  gpio.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(DHT11_GPIO_PORT, &gpio);
}

static GPIO_PinState dht11_read_pin(void)
{
  return HAL_GPIO_ReadPin(DHT11_GPIO_PORT, DHT11_GPIO_PIN);
}

static int dht11_wait_level(GPIO_PinState level, uint32_t timeout_us)
{
  uint32_t start = DWT->CYCCNT;
  uint32_t timeout_ticks = (SystemCoreClock / 1000000U) * timeout_us;

  while ((DWT->CYCCNT - start) < timeout_ticks)
  {
    if (dht11_read_pin() == level)
    {
      return 0;
    }
  }

  return -1;
}

void dht11_init(void)
{
  __HAL_RCC_GPIOC_CLK_ENABLE();
  dht11_delay_init();
  dht11_set_input();
  dht11_last_read_ms = 0U;
  HAL_Delay(1000);
}

dht11_status_t dht11_read(dht11_data_t *data)
{
  uint8_t raw[5] = {0};
  uint32_t now = HAL_GetTick();

  if (data == NULL)
  {
    return DHT11_ERROR;
  }

  if ((now - dht11_last_read_ms) < DHT11_MIN_INTERVAL_MS)
  {
    return DHT11_ERROR;
  }

  dht11_set_output_low();
  HAL_Delay(20);
  dht11_set_output_high();
  dht11_delay_us(30);
  dht11_set_input();

  __disable_irq();

  if (dht11_wait_level(GPIO_PIN_RESET, 100U) != 0 ||
      dht11_wait_level(GPIO_PIN_SET, 100U) != 0 ||
      dht11_wait_level(GPIO_PIN_RESET, 100U) != 0)
  {
    __enable_irq();
    return DHT11_ERROR;
  }

  for (uint8_t i = 0U; i < 40U; i++)
  {
    if (dht11_wait_level(GPIO_PIN_SET, 100U) != 0)
    {
      __enable_irq();
      return DHT11_ERROR;
    }

    dht11_delay_us(40);

    if (dht11_read_pin() == GPIO_PIN_SET)
    {
      raw[i / 8U] |= (uint8_t)(1U << (7U - (i % 8U)));
    }

    if (dht11_wait_level(GPIO_PIN_RESET, 100U) != 0)
    {
      __enable_irq();
      return DHT11_ERROR;
    }
  }

  __enable_irq();

  if ((uint8_t)(raw[0] + raw[1] + raw[2] + raw[3]) != raw[4])
  {
    return DHT11_ERROR;
  }

  data->humidity_pct = raw[0];
  data->temperature_c = (int8_t)raw[2];
  dht11_last_read_ms = now;

  return DHT11_OK;
}
