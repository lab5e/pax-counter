#include <Arduino.h>

#include "TelenorNBIot.h"
#include "wifi_scan.h"
#include "ble_scan.h"
#include "mac_pool.h"

MACAddressPool pool(300000); // Age limit: 5 minutes

#define TRANSMIT_DELAY_MS 1000 * 60 * 5 // Transmit every 5 minutes
#define BOOT_DELAY_MS 1000 * 60 * 60  // Reboot every hour

extern "C" 
{
  uint8_t temprature_sens_read();
}

void(* resetFunc) (void) = 0;

void setup() 
{
  wifi_scanner_setup();
//  ble_scanner_setup();
  nbiot_setup();
  Serial.begin(115200);
 }

uint32_t seconds_uptime = 0;
uint sequence_number = 0;
void loop() 
{
  unsigned long boot_time_start, boot_time_end, transmit_time_start, transmit_time_end;

  boot_time_start = millis();
  while (true)
  {
    transmit_time_start = millis();
    while (true)
    {
      wifi_scan();
      ble_scanner_setup();
      ble_scan();
      ble_scanner_teardown();
      pool.Log();
      transmit_time_end  = millis();
      boot_time_end = millis();
      float core_temperature = (temprature_sens_read() - 32) / 1.8;
      Serial.print("Core temperature:");
      Serial.println(core_temperature);
      if (transmit_time_end-transmit_time_start > TRANSMIT_DELAY_MS)
      {
        Serial.println("----- Transmitting -----");
        nbiot_status();
        sequence_number++;
        int uptime_s = boot_time_end / 1000;
        Serial.print("Uptime:");
        Serial.println(uptime_s);
        Serial.print("Sequence number:");
        Serial.println(sequence_number);
        nbiot_transmit_message(pool.get_count(BT), pool.get_count(WIFI), core_temperature, sequence_number, uptime_s);
        pool.Purge();
        Serial.println("----- Log purged -----");
        break;
      }
    }
    if (boot_time_end-boot_time_start > BOOT_DELAY_MS)
    {
      resetFunc();
    }
  }
}