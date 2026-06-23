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
    OTA_STATE_STREAM_DESCRIBE,
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
static char s_active_job_id[64];

static char s_topic_notify[OTA_TOPIC_BUF_SIZE];
static char s_topic_get[OTA_TOPIC_BUF_SIZE];
static char s_topic_accepted[OTA_TOPIC_BUF_SIZE];
static char s_topic_rejected[OTA_TOPIC_BUF_SIZE];
static char s_topic_stream_desc[OTA_TOPIC_BUF_SIZE];
static char s_topic_stream_data[OTA_TOPIC_BUF_SIZE];
#define OTA_STREAM_BLOCK_SIZE_ESP_LIMIT 960U
#define OTA_STREAM_BLOCK_SIZE_DEFAULT   OTA_STREAM_BLOCK_SIZE_ESP_LIMIT
#define OTA_STREAM_GET_RETRY_MS         12000U
#define OTA_TX_INITIAL_DELAY_MS         400U
#define OTA_TX_BACKOFF_MIN_MS           500U
#define OTA_TX_BACKOFF_MAX_MS           5000U
#define OTA_STREAM_DESCRIBE_TIMEOUT_MS  30000U
#define OTA_CBOR_REASM_BUF_SIZE         2048U
#define OTA_CBOR_CONT_CHUNK_MAX         128U

static char s_topic_stream_describe[OTA_TOPIC_BUF_SIZE];
static char s_topic_stream_get[OTA_TOPIC_BUF_SIZE];
static char s_topic_stream_rejected[OTA_TOPIC_BUF_SIZE];
static char s_topic_stream_ack[OTA_TOPIC_BUF_SIZE];
static char s_topic_job_update[OTA_TOPIC_BUF_SIZE];
static uint32_t s_last_stream_req_ms;
static uint32_t s_stream_block_size;
static bool s_describe_sent;
static uint8_t s_stream_cbor_reasm[OTA_CBOR_REASM_BUF_SIZE];
static uint16_t s_stream_cbor_reasm_len;

typedef enum {
    OTA_TX_IDLE = 0,
    OTA_TX_GETSTREAM,
} ota_tx_phase_t;

static ota_tx_phase_t s_tx_phase = OTA_TX_IDLE;
static int32_t s_awaiting_block = -1;
static uint32_t s_tx_next_attempt_ms;
static uint32_t s_tx_backoff_ms;

static void ota_log_bytes(const char *prefix, const uint8_t *data, uint16_t len);
static void ota_fail(const char *reason);
static int ota_request_stream_blocks(void);
static int ota_handle_stream_block(const uint8_t *cbor_data, uint16_t cbor_len);
static void ota_process_pending_tx(void);
static void ota_queue_block_followup(int32_t block_id);
static int ota_extract_file_version(const char *file_name, char *ver_out, size_t ver_len);
static int ota_running_version_is(const char *job_ver);
static void ota_skip_job_up_to_date(const char *job_ver);

static void ota_stream_cbor_reset(void)
{
    s_stream_cbor_reasm_len = 0U;
}

static void ota_cap_stream_block_size(void)
{
    if (s_stream_block_size == 0U || s_stream_block_size > OTA_STREAM_BLOCK_SIZE_ESP_LIMIT)
    {
        if (s_stream_block_size > OTA_STREAM_BLOCK_SIZE_ESP_LIMIT)
        {
            printf("OTA cap blockSize %lu -> %u (ESP-AT MQTT limit)\r\n",
                   (unsigned long)s_stream_block_size,
                   (unsigned)OTA_STREAM_BLOCK_SIZE_ESP_LIMIT);
            fflush(stdout);
        }
        s_stream_block_size = OTA_STREAM_BLOCK_SIZE_ESP_LIMIT;
    }
}

static int ota_stream_cbor_is_map_start(const uint8_t *data, uint16_t len)
{
    return (len >= 1U) && (data[0] == 0xBFU || (data[0] & 0xE0U) == 0xA0U);
}

