#include "protobuffer.h"
#include "pax.pb.h"
#include "pb_encode.h"
#include <HardwareSerial.h>



int EncodeProtoBuf(int bluetooth_device_count, int wifi_device_count, float core_temperature, int sequence_number, int seconds_uptime, uint8_t * buffer, int max_length)
{
    apipb_PAXMessage message = apipb_PAXMessage_init_zero;
    pb_ostream_t stream = pb_ostream_from_buffer((pb_byte_t*)buffer, max_length);

    message.bluetooth_device_count = bluetooth_device_count;
    message.wifi_device_count = wifi_device_count;
    message.core_temperature = core_temperature;
    message.sequence_number = sequence_number;
    message.seconds_uptime = seconds_uptime;

    bool status = pb_encode(&stream, apipb_PAXMessage_fields, &message);
    int message_length = stream.bytes_written;

    if(!status)
    {
        Serial.println("Protobuf encoding failed");
        return 0;
    }

    return message_length;
}
