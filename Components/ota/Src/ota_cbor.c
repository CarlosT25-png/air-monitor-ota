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

    ib = data[(*pos)++];
    if (ib < 0x60U || ib > 0x77U)
    {
        return false;
    }

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
                break;
            }

            if (strcmp(key, "c") == 0 && cbor_read_uint(data, len, &pos, block_id))
            {
                continue;
            }
            if (strcmp(key, "i") == 0 && cbor_read_uint(data, len, &pos, file_id))
            {
                continue;
            }
            if (strcmp(key, "l") == 0 && cbor_read_uint(data, len, &pos, &tmp))
            {
                continue;
            }
            if (strcmp(key, "b") == 0 && cbor_read_bytes(data, len, &pos, payload, payload_len))
            {
                continue;
            }

            return false;
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

        if (strcmp(key, "c") == 0)
        {
            if (!cbor_read_uint(data, len, &pos, block_id))
            {
                return false;
            }
        }
        else if (strcmp(key, "i") == 0)
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
        else if (strcmp(key, "b") == 0)
        {
            if (!cbor_read_bytes(data, len, &pos, payload, payload_len))
            {
                return false;
            }
        }
        else
        {
            return false;
        }
    }

    return (*payload != NULL && *block_id >= 0);
}

size_t ota_cbor_encode_ack(uint8_t *out, size_t out_len, int32_t block_id, int32_t status)
{
    if (out == NULL || out_len < 11U)
    {
        return 0U;
    }

    out[0] = 0xA2U;
    out[1] = 0x61U;
    out[2] = 'c';
    out[3] = 0x1AU;
    out[4] = (uint8_t)((block_id >> 24) & 0xFF);
    out[5] = (uint8_t)((block_id >> 16) & 0xFF);
    out[6] = (uint8_t)((block_id >> 8) & 0xFF);
    out[7] = (uint8_t)(block_id & 0xFF);
    out[8] = 0x61U;
    out[9] = 's';
    out[10] = (uint8_t)status;
    return 11U;
}
