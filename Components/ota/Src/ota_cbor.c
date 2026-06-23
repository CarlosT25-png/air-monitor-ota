#include "ota_cbor.h"

#include <string.h>

static bool cbor_read_uint(const uint8_t *data, size_t len, size_t *pos, int32_t *value)
{
    uint8_t ib;
    uint32_t val = 0U;
    size_t i;

    if (*pos >= len)
    {
        return false;
    }

    ib = data[(*pos)++];
    if (ib <= 0x17U)
    {
        *value = (int32_t)ib;
        return true;
    }

    if (ib == 0x18U)
    {
        if (*pos >= len)
        {
            return false;
        }
        *value = (int32_t)data[(*pos)++];
        return true;
    }

    if (ib == 0x19U)
    {
        if ((*pos + 2U) > len)
        {
            return false;
        }
        val = ((uint32_t)data[*pos] << 8) | data[*pos + 1U];
        *pos += 2U;
        *value = (int32_t)val;
        return true;
    }

    if (ib == 0x1AU)
    {
        if ((*pos + 4U) > len)
        {
            return false;
        }
        for (i = 0U; i < 4U; i++)
        {
            val = (val << 8) | data[*pos + i];
        }
        *pos += 4U;
        *value = (int32_t)val;
        return true;
    }

    return false;
}

static bool cbor_read_text(const uint8_t *data, size_t len, size_t *pos, char *text, size_t text_len)
{
    uint8_t ib;
    size_t slen;
    size_t i;

    if (*pos >= len || text_len == 0U)
    {
        return false;
    }

    ib = data[*pos];
    if (ib < 0x60U || ib > 0x77U)
    {
        return false;
    }

    (*pos)++;
    slen = (size_t)(ib - 0x60U);
    if ((*pos + slen) > len || slen >= text_len)
    {
        return false;
    }

    for (i = 0U; i < slen; i++)
    {
        text[i] = (char)data[*pos + i];
    }
    text[slen] = '\0';
    *pos += slen;
    return true;
}

static bool cbor_is_byte_string_header(uint8_t ib)
{
    return (ib >= 0x40U && ib <= 0x57U) || ib == 0x58U || ib == 0x59U || ib == 0x5AU;
}

static bool cbor_read_bytes(const uint8_t *data, size_t len, size_t *pos,
                            const uint8_t **out, size_t *out_len);

static bool cbor_try_read_payload(const uint8_t *data, size_t len, size_t *pos,
                                  const uint8_t **payload, size_t *payload_len)
{
    if (*pos >= len || !cbor_is_byte_string_header(data[*pos]))
    {
        return false;
    }

    return cbor_read_bytes(data, len, pos, payload, payload_len);
}

