#ifndef _FOTA_H_
#define _FOTA_H_

#include <stdint.h>
#include "context.h"

#define FIRMWARE_VER_ID 1
#define MODEL_NUMBER_ID 2
#define SERIAL_NUMBER_ID 3
#define CLIENT_MANUFACTURER_ID 4

#define HOST_ID 1
#define PORT_ID 2
#define PATH_ID 3
#define AVAILABLE_ID 4

// This is the reported manufacturer reported by the LwM2M client. It is an
// arbitrary string and will be exposed through the Horde API.
#define CLIENT_MANUFACTURER "Lab5e"

// This is the model number reported by the LwM2M client. It is an arbitrary
// string and will be exposed by the Horde API.
#define CLIENT_MODEL_NUMBER "FM-1-0"

// This is the serial number reported by the LwM2M client. If you have some
// kind of serial number available you can use that, otherwise the IMEI (the
// ID for the cellular modem) or IMSI (The ID of the SIM in use)
#define CLIENT_SERIAL_NUMBER "noname"

// This is the version of the firmware. This must match the versions set on the
// images uploaded via the Span API (at https://api.lab5e.com/span/)
#define CLIENT_FIRMWARE_VER "a.b.c"


size_t encode_tlv_string(uint8_t *buf, uint8_t id, const uint8_t *str);
inline uint8_t tlv_id(const uint8_t *buf, size_t idx);
int decode_tlv_string(const uint8_t *buf, size_t *idx, char *str);
int decode_tlv_uint32(const uint8_t *buf, size_t *idx, uint32_t *val);
int decode_tlv_bool(const uint8_t *buf, size_t *idx, bool *val);


#endif // _FOTA_H_