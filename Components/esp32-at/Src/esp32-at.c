#include "esp32-at.h"
#include "configuration.h"

#include <stdio.h>
#include <string.h>

#define MAX_AT_CMD_SIZE        256U
#define MAX_RX_BUFFER_SIZE     512U
#define UART4_RING_SIZE        1024U
#define AT_CMD_TERMINATOR      "\r\n"
#define AT_OK_STRING           "OK"
#define AT_CMD_TIMEOUT_MS      5000U
#define AT_CMD_LONG_TIMEOUT_MS 30000U

static char at_cmd[MAX_AT_CMD_SIZE];
static char rx_buffer[MAX_RX_BUFFER_SIZE];
static char mqtt_stream_buf[MAX_RX_BUFFER_SIZE];
static uint16_t mqtt_stream_len;
static uint8_t uart4_ring[UART4_RING_SIZE];
static volatile uint16_t uart4_ring_head;
static volatile uint16_t uart4_ring_tail;
static uint8_t uart4_rx_it_byte;

static void esp32_mqtt_process_stream(void);

static void uart4_ring_push(uint8_t byte)
{
  uint16_t next = (uint16_t)((uart4_ring_head + 1U) % UART4_RING_SIZE);

  if (next != uart4_ring_tail)
  {
    uart4_ring[uart4_ring_head] = byte;
    uart4_ring_head = next;
  }
}

static int uart4_ring_pop(uint8_t *byte)
{
  if (uart4_ring_head == uart4_ring_tail)
  {
    return 0;
  }

  *byte = uart4_ring[uart4_ring_tail];
  uart4_ring_tail = (uint16_t)((uart4_ring_tail + 1U) % UART4_RING_SIZE);
  return 1;
}

static void uart4_ring_reset(void)
{
  uart4_ring_head = 0U;
  uart4_ring_tail = 0U;
}

static void uart4_hw_drain_to_ring(void)
{
  while (__HAL_UART_GET_FLAG(&huart4, UART_FLAG_RXNE))
  {
    uart4_ring_push((uint8_t)(huart4.Instance->RDR & 0xFFU));
  }
}

static void uart4_forward_ring_to_mqtt_stream(void)
{
  uint8_t byte;

  uart4_hw_drain_to_ring();
  while (uart4_ring_pop(&byte))
  {
    if (mqtt_stream_len < (MAX_RX_BUFFER_SIZE - 1U))
    {
      mqtt_stream_buf[mqtt_stream_len++] = (char)byte;
      mqtt_stream_buf[mqtt_stream_len] = '\0';
    }
  }
}

static int uart4_read_byte(uint8_t *byte, uint32_t timeout_ms)
{
  uint32_t deadline = HAL_GetTick() + timeout_ms;

  while (HAL_GetTick() < deadline)
  {
    uart4_hw_drain_to_ring();
    if (uart4_ring_pop(byte))
    {
      return 1;
    }
  }

  return 0;
}

static void uart4_rx_start(void)
{
  uart4_ring_reset();
  HAL_NVIC_SetPriority(UART4_IRQn, 6, 0);
  HAL_NVIC_EnableIRQ(UART4_IRQn);
  HAL_UART_Receive_IT(&huart4, &uart4_rx_it_byte, 1U);
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
  if (huart->Instance == UART4)
  {
    uart4_ring_push(uart4_rx_it_byte);
    HAL_UART_Receive_IT(&huart4, &uart4_rx_it_byte, 1U);
  }
}

static void smart_delay(uint32_t ms)
{
  HAL_Delay(ms);
}

static void uart_flush_rx(UART_HandleTypeDef *huart)
{
  uart4_forward_ring_to_mqtt_stream();
  esp32_mqtt_process_stream();
  uart4_ring_reset();

  __HAL_UART_CLEAR_OREFLAG(huart);
  while (__HAL_UART_GET_FLAG(huart, UART_FLAG_RXNE))
  {
    (void)(huart->Instance->RDR & 0xFFU);
  }
}