static bool cbor_skip_value(const uint8_t *data, size_t len, size_t *pos)
{
    uint8_t ib;
    size_t count;
    size_t i;
    size_t n;

    if (*pos >= len)
    {
        return false;
    }

    ib = data[(*pos)++];

    if (ib <= 0x17U)
    {
        return true;
    }
    if (ib == 0x18U)
    {
        if (*pos >= len)
        {
            return false;
        }
        (*pos)++;
        return true;
    }
    if (ib == 0x19U)
    {
        if ((*pos + 2U) > len)
        {
            return false;
        }
        *pos += 2U;
        return true;
    }
    if (ib == 0x1AU)
    {
        if ((*pos + 4U) > len)
        {
            return false;
        }
        *pos += 4U;
        return true;
    }
    if (ib == 0x1BU)
    {
        if ((*pos + 8U) > len)
        {
            return false;
        }
        *pos += 8U;
        return true;
    }

    if (ib >= 0x20U && ib <= 0x37U)
    {
        return true;
    }
    if (ib == 0x38U)
    {
        if (*pos >= len)
        {
            return false;
        }
        (*pos)++;
        return true;
    }
    if (ib == 0x39U)
    {
        if ((*pos + 2U) > len)
        {
            return false;
        }
        *pos += 2U;
        return true;
    }
    if (ib == 0x3AU)
    {
        if ((*pos + 4U) > len)
        {
            return false;
        }
        *pos += 4U;
        return true;
    }
    if (ib == 0x3BU)
    {
        if ((*pos + 8U) > len)
        {
            return false;
        }
        *pos += 8U;
        return true;
    }

    if (ib >= 0x40U && ib <= 0x57U)
    {
        n = (size_t)(ib - 0x40U);
        *pos += n;
        return (*pos <= len);
    }
    if (ib == 0x58U)
    {
        if (*pos >= len)
        {
            return false;
        }
        n = data[(*pos)++];
        *pos += n;
        return (*pos <= len);
    }
    if (ib == 0x59U)
    {
        if ((*pos + 2U) > len)
        {
            return false;
        }
        n = ((size_t)data[*pos] << 8) | data[*pos + 1U];
        *pos += 2U + n;
        return (*pos <= len);
    }
    if (ib == 0x5AU)
    {
        if ((*pos + 4U) > len)
        {
            return false;
        }
        n = ((size_t)data[*pos] << 24) | ((size_t)data[*pos + 1U] << 16) |
            ((size_t)data[*pos + 2U] << 8) | data[*pos + 3U];
        *pos += 4U + n;
        return (*pos <= len);
    }

    if (ib >= 0x60U && ib <= 0x77U)
    {
        n = (size_t)(ib - 0x60U);
        *pos += n;
        return (*pos <= len);
    }
    if (ib == 0x78U)
    {
        if (*pos >= len)
        {
            return false;
        }
        n = data[(*pos)++];
        *pos += n;
        return (*pos <= len);
    }
    if (ib == 0x79U)
    {
        if ((*pos + 2U) > len)
        {
            return false;
        }
        n = ((size_t)data[*pos] << 8) | data[*pos + 1U];
        *pos += 2U + n;
        return (*pos <= len);
    }
    if (ib == 0x7AU)
    {
        if ((*pos + 4U) > len)
        {
            return false;
        }
        n = ((size_t)data[*pos] << 24) | ((size_t)data[*pos + 1U] << 16) |
            ((size_t)data[*pos + 2U] << 8) | data[*pos + 3U];
        *pos += 4U + n;
        return (*pos <= len);
    }

    if (ib >= 0x80U && ib <= 0x97U)
    {
        count = (size_t)(ib - 0x80U);
        for (i = 0U; i < count; i++)
        {
            if (!cbor_skip_value(data, len, pos))
            {
                return false;
            }
        }
        return true;
    }
    if (ib == 0x98U)
    {
        if ((*pos + 2U) > len)
        {
            return false;
        }
        count = ((size_t)data[*pos] << 8) | data[*pos + 1U];
        *pos += 2U;
        for (i = 0U; i < count; i++)
        {
            if (!cbor_skip_value(data, len, pos))
            {
                return false;
            }
        }
        return true;
    }
    if (ib == 0x9FU)
    {
        while (*pos < len && data[*pos] != 0xFFU)
        {
            if (!cbor_skip_value(data, len, pos))
            {
                return false;
            }
        }
        if (*pos >= len)
        {
            return false;
        }
        (*pos)++;
        return true;
    }

    if (ib >= 0xA0U && ib <= 0xB7U)
    {
        count = (size_t)(ib - 0xA0U);
        for (i = 0U; i < count; i++)
        {
            if (!cbor_skip_value(data, len, pos))
            {
                return false;
            }
            if (!cbor_skip_value(data, len, pos))
            {
                return false;
            }
        }
        return true;
    }
    if (ib == 0xB8U)
    {
        if ((*pos + 2U) > len)
        {
            return false;
        }
        count = ((size_t)data[*pos] << 8) | data[*pos + 1U];
        *pos += 2U;
        for (i = 0U; i < count; i++)
        {
            if (!cbor_skip_value(data, len, pos))
            {
                return false;
            }
            if (!cbor_skip_value(data, len, pos))
            {
                return false;
            }
        }
        return true;
    }
    if (ib == 0xBFU)
    {
        while (*pos < len && data[*pos] != 0xFFU)
        {
            if (!cbor_skip_value(data, len, pos))
            {
                return false;
            }
            if (!cbor_skip_value(data, len, pos))
            {
                return false;
            }
        }
        if (*pos >= len)
        {
            return false;
        }
        (*pos)++;
        return true;
    }

    if (ib >= 0xC0U && ib <= 0xD7U)
    {
        return cbor_skip_value(data, len, pos);
    }
    if (ib == 0xD8U)
    {
        if ((*pos + 2U) > len)
        {
            return false;
        }
        *pos += 2U;
        return cbor_skip_value(data, len, pos);
    }

    if (ib == 0xF4U || ib == 0xF5U || ib == 0xF6U || ib == 0xF7U)
    {
        return true;
    }
    if (ib == 0xF9U)
    {
        if ((*pos + 2U) > len)
        {
            return false;
        }
        *pos += 2U;
        return true;
    }
    if (ib == 0xFAU)
    {
        if ((*pos + 4U) > len)
        {
            return false;
        }
        *pos += 4U;
        return true;
    }
    if (ib == 0xFBU)
    {
        if ((*pos + 8U) > len)
        {
            return false;
        }
        *pos += 8U;
        return true;
    }
    if (ib == 0xFFU)
    {
        return true;
    }

    return false;
}

