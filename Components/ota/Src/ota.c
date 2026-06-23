#include "ota.h"
#include "ota_cbor.h"
#include "configuration.h"
#include "esp32-at.h"
#include "flash.h"
#include "mcuboot_app.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define OTA_TOPIC_BUF_SIZE          160U
#define OTA_JSON_BUF_SIZE           512U
#define OTA_ACK_BUF_SIZE            16U

typedef enum {
    OTA_STATE_IDLE = 0,
    OTA_STATE_JOB_REQUEST,
    OTA_STATE_JOB_ACTIVE,
    OTA_STATE_STREAM_SUB,
    OTA_STATE_DOWNLOADING,
    OTA_STATE_FINALIZE,
    OTA_STATE_REBOOT,
    OTA_STATE_FAILED,
} ota_state_t;

static ota_state_t s_state = OTA_STATE_IDLE;
static char s_job_id[64];
static char s_stream_id[64];
static char s_file_name[96];
static int32_t s_file_id;
static uint32_t s_file_size;
static uint32_t s_bytes_written;
static int32_t s_expected_block;
static bool s_subscribed_streams;

static char s_topic_notify[OTA_TOPIC_BUF_SIZE];
static char s_topic_get[OTA_TOPIC_BUF_SIZE];
static char s_topic_accepted[OTA_TOPIC_BUF_SIZE];
static char s_topic_rejected[OTA_TOPIC_BUF_SIZE];
static char s_topic_stream_desc[OTA_TOPIC_BUF_SIZE];
static char s_topic_stream_data[OTA_TOPIC_BUF_SIZE];
static char s_topic_stream_ack[OTA_TOPIC_BUF_SIZE];
static char s_topic_job_update[OTA_TOPIC_BUF_SIZE];

static void ota_build_topics(void)
{
    snprintf(s_topic_notify, sizeof(s_topic_notify),
             "$aws/things/%s/jobs/notify-next", OTA_THING_NAME);
    snprintf(s_topic_get, sizeof(s_topic_get),
             "$aws/things/%s/jobs/$next/get", OTA_THING_NAME);
    snprintf(s_topic_accepted, sizeof(s_topic_accepted),
             "$aws/things/%s/jobs/$next/get/accepted", OTA_THING_NAME);
    snprintf(s_topic_rejected, sizeof(s_topic_rejected),
             "$aws/things/%s/jobs/$next/get/rejected", OTA_THING_NAME);
}

static void ota_build_stream_topics(void)
{
    snprintf(s_topic_stream_desc, sizeof(s_topic_stream_desc),
             "$aws/things/%s/streams/%s/description/json", OTA_THING_NAME, s_stream_id);
    snprintf(s_topic_stream_data, sizeof(s_topic_stream_data),
             "$aws/things/%s/streams/%s/data/cbor", OTA_THING_NAME, s_stream_id);
    snprintf(s_topic_stream_ack, sizeof(s_topic_stream_ack),
             "$aws/things/%s/streams/%s/data/ack", OTA_THING_NAME, s_stream_id);
}

static int ota_publish_job_status(const char *status, const char *details)
{
    char payload[OTA_JSON_BUF_SIZE];

    if (s_job_id[0] == '\0')
    {
        return -1;
    }

    snprintf(s_topic_job_update, sizeof(s_topic_job_update),
             "$aws/things/%s/jobs/%s/update", OTA_THING_NAME, s_job_id);

    if (details != NULL)
    {
        snprintf(payload, sizeof(payload),
                 "{\"status\":\"%s\",\"statusDetails\":{\"reason\":\"%s\"}}", status, details);
    }
    else
    {
        snprintf(payload, sizeof(payload), "{\"status\":\"%s\"}", status);
    }

    if (ESP32_MQTT_Publish(s_topic_job_update, (const uint8_t *)payload,
                           (uint16_t)strlen(payload), 0U, 0U) != WIFI_OK)
    {
        return -1;
    }

    return 0;
}

static int ota_request_next_job(void)
{
    static const char empty_json[] = "{}";

    if (ESP32_MQTT_Publish(s_topic_get, (const uint8_t *)empty_json,
                           (uint16_t)(sizeof(empty_json) - 1U), 0U, 0U) != WIFI_OK)
    {
        return -1;
    }

    s_state = OTA_STATE_JOB_REQUEST;
    return 0;
}