static uint32_t at_cmd_timeout_ms(const uint8_t *cmd, size_t cmd_len)
{
  if (cmd_len >= 8U && strncmp((const char *)cmd, "AT+CWJAP", 8) == 0)
  {
    return AT_CMD_LONG_TIMEOUT_MS;
  }

  if (cmd_len >= 11U && strncmp((const char *)cmd, "AT+MQTTCONN", 11) == 0)
  {
    return AT_CMD_LONG_TIMEOUT_MS;
  }

  return AT_CMD_TIMEOUT_MS;
}

static esp32_status_t run_at_cmd(uint8_t *cmd, size_t cmd_len, uint8_t *expected)
{
  uint16_t rx_len = 0U;
  uint32_t deadline = HAL_GetTick() + at_cmd_timeout_ms(cmd, cmd_len);

  memset(rx_buffer, 0, sizeof(rx_buffer));
  uart_flush_rx(&huart4);

  if (HAL_UART_Transmit(&huart4, cmd, cmd_len, 1000U) != HAL_OK)
  {
    return ESP32_ERROR;
  }

  while (HAL_GetTick() < deadline)
  {
    uint8_t byte;

    if (uart4_read_byte(&byte, 10U) == 0)
    {
      continue;
    }

    if (rx_len < (MAX_RX_BUFFER_SIZE - 1U))
    {
      rx_buffer[rx_len++] = (char)byte;
      rx_buffer[rx_len] = '\0';
    }

    if (strstr(rx_buffer, (const char *)expected) != NULL)
    {
      uart4_forward_ring_to_mqtt_stream();
      esp32_mqtt_process_stream();
      return ESP32_OK;
    }

    if (strstr(rx_buffer, "ERROR") != NULL || strstr(rx_buffer, "FAIL") != NULL)
    {
      return ESP32_ERROR;
    }
  }

  return ESP32_ERROR;
}

static esp32_status_t run_at_mqtt_publish_raw(const char *topic, const uint8_t *payload, uint16_t payload_len,
                                              uint8_t qos, uint8_t retain)
{
  uint16_t rx_len = 0U;
  uint32_t deadline;
  uint8_t byte;
  int got_prompt = 0;

  memset(rx_buffer, 0, sizeof(rx_buffer));
  uart_flush_rx(&huart4);

  snprintf(at_cmd, sizeof(at_cmd), "AT+MQTTPUBRAW=0,\"%s\",%u,%u,%u\r\n", topic, payload_len, qos, retain);
  if (HAL_UART_Transmit(&huart4, (uint8_t *)at_cmd, strlen(at_cmd), 1000U) != HAL_OK)
  {
    return ESP32_ERROR;
  }

  deadline = HAL_GetTick() + AT_CMD_LONG_TIMEOUT_MS;
  while (HAL_GetTick() < deadline)
  {
    if (uart4_read_byte(&byte, 10U) == 0)
    {
      continue;
    }

    if (rx_len < (MAX_RX_BUFFER_SIZE - 1U))
    {
      rx_buffer[rx_len++] = (char)byte;
      rx_buffer[rx_len] = '\0';
    }

    if (strchr(rx_buffer, '>') != NULL)
    {
      got_prompt = 1;
      break;
    }

    if (strstr(rx_buffer, "ERROR") != NULL || strstr(rx_buffer, "FAIL") != NULL)
    {
      return ESP32_ERROR;
    }
  }

  if (!got_prompt)
  {
    return ESP32_ERROR;
  }

  if (HAL_UART_Transmit(&huart4, (uint8_t *)payload, payload_len, 5000U) != HAL_OK)
  {
    return ESP32_ERROR;
  }

  memset(rx_buffer, 0, sizeof(rx_buffer));
  rx_len = 0U;
  deadline = HAL_GetTick() + AT_CMD_LONG_TIMEOUT_MS;
  while (HAL_GetTick() < deadline)
  {
    if (uart4_read_byte(&byte, 10U) == 0)
    {
      continue;
    }

    if (rx_len < (MAX_RX_BUFFER_SIZE - 1U))
    {
      rx_buffer[rx_len++] = (char)byte;
      rx_buffer[rx_len] = '\0';
    }

    if (strstr(rx_buffer, "+MQTTPUB:OK") != NULL)
    {
      uart4_forward_ring_to_mqtt_stream();
      esp32_mqtt_process_stream();
      return ESP32_OK;
    }

    if (strstr(rx_buffer, "+MQTTPUB:FAIL") != NULL || strstr(rx_buffer, "ERROR") != NULL ||
        strstr(rx_buffer, "FAIL") != NULL)
    {
      return ESP32_ERROR;
    }
  }

  return ESP32_ERROR;
}