static bool cbor_read_bytes(const uint8_t *data, size_t len, size_t *pos,
                            const uint8_t **out, size_t *out_len)
{
    uint8_t ib;
    size_t blen;

    if (*pos >= len)
    {
        return false;
    }

    ib = data[(*pos)++];
    if (ib >= 0x40U && ib <= 0x57U)
    {
        blen = (size_t)(ib - 0x40U);
    }
    else if (ib == 0x58U)
    {
        if (*pos >= len)
        {
            return false;
        }
        blen = data[(*pos)++];
    }
    else if (ib == 0x59U)
    {
        if ((*pos + 2U) > len)
        {
            return false;
        }
        blen = ((size_t)data[*pos] << 8) | data[*pos + 1U];
        *pos += 2U;
    }
    else if (ib == 0x5AU)
    {
        if ((*pos + 4U) > len)
        {
            return false;
        }
        blen = ((size_t)data[*pos] << 24) | ((size_t)data[*pos + 1U] << 16) |
               ((size_t)data[*pos + 2U] << 8) | data[*pos + 3U];
        *pos += 4U;
    }
    else
    {
        return false;
    }

    if ((*pos + blen) > len)
    {
        return false;
    }

    *out = data + *pos;
    *out_len = blen;
    *pos += blen;
    return true;
}