static int ota_stream_cbor_append(const uint8_t *payload, uint16_t len,
                                  const uint8_t **out, uint16_t *out_len)
{
    if (s_stream_cbor_reasm_len > 0U && len < OTA_CBOR_CONT_CHUNK_MAX)
    {
        if ((uint32_t)s_stream_cbor_reasm_len + (uint32_t)len > OTA_CBOR_REASM_BUF_SIZE)
        {
            ota_stream_cbor_reset();
            return 0;
        }

        memcpy(s_stream_cbor_reasm + s_stream_cbor_reasm_len, payload, len);
        s_stream_cbor_reasm_len = (uint16_t)(s_stream_cbor_reasm_len + len);
        *out = s_stream_cbor_reasm;
        *out_len = s_stream_cbor_reasm_len;
        return 1;
    }

    if (ota_stream_cbor_is_map_start(payload, len))
    {
        if (len > OTA_CBOR_REASM_BUF_SIZE)
        {
            ota_stream_cbor_reset();
            return 0;
        }

        memcpy(s_stream_cbor_reasm, payload, len);
        s_stream_cbor_reasm_len = len;
    }
    else if (s_stream_cbor_reasm_len > 0U)
    {
        if ((uint32_t)s_stream_cbor_reasm_len + (uint32_t)len > OTA_CBOR_REASM_BUF_SIZE)
        {
            ota_stream_cbor_reset();
            return 0;
        }

        memcpy(s_stream_cbor_reasm + s_stream_cbor_reasm_len, payload, len);
        s_stream_cbor_reasm_len = (uint16_t)(s_stream_cbor_reasm_len + len);
    }
    else
    {
        return 0;
    }

    *out = s_stream_cbor_reasm;
    *out_len = s_stream_cbor_reasm_len;
    return 1;
}

static int ota_handle_stream_block(const uint8_t *cbor_data, uint16_t cbor_len)
{
    int32_t block_id = -1;
    int32_t file_id = 0;
    const uint8_t *block_payload = NULL;
    size_t block_len = 0U;
    size_t expected_total = 0U;
    bool has_total = ota_cbor_expected_total(cbor_data, cbor_len, &expected_total);

    if (!has_total || cbor_len < expected_total)
    {
        return 0;
    }

    if (!ota_cbor_decode_block(cbor_data, cbor_len, &block_id, &file_id, &block_payload, &block_len))
    {
        printf("OTA CBOR decode fail (%u bytes, expect %u)\r\n",
               (unsigned)cbor_len, (unsigned)expected_total);
        ota_log_bytes("OTA CBOR head: ", cbor_data, cbor_len);
        ota_stream_cbor_reset();
        return 0;
    }

    if (block_len == 0U || block_len > s_stream_block_size)
    {
        printf("OTA bad block size %u\r\n", (unsigned)block_len);
        fflush(stdout);
        ota_stream_cbor_reset();
        return 0;
    }

    ota_stream_cbor_reset();
    s_awaiting_block = -1;

    printf("OTA block %ld (%u bytes)\r\n", (long)block_id, (unsigned)block_len);
    fflush(stdout);

    if (file_id != s_file_id && s_file_id != 0)
    {
        return 0;
    }

    if (block_id != s_expected_block)
    {
        printf("OTA unexpected block %ld (want %ld)\r\n",
               (long)block_id, (long)s_expected_block);
        fflush(stdout);
        return 0;
    }

    if (flash_write_slot1(s_bytes_written, block_payload, (uint32_t)block_len) != 0)
    {
        printf("OTA flash write fail off=%lu len=%u\r\n",
               (unsigned long)s_bytes_written, (unsigned)block_len);
        fflush(stdout);
        ota_fail("flash write");
        return 0;
    }

    s_bytes_written += (uint32_t)block_len;
    s_expected_block++;
    ota_queue_block_followup(block_id);

    if (s_file_size > 0U && s_bytes_written >= s_file_size)
    {
        s_state = OTA_STATE_FINALIZE;
    }

    return 1;
}

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
    snprintf(s_topic_stream_describe, sizeof(s_topic_stream_describe),
             "$aws/things/%s/streams/%s/describe/json", OTA_THING_NAME, s_stream_id);
    snprintf(s_topic_stream_data, sizeof(s_topic_stream_data),
             "$aws/things/%s/streams/%s/data/cbor", OTA_THING_NAME, s_stream_id);
    snprintf(s_topic_stream_get, sizeof(s_topic_stream_get),
             "$aws/things/%s/streams/%s/get/cbor", OTA_THING_NAME, s_stream_id);
    snprintf(s_topic_stream_rejected, sizeof(s_topic_stream_rejected),
             "$aws/things/%s/streams/%s/rejected/cbor", OTA_THING_NAME, s_stream_id);
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

    if (s_state != OTA_STATE_IDLE)
    {
        return 0;
    }

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

