#include <Arduino.h>

#include "TelenorNBIot.h"
#include "wifi_scan.h"
#include "ble_scan.h"
#include "mac_pool.h"

MACAddressPool pool(300000); // Age limit: 5 minutes

#define TRANSMIT_DELAY_MS 1000 * 60 * 5 // Transmit every 5 minutes
#define BOOT_DELAY_MS 1000 * 60 * 60  // Reboot every hour

void(* resetFunc) (void) = 0;

void setup() 
{
  wifi_scanner_setup();
  ble_scanner_setup();
  nbiot_setup();
  Serial.begin(115200);
 }

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
      ble_scan();
      pool.Log();
      transmit_time_end  = millis();
      boot_time_end = millis();
      if (transmit_time_end-transmit_time_start > TRANSMIT_DELAY_MS)
      {
        Serial.println("----- Transmitting -----");
        nbiot_status();
        nbiot_transmit_message(pool.get_count(BT), pool.get_count(WIFI));
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