bool ota_cbor_decode_block(const uint8_t *data, size_t len,
                           int32_t *block_id, int32_t *file_id,
                           const uint8_t **payload, size_t *payload_len)
{
    size_t pos = 0U;
    uint8_t map_head;
    uint8_t pairs;
    char key[8];
    int32_t tmp = 0;

    if (data == NULL || len == 0U || block_id == NULL || file_id == NULL ||
        payload == NULL || payload_len == NULL)
    {
        return false;
    }

    *block_id = -1;
    *file_id = 0;
    *payload = NULL;
    *payload_len = 0U;

    map_head = data[pos];
    if (map_head == 0xBFU)
    {
        pos = 1U;
        while (pos < len && data[pos] != 0xFFU)
        {
            if (!cbor_read_text(data, len, &pos, key, sizeof(key)))
            {
                if (cbor_try_read_payload(data, len, &pos, payload, payload_len))
                {
                    continue;
                }
                break;
            }

            if (strcmp(key, "i") == 0 && cbor_read_uint(data, len, &pos, block_id))
            {
                continue;
            }
            if (strcmp(key, "f") == 0 && cbor_read_uint(data, len, &pos, file_id))
            {
                continue;
            }
            if (strcmp(key, "l") == 0 && cbor_read_uint(data, len, &pos, &tmp))
            {
                if (cbor_try_read_payload(data, len, &pos, payload, payload_len))
                {
                    continue;
                }
                continue;
            }
            if (strcmp(key, "p") == 0 && cbor_read_bytes(data, len, &pos, payload, payload_len))
            {
                continue;
            }
            if (strcmp(key, "b") == 0 && cbor_read_bytes(data, len, &pos, payload, payload_len))
            {
                continue;
            }

            if (!cbor_skip_value(data, len, &pos))
            {
                return false;
            }
        }

        if (pos < len && data[pos] == 0xFFU)
        {
            pos++;
        }

        return (*payload != NULL);
    }

    if ((map_head & 0xE0U) != 0xA0U)
    {
        return false;
    }

    pairs = (uint8_t)(map_head - 0xA0U);
    pos = 1U;

    for (uint8_t i = 0U; i < pairs; i++)
    {
        if (!cbor_read_text(data, len, &pos, key, sizeof(key)))
        {
            return false;
        }

        if (strcmp(key, "i") == 0)
        {
            if (!cbor_read_uint(data, len, &pos, block_id))
            {
                return false;
            }
        }
        else if (strcmp(key, "f") == 0)
        {
            if (!cbor_read_uint(data, len, &pos, file_id))
            {
                return false;
            }
        }
        else if (strcmp(key, "l") == 0)
        {
            if (!cbor_read_uint(data, len, &pos, &tmp))
            {
                return false;
            }
        }
        else if (strcmp(key, "p") == 0 || strcmp(key, "b") == 0)
        {
            if (!cbor_read_bytes(data, len, &pos, payload, payload_len))
            {
                return false;
            }
        }
        else if (!cbor_skip_value(data, len, &pos))
        {
            return false;
        }
    }

    return (*payload != NULL && *block_id >= 0);
}

static bool ota_cbor_measure_block_bytes(const uint8_t *data, size_t len, size_t start,
                                         size_t *total_len)
{
    size_t pos = start;
    size_t blen;
    uint8_t ib;

    if (start >= len || !cbor_is_byte_string_header(data[start]))
    {
        return false;
    }

    ib = data[pos++];
    if (ib >= 0x40U && ib <= 0x57U)
    {
        blen = (size_t)(ib - 0x40U);
    }
    else if (ib == 0x58U)
    {
        if (pos >= len)
        {
            return false;
        }
        blen = data[pos++];
    }
    else if (ib == 0x59U)
    {
        if ((pos + 2U) > len)
        {
            return false;
        }
        blen = ((size_t)data[pos] << 8) | data[pos + 1U];
        pos += 2U;
    }
    else if (ib == 0x5AU)
    {
        if ((pos + 4U) > len)
        {
            return false;
        }
        blen = ((size_t)data[pos] << 24) | ((size_t)data[pos + 1U] << 16) |
               ((size_t)data[pos + 2U] << 8) | data[pos + 3U];
        pos += 4U;
    }
    else
    {
        return false;
    }

    *total_len = pos + blen + 1U;
    return true;
}