static const char *ota_json_find(const char *json, const char *key)
{
    char pattern[48];
    const char *start;
    const char *end;

    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    start = strstr(json, pattern);
    if (start == NULL)
    {
        return NULL;
    }

    start = strchr(start, ':');
    if (start == NULL)
    {
        return NULL;
    }
    start++;

    while (*start == ' ' || *start == '\"')
    {
        if (*start == '\"')
        {
            start++;
            end = strchr(start, '\"');
            if (end == NULL)
            {
                return NULL;
            }
            return start;
        }
        start++;
    }

    return start;
}

static int ota_copy_json_string(const char *json, const char *key, char *out, size_t out_len)
{
    const char *start = ota_json_find(json, key);
    const char *end;

    if (start == NULL || out_len == 0U)
    {
        return -1;
    }

    end = strchr(start, '\"');
    if (end == NULL || (size_t)(end - start) >= out_len)
    {
        return -1;
    }

    memcpy(out, start, (size_t)(end - start));
    out[end - start] = '\0';
    return 0;
}

static int ota_copy_json_number(const char *json, const char *key, uint32_t *value)
{
    const char *start = ota_json_find(json, key);
    char *endptr;
    unsigned long val;

    if (start == NULL || value == NULL)
    {
        return -1;
    }

    val = strtoul(start, &endptr, 10);
    if (endptr == start)
    {
        return -1;
    }

    *value = (uint32_t)val;
    return 0;
}

static int ota_parse_job_document(const char *json)
{
    const char *doc = strstr(json, "jobDocument");
    const char *search = (doc != NULL) ? doc : json;

    s_job_id[0] = '\0';
    s_stream_id[0] = '\0';
    s_file_name[0] = '\0';
    s_file_id = 0;
    s_file_size = 0U;

    (void)ota_copy_json_string(json, "jobId", s_job_id, sizeof(s_job_id));

    if (ota_copy_json_string(search, "streamname", s_stream_id, sizeof(s_stream_id)) != 0 &&
        ota_copy_json_string(search, "streamName", s_stream_id, sizeof(s_stream_id)) != 0)
    {
        return -1;
    }

    (void)ota_copy_json_string(search, "fileName", s_file_name, sizeof(s_file_name));
    (void)ota_copy_json_number(search, "fileId", (uint32_t *)&s_file_id);
    (void)ota_copy_json_number(search, "fileSize", &s_file_size);

    if (s_stream_id[0] == '\0')
    {
        return -1;
    }

    return 0;
}

static void ota_flash_progress(uint32_t written, uint32_t total)
{
    if ((written % 4096U) == 0U || written == total)
    {
        printf("OTA flash: %lu / %lu\r\n", (unsigned long)written, (unsigned long)total);
        fflush(stdout);
    }
}

static int ota_send_block_ack(int32_t block_id)
{
    uint8_t ack[OTA_ACK_BUF_SIZE];
    size_t ack_len = ota_cbor_encode_ack(ack, sizeof(ack), block_id, 0);

    if (ack_len == 0U)
    {
        return -1;
    }

    return (ESP32_MQTT_Publish(s_topic_stream_ack, ack, (uint16_t)ack_len, 0U, 0U) == WIFI_OK) ? 0 : -1;
}

static void ota_fail(const char *reason)
{
    printf("OTA failed: %s\r\n", reason);
    fflush(stdout);
    (void)ota_publish_job_status("FAILED", reason);
    s_state = OTA_STATE_FAILED;
}

