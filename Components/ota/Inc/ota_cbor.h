#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

bool ota_cbor_decode_block(const uint8_t *data, size_t len,
                           int32_t *block_id, int32_t *file_id,
                           const uint8_t **payload, size_t *payload_len);

size_t ota_cbor_encode_ack(uint8_t *out, size_t out_len, int32_t block_id, int32_t status);
