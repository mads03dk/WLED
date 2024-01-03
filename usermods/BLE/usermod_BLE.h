#pragma once

#include <NimBLEDevice.h>

#include "wled.h"

#define SERVICE_UUID "870c19d6-1f46-4d04-9fb3-4cae6a30d628"
#define DATA_UUID "b4be6074-3dc7-4fcf-bd3e-ba90de70c8a1"
// BLE Characteristics
BLEServer *BleServer;
BLECharacteristic *dataCharacteristic;

class BLEUsermod : public Usermod {
   private:
    class data_callbacks : public BLECharacteristicCallbacks {
        void onWrite(BLECharacteristic *pCharacteristic) {
            String raw = pCharacteristic->getValue().c_str();

            // on main segment, get the relevent variables
            Segment &selseg = strip.getMainSegment();
            byte effectIn = selseg.mode;
            byte speedIn = selseg.speed;
            byte intensityIn = selseg.intensity;
            uint32_t col0 = selseg.colors[0];
            uint32_t col1 = selseg.colors[1];

            if (raw == "GETFX") {
                for (size_t i = 0; i < strip.getModeCount(); i++) {
                    char lineBuffer[128];
                    strncpy_P(lineBuffer, strip.getModeData(i), 127);
                    if (lineBuffer[0] != 0) {
                        char *dataPtr = strchr(lineBuffer, '@');
                        if (dataPtr) *dataPtr = 0;  // terminate mode data after name
                        dataCharacteristic->setValue(lineBuffer);
                        delay(5);
                    }
                }
                return;
            } else if (raw == "GETSTATE") {
                uint32_t col0 = selseg.colors[0];
                uint32_t col1 = selseg.colors[1];

                String str = "CL=";
                str += col0;
                str += "&C2=";
                str += col0;
                str += "&A=";
                str += bri;
                str += "&SX=";
                str += speedIn;
                str += "&IX=";
                str += intensityIn;
                str += "&FX=";
                str += effectIn;
                dataCharacteristic->setValue(str);
            }

            byte colIn[4] = {R(col0), G(col0), B(col0), W(col0)};
            byte colInSec[4] = {R(col1), G(col1), B(col1), W(col1)};
            int pos;

            bool speedChanged = false;
            bool intensityChanged = false;
            bool fxModeChanged = false;

            // primary
            pos = raw.indexOf(F("CL="));
            if (pos > 0) {
                // set color from HEX or 32bit DEC
                colorFromDecOrHexString(colIn, (char *)raw.substring(pos + 3).c_str());
                uint32_t colIn0 = RGBW32(colIn[0], colIn[1], colIn[2], colIn[3]);
                strip.setColor(0, colIn0);
            }

            // secondary color
            pos = raw.indexOf(F("C2="));
            if (pos > 0) {
                // set color from HEX or 32bit DEC
                colorFromDecOrHexString(colInSec, (char *)raw.substring(pos + 3).c_str());
                uint32_t colIn1 = RGBW32(colInSec[0], colInSec[1], colInSec[2], colInSec[3]);
                strip.setColor(1, colIn1);
            }

            updateVal(raw.c_str(), "A=", &bri);  // global brightness

            // if a preset is specified
            pos = raw.indexOf(F("PL="));
            if (pos > 0) {
                byte newPreset;
                updateVal(raw.c_str(), "PL=", &newPreset);  // get ID of new preset
                applyPreset(newPreset);
            }

            speedChanged = updateVal(raw.c_str(), "SX=", &speedIn);                                 // speed
            intensityChanged = updateVal(raw.c_str(), "IX=", &intensityIn);                         // intensity
            fxModeChanged = updateVal(raw.c_str(), "FX=", &effectIn, 0, strip.getModeCount() - 1);  // effect

            // apply to all segments
            for (uint8_t i = 0; i < strip.getSegmentsNum(); i++) {
                Segment &seg = strip.getSegment(i);
                if (fxModeChanged) seg.setMode(effectIn, raw.indexOf(F("FXD=")) > 0);  // apply defaults if FXD= is specified
                if (speedChanged) seg.speed = speedIn;
                if (intensityChanged) seg.intensity = intensityIn;
            }
        }
    };

   public:
    void setup() {
        if (BLEDevice::getInitialized()) {
            return;
        }
        // start BLE
        BLEDevice::init(apSSID);
        BLEDevice::setMTU(BLE_ATT_MTU_MAX);

        BleServer = BLEDevice::createServer();
        BLEService *pService = BleServer->createService(SERVICE_UUID);
        dataCharacteristic = pService->createCharacteristic(DATA_UUID, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR | NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY | NIMBLE_PROPERTY::INDICATE);

        // create callbacks, set values and start advertising
        dataCharacteristic->setCallbacks(new data_callbacks());
        pService->start();
        BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
        pAdvertising->addServiceUUID(SERVICE_UUID);
        pAdvertising->setScanResponse(true);
        pAdvertising->setMinPreferred(0x06);  // functions that help with iPhone connections issue
        pAdvertising->setMaxPreferred(0x12);

        BLEDevice::startAdvertising();
        Serial.println("[BLE] Up!");
    }

    void loop(){};
};