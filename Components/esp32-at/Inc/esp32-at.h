#pragma once
#include "main.h"
#include "stm32g4xx_hal_uart.h"
#include "configuration.h"

typedef enum {
    ESP32_OK = 0,
    ESP32_ERROR
} esp32_status_t;

typedef enum {
    WIFI_OK = ESP32_OK,
    WIFI_ERROR = ESP32_ERROR,
    WIFI_TIMEOUT
} Wifi_Status_t;

typedef struct {
    char day[4];
    char month[4];
    int date;
    int hour;
    int min;
    int sec;
    int year;
} sntp_time_t;


esp32_status_t esp32_init(void);
esp32_status_t esp32_init_with_retry(uint8_t max_retries);

Wifi_Status_t ESP32_Connect_AP(const char *ssid, const char *pass);

Wifi_Status_t ESP32_Config_SNTP(void);
Wifi_Status_t ESP32_Get_SNTP_Time(sntp_time_t *time_data);

Wifi_Status_t ESP32_Connect_AWS(void);
Wifi_Status_t ESP32_MQTT_Publish(const char *topic, const uint8_t *payload, uint16_t payload_len, uint8_t qos, uint8_t retain);
Wifi_Status_t ESP32_MQTT_Subscribe(const char *topic_filter, uint8_t qos);
void ESP32_MQTT_Poll(void);
typedef void (*esp32_mqtt_msg_cb_t)(const char *topic, const uint8_t *payload, uint16_t len, void *ctx);
void ESP32_MQTT_SetMessageCallback(esp32_mqtt_msg_cb_t cb, void *ctx);