bool ota_cbor_expected_total(const uint8_t *data, size_t len, size_t *total_len)
{
    size_t pos = 0U;
    char key[8];
    int32_t tmp = 0;

    if (data == NULL || total_len == NULL || len < 4U)
    {
        return false;
    }

    if (data[0] == 0xBFU)
    {
        pos = 1U;
        while (pos < len && data[pos] != 0xFFU)
        {
            if (!cbor_read_text(data, len, &pos, key, sizeof(key)))
            {
                return ota_cbor_measure_block_bytes(data, len, pos, total_len);
            }

            if (strcmp(key, "l") == 0 && cbor_read_uint(data, len, &pos, &tmp))
            {
                if (ota_cbor_measure_block_bytes(data, len, pos, total_len))
                {
                    return true;
                }
                continue;
            }

            if ((strcmp(key, "p") == 0 || strcmp(key, "b") == 0) &&
                ota_cbor_measure_block_bytes(data, len, pos, total_len))
            {
                return true;
            }

            if (!cbor_skip_value(data, len, &pos))
            {
                return false;
            }

            if (pos < len && data[pos] == 0xFFU)
            {
                pos++;
                *total_len = pos;
                return true;
            }
        }

        return false;
    }

    if ((data[0] & 0xE0U) == 0xA0U)
    {
        uint8_t pairs = (uint8_t)(data[0] - 0xA0U);

        pos = 1U;
        for (uint8_t i = 0U; i < pairs; i++)
        {
            if (!cbor_read_text(data, len, &pos, key, sizeof(key)))
            {
                return false;
            }

            if (strcmp(key, "p") == 0 || strcmp(key, "b") == 0)
            {
                return ota_cbor_measure_block_bytes(data, len, pos, total_len);
            }

            if (!cbor_skip_value(data, len, &pos))
            {
                return false;
            }
        }
    }

    return false;
}

size_t ota_cbor_encode_ack(uint8_t *out, size_t out_len, int32_t block_id, int32_t status)
{
    (void)block_id;

    if (out == NULL || out_len < 8U)
    {
        return 0U;
    }

    out[0] = 0xA2U;
    out[1] = 0x61U;
    out[2] = 'c';
    out[3] = 0x61U;
    out[4] = '1';
    out[5] = 0x61U;
    out[6] = 's';
    out[7] = (uint8_t)status;
    return 8U;
}

static int ota_cbor_append_uint(uint8_t *out, size_t out_len, size_t *pos, int32_t value)
{
    if (*pos >= out_len)
    {
        return -1;
    }

    if (value >= 0 && value <= 23)
    {
        out[(*pos)++] = (uint8_t)value;
        return 0;
    }

    if (value >= 0 && value <= 255)
    {
        if ((*pos + 2U) > out_len)
        {
            return -1;
        }
        out[(*pos)++] = 0x18U;
        out[(*pos)++] = (uint8_t)value;
        return 0;
    }

    if ((*pos + 5U) > out_len)
    {
        return -1;
    }

    out[(*pos)++] = 0x1AU;
    out[(*pos)++] = (uint8_t)((value >> 24) & 0xFF);
    out[(*pos)++] = (uint8_t)((value >> 16) & 0xFF);
    out[(*pos)++] = (uint8_t)((value >> 8) & 0xFF);
    out[(*pos)++] = (uint8_t)(value & 0xFF);
    return 0;
}

size_t ota_cbor_encode_get_stream_request(uint8_t *out, size_t out_len, int32_t file_id,
                                          int32_t block_index, int32_t block_size,
                                          int32_t num_blocks)
{
    size_t pos = 0U;

    if (out == NULL || out_len < 32U)
    {
        return 0U;
    }

    out[pos++] = 0xA5U;

    out[pos++] = 0x61U;
    out[pos++] = (uint8_t)'c';
    out[pos++] = 0x61U;
    out[pos++] = (uint8_t)'1';

    out[pos++] = 0x61U;
    out[pos++] = (uint8_t)'f';
    if (ota_cbor_append_uint(out, out_len, &pos, file_id) != 0)
    {
        return 0U;
    }

    out[pos++] = 0x61U;
    out[pos++] = (uint8_t)'l';
    if (ota_cbor_append_uint(out, out_len, &pos, block_size) != 0)
    {
        return 0U;
    }

    out[pos++] = 0x61U;
    out[pos++] = (uint8_t)'o';
    if (ota_cbor_append_uint(out, out_len, &pos, block_index) != 0)
    {
        return 0U;
    }

    out[pos++] = 0x61U;
    out[pos++] = (uint8_t)'n';
    if (ota_cbor_append_uint(out, out_len, &pos, num_blocks) != 0)
    {
        return 0U;
    }

    return pos;
}