static void ota_on_mqtt_message(const char *topic, const uint8_t *payload, uint16_t len, void *ctx)
{
    char json[OTA_JSON_BUF_SIZE];

    (void)ctx;

    if (topic == NULL || payload == NULL || len == 0U)
    {
        return;
    }

    if (strstr(topic, "/jobs/notify-next") != NULL)
    {
        (void)ota_request_next_job();
        return;
    }

    if (strstr(topic, "/jobs/$next/get/accepted") != NULL)
    {
        if (len >= sizeof(json))
        {
            ota_fail("job json too large");
            return;
        }

        memcpy(json, payload, len);
        json[len] = '\0';

        if (ota_parse_job_document(json) != 0)
        {
            ota_fail("job parse error");
            return;
        }

        printf("OTA job %s stream %s file %s\r\n", s_job_id, s_stream_id, s_file_name);
        fflush(stdout);

        (void)ota_publish_job_status("IN_PROGRESS", NULL);
        ota_build_stream_topics();
        s_subscribed_streams = false;
        s_state = OTA_STATE_STREAM_SUB;
        return;
    }

    if (strstr(topic, "/jobs/$next/get/rejected") != NULL)
    {
        s_state = OTA_STATE_IDLE;
        return;
    }

    if (strstr(topic, "/streams/") != NULL && strstr(topic, "/description/json") != NULL)
    {
        if (s_file_size == 0U)
        {
            if (len < sizeof(json))
            {
                memcpy(json, payload, len);
                json[len] = '\0';
                (void)ota_copy_json_number(json, "fileSize", &s_file_size);
            }
        }
        return;
    }

    if (strstr(topic, "/streams/") != NULL && strstr(topic, "/data/cbor") != NULL)
    {
        int32_t block_id = -1;
        int32_t file_id = 0;
        const uint8_t *block_payload = NULL;
        size_t block_len = 0U;

        if (!ota_cbor_decode_block(payload, len, &block_id, &file_id, &block_payload, &block_len))
        {
            printf("OTA CBOR decode failed block\r\n");
            fflush(stdout);
            return;
        }

        if (file_id != s_file_id && s_file_id != 0)
        {
            return;
        }

        if (block_id != s_expected_block)
        {
            printf("OTA unexpected block %ld (want %ld)\r\n",
                   (long)block_id, (long)s_expected_block);
            fflush(stdout);
            return;
        }

        if (flash_write_slot1(s_bytes_written, block_payload, (uint32_t)block_len) != 0)
        {
            ota_fail("flash write");
            return;
        }

        s_bytes_written += (uint32_t)block_len;
        s_expected_block++;
        (void)ota_send_block_ack(block_id);

        if (s_file_size > 0U && s_bytes_written >= s_file_size)
        {
            s_state = OTA_STATE_FINALIZE;
        }
    }
}

bool ota_is_active(void)
{
    return s_state != OTA_STATE_IDLE && s_state != OTA_STATE_FAILED;
}

int ota_init(void)
{
    ota_build_topics();
    s_state = OTA_STATE_IDLE;
    s_expected_block = 0;
    s_bytes_written = 0U;
    s_subscribed_streams = false;

    ESP32_MQTT_SetMessageCallback(ota_on_mqtt_message, NULL);

    if (ESP32_MQTT_Subscribe(s_topic_notify, 0U) != WIFI_OK)
    {
        return -1;
    }

    if (ESP32_MQTT_Subscribe(s_topic_accepted, 0U) != WIFI_OK)
    {
        return -1;
    }

    if (ESP32_MQTT_Subscribe(s_topic_rejected, 0U) != WIFI_OK)
    {
        return -1;
    }

    if (mcuboot_app_is_pending())
    {
        printf("MCUboot test boot detected, confirming image\r\n");
        fflush(stdout);
        (void)mcuboot_app_confirm();
    }

    printf("OTA agent ready (notify-next subscribed)\r\n");
    fflush(stdout);
    return 0;
}

void ota_process(void)
{
    switch (s_state)
    {
    case OTA_STATE_STREAM_SUB:
        if (!s_subscribed_streams)
        {
            if (ESP32_MQTT_Subscribe(s_topic_stream_desc, 0U) != WIFI_OK)
            {
                break;
            }
            if (ESP32_MQTT_Subscribe(s_topic_stream_data, 0U) != WIFI_OK)
            {
                break;
            }

            if (flash_erase_slot1() != 0)
            {
                ota_fail("slot1 erase");
                break;
            }

            flash_set_progress_callback(ota_flash_progress, s_file_size);
            s_bytes_written = 0U;
            s_expected_block = 0;
            s_subscribed_streams = true;
            s_state = OTA_STATE_DOWNLOADING;
            printf("OTA stream subscribed, downloading...\r\n");
            fflush(stdout);
        }
        break;

    case OTA_STATE_FINALIZE:
        if (flash_finalize_slot1() != 0)
        {
            ota_fail("flash finalize");
            break;
        }

        (void)ota_publish_job_status("SUCCEEDED", NULL);
        printf("OTA download complete (%lu bytes), rebooting...\r\n", (unsigned long)s_bytes_written);
        fflush(stdout);

        if (mcuboot_app_set_pending() != 0)
        {
            ota_fail("boot_set_pending");
            break;
        }

        s_state = OTA_STATE_REBOOT;
        HAL_Delay(500);
        NVIC_SystemReset();
        break;

    case OTA_STATE_FAILED:
        s_state = OTA_STATE_IDLE;
        break;

    default:
        break;
    }
}