static void run_at_cmd_best_effort(const char *cmd)
{
  (void)run_at_cmd((uint8_t *)cmd, strlen(cmd), (uint8_t *)AT_OK_STRING);
}

static void esp32_mqtt_close_stale(void)
{
  printf("Closing stale MQTT session (if any)...\r\n");
  fflush(stdout);

  memset(at_cmd, '\0', MAX_AT_CMD_SIZE);
  snprintf(at_cmd, sizeof(at_cmd), "AT+MQTTCONN?%s", AT_CMD_TERMINATOR);
  if (run_at_cmd((uint8_t *)at_cmd, strlen(at_cmd), (uint8_t *)"+MQTTCONN:") == ESP32_OK)
  {
    if (strstr(rx_buffer, "+MQTTCONN:0,1") != NULL)
    {
      printf("MQTT link 0 still connected on ESP32, tearing down...\r\n");
      fflush(stdout);
    }
  }

  for (uint8_t i = 0U; i < 3U; i++)
  {
    memset(at_cmd, '\0', MAX_AT_CMD_SIZE);
    snprintf(at_cmd, sizeof(at_cmd), "AT+MQTTCLEAN=0%s", AT_CMD_TERMINATOR);
    if (run_at_cmd((uint8_t *)at_cmd, strlen(at_cmd), (uint8_t *)AT_OK_STRING) == ESP32_OK)
    {
      break;
    }
    smart_delay(200);
  }

  uart_flush_rx(&huart4);
  smart_delay(500);
}

static esp32_status_t esp32_sync_at(void)
{
  memset(at_cmd, '\0', MAX_AT_CMD_SIZE);
  snprintf(at_cmd, sizeof(at_cmd), "AT%s", AT_CMD_TERMINATOR);
  return run_at_cmd((uint8_t *)at_cmd, strlen(at_cmd), (uint8_t *)AT_OK_STRING);
}

static int esp32_io_init(void)
{
  uart4_rx_start();
  uart_flush_rx(&huart4);
  return 0;
}

static esp32_status_t esp32_init_once(void)
{
  if (esp32_io_init() < 0)
  {
    return ESP32_ERROR;
  }

  esp32_mqtt_close_stale();
  smart_delay(500);

  if (esp32_sync_at() != ESP32_OK)
  {
    return ESP32_ERROR;
  }

  memset(at_cmd, '\0', MAX_AT_CMD_SIZE);
  snprintf(at_cmd, sizeof(at_cmd), "ATE0%s", AT_CMD_TERMINATOR);
  if (run_at_cmd((uint8_t *)at_cmd, strlen(at_cmd), (uint8_t *)AT_OK_STRING) != ESP32_OK)
  {
    return ESP32_ERROR;
  }

  memset(at_cmd, '\0', MAX_AT_CMD_SIZE);
  snprintf(at_cmd, sizeof(at_cmd), "AT+CWMODE=1%s", AT_CMD_TERMINATOR);
  return run_at_cmd((uint8_t *)at_cmd, strlen(at_cmd), (uint8_t *)AT_OK_STRING);
}