static int ota_extract_file_version(const char *file_name, char *ver_out, size_t ver_len)
{
    static const char prefix[] = "air-monitor-ota-";
    static const char suffix[] = ".signed.bin";
    const char *base;
    const char *start;
    const char *end;
    size_t ver_strlen;

    if (file_name == NULL || ver_out == NULL || ver_len == 0U)
    {
        return -1;
    }

    base = strrchr(file_name, '/');
    base = (base != NULL) ? (base + 1) : file_name;

    start = strstr(base, prefix);
    if (start != NULL)
    {
        start += sizeof(prefix) - 1U;
    }
    else
    {
        start = base;
    }

    end = strstr(start, suffix);
    if (end == NULL || end == start)
    {
        return -1;
    }

    ver_strlen = (size_t)(end - start);
    if (ver_strlen >= ver_len)
    {
        return -1;
    }

    memcpy(ver_out, start, ver_strlen);
    ver_out[ver_strlen] = '\0';
    return 0;
}

static int ota_running_version_is(const char *job_ver)
{
    struct image_version running;
    char running_ver[24];

    if (job_ver == NULL || job_ver[0] == '\0')
    {
        return 0;
    }

    if (mcuboot_app_get_running_version(&running) != 0)
    {
        return 0;
    }

    if (mcuboot_app_format_version(&running, running_ver, sizeof(running_ver)) <= 0)
    {
        return 0;
    }

    return (strcmp(running_ver, job_ver) == 0);
}

static void ota_skip_job_up_to_date(const char *job_ver)
{
    printf("OTA skip: already running v%s\r\n", job_ver);
    fflush(stdout);
    s_state = OTA_STATE_IDLE;
    (void)ota_publish_job_status("SUCCEEDED", "already up to date");
    s_active_job_id[0] = '\0';
}

