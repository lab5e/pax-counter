#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>

#include "ble_scan.h"
#include "mac_pool.h"

extern MACAddressPool pool;
BLEScan* pBLEScan;
int scanTime = 15; //In seconds

class MyAdvertisedDeviceCallbacks: public BLEAdvertisedDeviceCallbacks 
{
    void onResult(BLEAdvertisedDevice advertisedDevice) 
    {

        // Serial.printf(advertisedDevice.getAddress().toString().c_str());
        // Serial.printf(" Appearance: %d, ", advertisedDevice.getAppearance());
        // Serial.printf(advertisedDevice.getManufacturerData().c_str());
        // Serial.printf(advertisedDevice.getName().c_str());
        // Serial.printf(", ");
        // Serial.printf("RSSI: %d ,", advertisedDevice.getRSSI());
        // Serial.printf("ServiceData:");
        // if (advertisedDevice.haveServiceData())
        // {
        //     Serial.printf(advertisedDevice.getServiceData().c_str());
        // }
        // else
        // {
        //     Serial.printf("<empty>");
        // }
        // Serial.printf(", ");

        // Serial.printf("DataUUID:");

        // if (advertisedDevice.haveServiceUUID())
        // {
        //     Serial.printf(advertisedDevice.getServiceDataUUID().toString().c_str());
        // } else
        // {
        //     Serial.printf("<empty>");
        // }
        // Serial.printf(", ");

        // Serial.printf("ServiceUUID:");

        // if (advertisedDevice.haveServiceUUID())
        // {
        //     Serial.printf(advertisedDevice.getServiceUUID().toString().c_str());
        // } else
        // {
        //     Serial.printf("<empty>");
        // }

        // Serial.printf(", ");
        // Serial.printf("TXPower: %d ,", advertisedDevice.getTXPower());
        // Serial.printf("Payload length: %d, ", advertisedDevice.getPayloadLength());
        // Serial.printf(" Payload: ");
        // for (int i=0; i<advertisedDevice.getPayloadLength(); i++)
        // {
        //     Serial.printf("%02X ", advertisedDevice.getPayload()[i]);
        // }
        // Serial.printf(", Address type: ");

        // switch(advertisedDevice.getAddressType())
        // {
        //     case 0: Serial.println("PUBLIC"); break;
        //     case 1: Serial.println("RANDOM"); break;
        //     case 2: Serial.println("RPA PUBLIC"); break;
        //     case 3: Serial.println("RPA RANDOM"); break;
        // }

        pool.Add(MACSighting(BT, advertisedDevice.getAddress().toString().c_str()));
    }
};

void ble_scanner_setup()
{
    BLEDevice::init("PAX");
    pBLEScan = BLEDevice::getScan(); //create new scan
    pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
    pBLEScan->setActiveScan(true); //active scan uses more power, but get results faster
    pBLEScan->setInterval(100);
    pBLEScan->setWindow(99);  // less or equal setInterval value
}

void ble_scanner_teardown()
{
    BLEDevice::deinit(false);
}

void ble_scan()
{
    Serial.println("Scanning bluetooth...");

    BLEScanResults foundDevices = pBLEScan->start(scanTime, false);
    pBLEScan->clearResults();
    delay(2000);
}
