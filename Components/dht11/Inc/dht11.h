#pragma once

#include <stdint.h>

typedef enum {
  DHT11_OK = 0,
  DHT11_ERROR
} dht11_status_t;

typedef struct {
  int8_t temperature_c;
  uint8_t humidity_pct;
} dht11_data_t;

void dht11_init(void);
dht11_status_t dht11_read(dht11_data_t *data);