esp32_status_t esp32_init_with_retry(uint8_t max_retries)
{
  if (max_retries == 0U)
  {
    max_retries = 1U;
  }

  for (uint8_t attempt = 1U; attempt <= max_retries; attempt++)
  {
    printf("esp32_init attempt %u/%u...\r\n", attempt, max_retries);
    fflush(stdout);

    if (esp32_init_once() == ESP32_OK)
    {
      return ESP32_OK;
    }

    printf("esp32_init attempt %u failed, recovering...\r\n", attempt);
    fflush(stdout);

    uart_flush_rx(&huart4);
    run_at_cmd_best_effort("AT+MQTTCLEAN=0\r\n");
    smart_delay(ESP32_INIT_RETRY_DELAY_MS * attempt);
  }

  return ESP32_ERROR;
}

esp32_status_t esp32_init(void)
{
  return esp32_init_with_retry(ESP32_INIT_MAX_RETRIES);
}

Wifi_Status_t ESP32_Connect_AP(const char *ssid, const char *pass)
{
  memset(at_cmd, '\0', MAX_AT_CMD_SIZE);
  snprintf(at_cmd, sizeof(at_cmd), "AT+CWJAP=\"%s\",\"%s\"%s", ssid, pass, AT_CMD_TERMINATOR);

  printf("Connecting to AP: %s...\r\n", ssid);
  fflush(stdout);

  if (run_at_cmd((uint8_t *)at_cmd, strlen(at_cmd), (uint8_t *)AT_OK_STRING) != ESP32_OK)
  {
    printf("Error: Failed to associate with Access Point.\r\n");
    fflush(stdout);
    return WIFI_ERROR;
  }

  smart_delay(1000);

  memset(at_cmd, '\0', MAX_AT_CMD_SIZE);
  snprintf(at_cmd, sizeof(at_cmd), "AT+CIPSTA?%s", AT_CMD_TERMINATOR);
  run_at_cmd((uint8_t *)at_cmd, strlen(at_cmd), (uint8_t *)AT_OK_STRING);

  printf("Network Details parsed via ring buffer.\r\n");
  fflush(stdout);

  return WIFI_OK;
}

Wifi_Status_t ESP32_Config_SNTP(void)
{
  const char *ntp_server1 = "pool.ntp.org";
  const char *ntp_server2 = "time.google.com";

  printf("start SNTP configuration...\r\n");
  fflush(stdout);

  memset(at_cmd, '\0', MAX_AT_CMD_SIZE);
  snprintf(at_cmd, sizeof(at_cmd), "AT+CIPSNTPCFG=1,%d,\"%s\",\"%s\"%s",
           UTC_OFFSET, ntp_server1, ntp_server2, AT_CMD_TERMINATOR);

  if (run_at_cmd((uint8_t *)at_cmd, strlen(at_cmd), (uint8_t *)AT_OK_STRING) == ESP32_OK)
  {
    return WIFI_OK;
  }
  return WIFI_ERROR;
}