static int ota_parse_job_document(const char *json)
{
    const char *doc = strstr(json, "jobDocument");
    const char *search = (doc != NULL) ? doc : json;
    const char *files = strstr(search, "\"files\"");

    if (files != NULL)
    {
        search = files;
    }

    s_job_id[0] = '\0';
    s_stream_id[0] = '\0';
    s_file_name[0] = '\0';
    s_file_id = 0;
    s_file_size = 0U;

    (void)ota_copy_json_string(json, "jobId", s_job_id, sizeof(s_job_id));

    if (ota_copy_json_string(json, "streamname", s_stream_id, sizeof(s_stream_id)) != 0 &&
        ota_copy_json_string(json, "streamName", s_stream_id, sizeof(s_stream_id)) != 0)
    {
        return -1;
    }

    if (ota_copy_json_string(search, "filepath", s_file_name, sizeof(s_file_name)) != 0 &&
        ota_copy_json_string(search, "fileName", s_file_name, sizeof(s_file_name)) != 0 &&
        ota_copy_json_string(search, "filePath", s_file_name, sizeof(s_file_name)) != 0)
    {
        s_file_name[0] = '\0';
    }

    if (ota_copy_json_number(search, "filesize", &s_file_size) != 0)
    {
        (void)ota_copy_json_number(search, "fileSize", &s_file_size);
    }

    if (ota_copy_json_number(search, "fileid", (uint32_t *)&s_file_id) != 0)
    {
        (void)ota_copy_json_number(search, "fileId", (uint32_t *)&s_file_id);
    }

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

static void ota_log_bytes(const char *prefix, const uint8_t *data, uint16_t len)
{
    uint16_t n = (len > 48U) ? 48U : len;

    printf("%s", prefix);
    for (uint16_t i = 0U; i < n; i++)
    {
        printf("%02x", data[i]);
    }
    if (len > n)
    {
        printf("...");
    }
    printf("\r\n");
    fflush(stdout);
}

static void ota_queue_block_followup(int32_t block_id)
{
    (void)block_id;

    if (s_file_size > 0U && s_bytes_written >= s_file_size)
    {
        return;
    }

    s_tx_phase = OTA_TX_GETSTREAM;
    s_tx_next_attempt_ms = HAL_GetTick() + OTA_TX_INITIAL_DELAY_MS;
}

static void ota_process_pending_tx(void)
{
    if (s_tx_phase != OTA_TX_GETSTREAM || HAL_GetTick() < s_tx_next_attempt_ms)
    {
        return;
    }

    if (s_awaiting_block == s_expected_block &&
        (HAL_GetTick() - s_last_stream_req_ms) < OTA_STREAM_GET_RETRY_MS)
    {
        s_tx_phase = OTA_TX_IDLE;
        return;
    }

    ESP32_MQTT_Poll();

    if (ota_request_stream_blocks() == 0)
    {
        s_awaiting_block = s_expected_block;
        s_tx_phase = OTA_TX_IDLE;
        s_tx_backoff_ms = OTA_TX_BACKOFF_MIN_MS;
        return;
    }

    if (s_tx_backoff_ms < OTA_TX_BACKOFF_MIN_MS)
    {
        s_tx_backoff_ms = OTA_TX_BACKOFF_MIN_MS;
    }
    else if (s_tx_backoff_ms < OTA_TX_BACKOFF_MAX_MS)
    {
        s_tx_backoff_ms += OTA_TX_BACKOFF_MIN_MS;
        if (s_tx_backoff_ms > OTA_TX_BACKOFF_MAX_MS)
        {
            s_tx_backoff_ms = OTA_TX_BACKOFF_MAX_MS;
        }
    }

    s_tx_next_attempt_ms = HAL_GetTick() + s_tx_backoff_ms;
}

static void ota_begin_download(void)
{
    ota_cap_stream_block_size();
    ota_stream_cbor_reset();
    s_tx_phase = OTA_TX_IDLE;
    s_awaiting_block = -1;
    s_tx_next_attempt_ms = 0U;
    s_tx_backoff_ms = OTA_TX_BACKOFF_MIN_MS;
    s_state = OTA_STATE_DOWNLOADING;
    s_last_stream_req_ms = 0U;
    s_tx_phase = OTA_TX_GETSTREAM;
    printf("OTA stream blockSize=%lu\r\n", (unsigned long)s_stream_block_size);
    fflush(stdout);
}

static int ota_request_stream_blocks(void)
{
    uint8_t req[64];
    size_t req_len;

    req_len = ota_cbor_encode_get_stream_request(req, sizeof(req), s_file_id, s_expected_block,
                                                 (int32_t)s_stream_block_size, 1);
    if (req_len == 0U)
    {
        return -1;
    }

    if (ESP32_MQTT_Publish(s_topic_stream_get, req, (uint16_t)req_len, 0U, 0U) != WIFI_OK)
    {
        printf("OTA GetStream failed blk %ld (sz %lu)\r\n",
               (long)s_expected_block, (unsigned long)s_stream_block_size);
        fflush(stdout);
        return -1;
    }

    printf("OTA GetStream blk %ld (sz %lu)\r\n",
           (long)s_expected_block, (unsigned long)s_stream_block_size);
    fflush(stdout);

    s_last_stream_req_ms = HAL_GetTick();

    for (uint8_t i = 0U; i < 5U; i++)
    {
        ESP32_MQTT_Poll();
        HAL_Delay(20);
    }

    return 0;
}

static void ota_fail(const char *reason)
{
    printf("OTA failed: %s\r\n", reason);
    fflush(stdout);
    ota_stream_cbor_reset();
    s_tx_phase = OTA_TX_IDLE;
    s_awaiting_block = -1;
    (void)ota_publish_job_status("FAILED", reason);
    s_active_job_id[0] = '\0';
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
        if (s_state == OTA_STATE_IDLE)
        {
            (void)ota_request_next_job();
        }
        return;
    }

    if (strstr(topic, "/jobs/$next/get/accepted") != NULL)
    {
        if (s_state != OTA_STATE_JOB_REQUEST)
        {
            return;
        }
        if (len >= sizeof(json))
        {
            ota_fail("job json too large");
            return;
        }

        memcpy(json, payload, len);
        json[len] = '\0';

        if (ota_parse_job_document(json) != 0)
        {
            printf("OTA job parse fail: %.200s\r\n", json);
            fflush(stdout);
            ota_fail("job parse error");
            return;
        }

        if (s_active_job_id[0] != '\0' && strcmp(s_job_id, s_active_job_id) == 0)
        {
            return;
        }

        {
            char job_ver[24];

            if (s_file_name[0] != '\0' &&
                ota_extract_file_version(s_file_name, job_ver, sizeof(job_ver)) == 0 &&
                ota_running_version_is(job_ver))
            {
                ota_skip_job_up_to_date(job_ver);
                return;
            }
        }

        strncpy(s_active_job_id, s_job_id, sizeof(s_active_job_id) - 1U);
        s_active_job_id[sizeof(s_active_job_id) - 1U] = '\0';

        printf("OTA job %s stream %s file %s (%lu bytes)\r\n",
               s_job_id, s_stream_id, s_file_name, (unsigned long)s_file_size);
        fflush(stdout);

        /* Advance state before IN_PROGRESS publish (may re-enter via MQTT drain). */
        ota_build_stream_topics();
        s_subscribed_streams = false;
        s_describe_sent = false;
        s_stream_block_size = OTA_STREAM_BLOCK_SIZE_DEFAULT;
        s_bytes_written = 0U;
        s_expected_block = 0;
        s_state = OTA_STATE_STREAM_SUB;

        if (ota_publish_job_status("IN_PROGRESS", NULL) != 0)
        {
            printf("OTA job IN_PROGRESS update failed\r\n");
            fflush(stdout);
        }
        return;
    }

    if (strstr(topic, "/jobs/$next/get/rejected") != NULL)
    {
        if (s_state == OTA_STATE_JOB_REQUEST)
        {
            s_state = OTA_STATE_IDLE;
        }
        return;
    }

    if (strstr(topic, "/streams/") != NULL && strstr(topic, "/rejected/cbor") != NULL)
    {
        printf("OTA stream rejected (%u bytes)\r\n", (unsigned)len);
        ota_log_bytes("OTA stream rejected: ", payload, len);
        return;
    }

    if (strstr(topic, "/streams/") != NULL && strstr(topic, "/description/json") != NULL)
    {
        const char *files = NULL;

        if (len < sizeof(json))
        {
            memcpy(json, payload, len);
            json[len] = '\0';
            files = strstr(json, "\"files\"");
            if (files == NULL)
            {
                files = json;
            }
            if (s_file_size == 0U)
            {
                (void)ota_copy_json_number(files, "filesize", &s_file_size);
                (void)ota_copy_json_number(files, "fileSize", &s_file_size);
            }
            if (ota_copy_json_number(files, "blocksize", &s_stream_block_size) != 0)
            {
                (void)ota_copy_json_number(files, "blockSize", &s_stream_block_size);
            }
            ota_cap_stream_block_size();
        }

        if (s_state == OTA_STATE_STREAM_DESCRIBE)
        {
            ota_begin_download();
        }
        return;
    }

    if (strstr(topic, "/streams/") != NULL && strstr(topic, "/data/cbor") != NULL)
    {
        const uint8_t *cbor_data = NULL;
        uint16_t cbor_len = 0U;

        if (ota_stream_cbor_is_map_start(payload, len) &&
            len >= OTA_CBOR_CONT_CHUNK_MAX &&
            s_stream_cbor_reasm_len > 0U)
        {
            ota_stream_cbor_reset();
        }

        if (!ota_stream_cbor_append(payload, len, &cbor_data, &cbor_len))
        {
            return;
        }

        printf("OTA stream chunk %u bytes (buf %u)\r\n", (unsigned)len, (unsigned)cbor_len);
        fflush(stdout);

        (void)ota_handle_stream_block(cbor_data, cbor_len);
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
    s_active_job_id[0] = '\0';
    s_stream_block_size = OTA_STREAM_BLOCK_SIZE_DEFAULT;
    s_describe_sent = false;

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

    /* Pick up jobs that were queued while the device was offline. */
    if (ota_request_next_job() != 0)
    {
        printf("OTA pending job poll failed\r\n");
        fflush(stdout);
    }

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
                printf("OTA stream desc subscribe failed\r\n");
                fflush(stdout);
                break;
            }
            if (ESP32_MQTT_Subscribe(s_topic_stream_data, 0U) != WIFI_OK)
            {
                printf("OTA stream data subscribe failed\r\n");
                fflush(stdout);
                break;
            }
            if (ESP32_MQTT_Subscribe(s_topic_stream_rejected, 0U) != WIFI_OK)
            {
                printf("OTA stream rejected subscribe failed\r\n");
                fflush(stdout);
                break;
            }

            if (flash_erase_slot1() != 0)
            {
                printf("OTA slot1 erase failed\r\n");
                fflush(stdout);
                ota_fail("slot1 erase");
                break;
            }

            flash_set_progress_callback(ota_flash_progress, s_file_size);
            s_bytes_written = 0U;
            s_expected_block = 0;
            s_subscribed_streams = true;
            s_state = OTA_STATE_STREAM_DESCRIBE;
            s_describe_sent = false;
            s_last_stream_req_ms = HAL_GetTick();
            printf("OTA stream subscribed, requesting description...\r\n");
            fflush(stdout);
        }
        break;

    case OTA_STATE_STREAM_DESCRIBE:
        if (!s_describe_sent)
        {
            static const char describe_req[] = "{}";

            if (ESP32_MQTT_Publish(s_topic_stream_describe, (const uint8_t *)describe_req,
                                   (uint16_t)(sizeof(describe_req) - 1U), 0U, 0U) != WIFI_OK)
            {
                printf("OTA DescribeStream publish failed\r\n");
                fflush(stdout);
                break;
            }
            s_describe_sent = true;
            s_last_stream_req_ms = HAL_GetTick();
        }
        else if ((HAL_GetTick() - s_last_stream_req_ms) > OTA_STREAM_DESCRIBE_TIMEOUT_MS)
        {
            printf("OTA describe timeout, using default blockSize\r\n");
            fflush(stdout);
            ota_begin_download();
        }
        break;

    case OTA_STATE_DOWNLOADING:
        ota_process_pending_tx();
        if (s_tx_phase == OTA_TX_IDLE &&
            s_file_size > 0U && s_bytes_written < s_file_size &&
            s_awaiting_block == s_expected_block &&
            (HAL_GetTick() - s_last_stream_req_ms) > OTA_STREAM_GET_RETRY_MS)
        {
            printf("OTA retry GetStream block %ld\r\n", (long)s_expected_block);
            fflush(stdout);
            s_awaiting_block = -1;
            s_tx_phase = OTA_TX_GETSTREAM;
            s_tx_next_attempt_ms = HAL_GetTick() + OTA_TX_INITIAL_DELAY_MS;
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
        s_active_job_id[0] = '\0';
        s_state = OTA_STATE_IDLE;
        break;

    default:
        break;
    }
}
