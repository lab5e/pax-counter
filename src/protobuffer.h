#pragma once

#include <stdint.h>

int EncodeProtoBuf(int bluetooth_device_count, int wifi_device_count, float core_temperature, uint8_t * buffer, int max_length);