Wifi_Status_t ESP32_Get_SNTP_Time(sntp_time_t *time_data)
{
  const int max_retries = 10;

  memset(at_cmd, '\0', MAX_AT_CMD_SIZE);
  snprintf(at_cmd, sizeof(at_cmd), "AT+CIPSNTPTIME?%s", AT_CMD_TERMINATOR);

  for (int retries = 0; retries < max_retries; retries++)
  {
    smart_delay(1000);
    printf("verifying time (attempt %d/%d)...\r\n", retries + 1, max_retries);
    fflush(stdout);

    if (run_at_cmd((uint8_t *)at_cmd, strlen(at_cmd), (uint8_t *)AT_OK_STRING) != ESP32_OK)
    {
      continue;
    }

    char *p_sntp_time = strstr(rx_buffer, "+CIPSNTPTIME:");

    if (!p_sntp_time || sscanf(p_sntp_time, "+CIPSNTPTIME:%3s %3s %d %d:%d:%d %d",
                               time_data->day, time_data->month, &time_data->date,
                               &time_data->hour, &time_data->min, &time_data->sec,
                               &time_data->year) != 7)
    {
      continue;
    }

    if (time_data->year >= 2026)
    {
      printf(" Time retrieved successfully:\r\n");
      printf(" Date: %s, %s %d, %d\r\n", time_data->day, time_data->month, time_data->date, time_data->year);
      printf(" Time: %02d:%02d:%02d CST\r\n", time_data->hour, time_data->min, time_data->sec);
      fflush(stdout);
      return WIFI_OK;
    }
  }

  printf("SNTP synchronization failed.\r\n");
  fflush(stdout);
  return WIFI_ERROR;
}

Wifi_Status_t ESP32_Connect_AWS(void)
{
  for (uint8_t attempt = 1U; attempt <= ESP32_AWS_MAX_RETRIES; attempt++)
  {
    printf("AWS connect attempt %u/%u...\r\n", attempt, ESP32_AWS_MAX_RETRIES);
    fflush(stdout);

    esp32_mqtt_close_stale();

    printf("MQTT start connection...\r\n");
    fflush(stdout);

    memset(at_cmd, '\0', MAX_AT_CMD_SIZE);
    snprintf(at_cmd, sizeof(at_cmd), "AT+MQTTUSERCFG=0,5,\"%s\",\"\",\"\",0,0,\"\"%s", CLIENT_ID, AT_CMD_TERMINATOR);
    if (run_at_cmd((uint8_t *)at_cmd, strlen(at_cmd), (uint8_t *)AT_OK_STRING) != ESP32_OK)
    {
      printf("Error: MQTTUSERCFG failed to acknowledge.\r\n");
      fflush(stdout);
      smart_delay(ESP32_INIT_RETRY_DELAY_MS * attempt);
      continue;
    }

    memset(at_cmd, '\0', MAX_AT_CMD_SIZE);
    snprintf(at_cmd, sizeof(at_cmd), "AT+MQTTCONNCFG=0,120,0,\"\",\"\",0,0%s", AT_CMD_TERMINATOR);
    if (run_at_cmd((uint8_t *)at_cmd, strlen(at_cmd), (uint8_t *)AT_OK_STRING) != ESP32_OK)
    {
      printf("Error: MQTTCONNCFG failed to acknowledge.\r\n");
      fflush(stdout);
      smart_delay(ESP32_INIT_RETRY_DELAY_MS * attempt);
      continue;
    }

    printf("Connection to MQTT broker...\r\n");
    fflush(stdout);

    memset(at_cmd, '\0', MAX_AT_CMD_SIZE);
    snprintf(at_cmd, sizeof(at_cmd), "AT+MQTTCONN=0,\"%s\",8883,1%s", DOMAIN_NAME, AT_CMD_TERMINATOR);

    if (run_at_cmd((uint8_t *)at_cmd, strlen(at_cmd), (uint8_t *)"+MQTTCONNECTED") == ESP32_OK)
    {
      printf("CONNECTED TO AWS CORE SUCCESSFULLY!\r\n");
      fflush(stdout);
      return WIFI_OK;
    }

    printf("AWS connect attempt %u failed, cleaning up...\r\n", attempt);
    fflush(stdout);
    esp32_mqtt_close_stale();
    smart_delay(ESP32_INIT_RETRY_DELAY_MS * attempt);
  }

  printf("AWS Connection timed out or handshake rejected.\r\n");
  fflush(stdout);
  return WIFI_ERROR;
}

