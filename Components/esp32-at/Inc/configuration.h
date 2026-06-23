#pragma once

#define WIFI_SSID "Fam_TG"
#define WIFI_PWD "Soli010520"


#define UTC_OFFSET -6
#define CLIENT_ID "ESP32-1"
#define DOMAIN_NAME "ayi2at0tc9po8-ats.iot.us-east-2.amazonaws.com"

#define MQTT_TOPIC "sensor/kitchen/data"

#define MQTT_PUBLISH_INTERVAL_MS 60000U
#define MQTT_QOS                 0U
#define MQTT_RETAIN              0U

#define ESP32_INIT_MAX_RETRIES     5U
#define ESP32_INIT_RETRY_DELAY_MS  500U
#define ESP32_AWS_MAX_RETRIES      3U