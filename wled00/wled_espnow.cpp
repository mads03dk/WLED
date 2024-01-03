#include "wled.h"

#ifdef ARDUINO_ARCH_ESP32
#include "esp_now.h"
#else
#include "espnow.h"
#endif

#define ESP_NOW_STATE_UNINIT 0
#define ESP_NOW_STATE_ON 1
#define ESP_NOW_STATE_ERROR 2
#if !defined(ARDUINO_ARCH_ESP32) && !defined(ESP_OK)
#define ESP_OK 0  // add missing constant for stupid esp8266
#endif

#define LEDCOUNT 80
typedef struct __attribute__((packed)) {
    uint16_t offset;      // how many LEDs the LEDS array is offset from pixel 0
    uint8_t R[LEDCOUNT];  // every entry represents a pixel consisting of five-bit RGB values
    uint8_t G[LEDCOUNT];  // every entry represents a pixel consisting of five-bit RGB values
    uint8_t B[LEDCOUNT];  // every entry represents a pixel consisting of five-bit RGB values
} Datapacket;

static int esp_now_state = ESP_NOW_STATE_UNINIT;
uint32_t lastEspNowUpdate = 0;
uint8_t peer_addr[6];

#ifdef ARDUINO_ARCH_ESP32
esp_now_peer_info_t slave;
#endif

#ifdef ESP8266
void OnDataRecvCustom(uint8_t *mac, uint8_t *incomingData, uint8_t len) {
#else
void OnDataRecvCustom(const uint8_t *mac, const uint8_t *incomingData, int len) {
#endif
    // stop if not in slave mode
    if (ESPNowMode != ESPNOW_MODE_SLAVE) return;

    sprintf(last_signal_src, "%02x%02x%02x%02x%02x%02x",
            mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    // copy raw datastream into packet
    Datapacket packet;
    memcpy(&packet, incomingData, sizeof(packet));

    for (uint8_t i = 0; i < LEDCOUNT; i++) {
        uint16_t pixel = packet.offset + i;
        // stop if above the length of the strip, stop
        if (pixel > strip.getLengthTotal()) break;

        if (!realtimeOverride) {
            if (i == 0) {
                // Serial.print("pixel 0:");
                // Serial.println(packet.Leds[i]);
                // Serial.print("R:");
                // Serial.println(red);
                // Serial.print("G:");
                // Serial.println(green);
                // Serial.print("B:");
                // Serial.println(blue);
            }
            setRealtimePixel(pixel, packet.R[i], packet.G[i], packet.B[i], 0);
        }
    }

    if (!realtimeOverride) {
        // Serial.println("[ESPNOW] Showing");
        strip.show();
    }

    realtimeLock(realtimeTimeoutMs, REALTIME_MODE_ESPNOW);
}

void broadcastEspNow() {
    uint16_t pixelCount = strip.getLengthTotal();
    Datapacket packet;
    packet.offset = 0;

    uint8_t packetLedCount = -1;
    for (uint16_t i = 0; i < pixelCount; i++) {
        packetLedCount++;
        uint32_t c = strip.getPixelColor(i);
        packet.R[packetLedCount] = (qadd8(W(c), R(c)));  // R, add white channel to RGB channels as a simple RGBW -> RGB map
        packet.G[packetLedCount] = (qadd8(W(c), G(c)));  // G
        packet.B[packetLedCount] = (qadd8(W(c), B(c)));  // B

        // if the max size for a packet or the last pixel
        if (packetLedCount >= LEDCOUNT-1 || i >= pixelCount-1) {
            // Serial.print("R:");
            // Serial.println(packet.R[0]);
            // Serial.print("G:");
            // Serial.println(packet.G[0]);
            // Serial.print("B:");
            // Serial.println(packet.B[0]);
            // send packet, set offset and reset
            esp_now_send(peer_addr, (uint8_t *)&packet, sizeof(packet));

            // clean the buffer
            memset(&packet.R, 0, sizeof(packet.R));
            memset(&packet.G, 0, sizeof(packet.G));
            memset(&packet.B, 0, sizeof(packet.B));
            Serial.print("Sent ");
            Serial.print(packetLedCount);
            Serial.print(" pixels offset by ");
            Serial.println(packet.offset);

            packet.offset = packetLedCount;
            packetLedCount = 0;
        }
    }
}

void handleEspNow() {
    // if not disabled, make sure it's active
    if (ESPNowMode != ESPNOW_MODE_DISABLED) {
        if ((esp_now_state == ESP_NOW_STATE_UNINIT) && (interfacesInited || apActive)) {  // ESPNOW requires Wifi to be initialized (either STA, or AP Mode)

            Serial.println("[ESPNOW] init");
            if (esp_now_init() == ESP_OK) {
                Serial.println("[ESPNOW] Init success");
            } else {
                Serial.println("[ESPNOW] Init fail");
                delay(250);
            }

#ifdef ESP8266
            if (ESPNowMode == ESPNOW_MODE_MASTER) {
                esp_now_set_self_role(ESP_NOW_ROLE_CONTROLLER);
            } else if (ESPNowMode == ESPNOW_MODE_SLAVE) {
                esp_now_set_self_role(ESP_NOW_ROLE_SLAVE);
            }
#endif
            // if slave, activate callback
            esp_now_register_recv_cb(OnDataRecvCustom);

            // add broadcast address
            for (int ii = 0; ii < 6; ++ii) {
                peer_addr[ii] = (uint8_t)0xff;
            }
#ifdef ARDUINO_ARCH_ESP32
            memcpy(slave.peer_addr, peer_addr, sizeof(peer_addr));  // copy addr
            slave.channel = apChannel;                              // pick a channel
            slave.encrypt = 0;                                      // no encryption

            esp_now_add_peer(&slave);

#else
            esp_now_add_peer(peer_addr, ESP_NOW_ROLE_SLAVE, apChannel, 0, 0);
#endif
            esp_now_state = ESP_NOW_STATE_ON;
        }

        if (esp_now_state == ESP_NOW_STATE_ON) {
            if (ESPNowMode == ESPNOW_MODE_MASTER && lastEspNowUpdate != strip.getLastShow()) {
                broadcastEspNow();
                lastEspNowUpdate = strip.getLastShow();
            }
        }

    } else if (esp_now_state == ESPNOW_MODE_DISABLED && esp_now_state != ESP_NOW_STATE_UNINIT) {
        // if desired disabled, and not de-inited, do it
        esp_now_deinit();
        esp_now_state = ESP_NOW_STATE_UNINIT;
    }
}