Wifi_Status_t ESP32_MQTT_Publish(const char *topic, const uint8_t *payload, uint16_t payload_len, uint8_t qos, uint8_t retain)
{
  if (topic == NULL || payload == NULL || payload_len == 0U)
  {
    return WIFI_ERROR;
  }

  if (run_at_mqtt_publish_raw(topic, payload, payload_len, qos, retain) == ESP32_OK)
  {
    return WIFI_OK;
  }

  printf("MQTT publish error response: %s\r\n", rx_buffer);
  fflush(stdout);
  return WIFI_ERROR;
}

Wifi_Status_t ESP32_MQTT_Subscribe(const char *topic_filter, uint8_t qos)
{
  memset(at_cmd, '\0', MAX_AT_CMD_SIZE);
  snprintf(at_cmd, sizeof(at_cmd), "AT+MQTTSUB=0,\"%s\",%u\r\n", topic_filter, qos);

  if (run_at_cmd((uint8_t *)at_cmd, strlen(at_cmd), (uint8_t *)AT_OK_STRING) == ESP32_OK)
  {
    return WIFI_OK;
  }
  return WIFI_ERROR;
}

static void esp32_mqtt_print_payload(const char *topic, const char *payload)
{
  int temp_whole = 0;
  int temp_frac = 0;
  unsigned int humidity = 0U;

  if (sscanf(payload, "{\"temp\": %d.%d, \"humidity\": %u}", &temp_whole, &temp_frac, &humidity) == 3 ||
      sscanf(payload, "{\"temp\":%d.%d,\"humidity\":%u}", &temp_whole, &temp_frac, &humidity) == 3)
  {
    printf("MQTT recv [%s]: temp=%d.%02d humidity=%u\r\n", topic, temp_whole, temp_frac, humidity);
  }
  else
  {
    printf("MQTT recv [%s]: %s\r\n", topic, payload);
  }
  fflush(stdout);
}

static void esp32_mqtt_process_stream(void)
{
  char *msg;

  while ((msg = strstr(mqtt_stream_buf, "+MQTTSUBRECV:")) != NULL)
  {
    char *payload_start;
    char topic[128];
    int link_id = 0;
    int data_len = 0;
    char payload[128];

    if (sscanf(msg, "+MQTTSUBRECV:%d,\"%127[^\"]\",%d,", &link_id, topic, &data_len) != 3)
    {
      break;
    }

    payload_start = strchr(msg, ',');
    if (payload_start == NULL)
    {
      break;
    }
    payload_start = strchr(payload_start + 1U, ',');
    if (payload_start == NULL)
    {
      break;
    }
    payload_start = strchr(payload_start + 1U, ',');
    if (payload_start == NULL)
    {
      break;
    }
    payload_start++;

    if (data_len <= 0 || data_len >= (int)sizeof(payload))
    {
      break;
    }

    if ((uint16_t)((payload_start - mqtt_stream_buf) + data_len) > mqtt_stream_len)
    {
      break;
    }

    memcpy(payload, payload_start, (size_t)data_len);
    payload[data_len] = '\0';
    esp32_mqtt_print_payload(topic, payload);

    payload_start += data_len;
    while (payload_start < (mqtt_stream_buf + mqtt_stream_len) &&
           (*payload_start == '\r' || *payload_start == '\n'))
    {
      payload_start++;
    }

    {
      size_t remaining = mqtt_stream_len - (uint16_t)(payload_start - mqtt_stream_buf);
      memmove(mqtt_stream_buf, payload_start, remaining);
      mqtt_stream_len = (uint16_t)remaining;
      mqtt_stream_buf[mqtt_stream_len] = '\0';
    }
  }

  if (mqtt_stream_len >= (MAX_RX_BUFFER_SIZE - 1U))
  {
    mqtt_stream_len = 0U;
    mqtt_stream_buf[0] = '\0';
  }
}

void ESP32_MQTT_Poll(void)
{
  uart4_forward_ring_to_mqtt_stream();
  esp32_mqtt_process_stream();
}
