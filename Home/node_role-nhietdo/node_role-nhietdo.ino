/**
 * =============================================================================
 * NODE HYBRID (Relay + DHT11) — SmartHome ESP-NOW (OOP)
 * =============================================================================
 * Phần cứng : ESP-01S (ESP8266)
 * GPIO Relay: 0
 * GPIO DHT  : 2  (DHT11)
 * DeviceType: 4 (DEVICE_HYBRID)
 * Report    : mỗi 10 giây (temp + hum + relay status)
 *
 * Kiến trúc : HybridNodeApp = DeviceNode + RelayController + DhtSensor
 *             + EspNowManager + StorageManager
 * =============================================================================
 */

#include "HybridNodeApp.h"

static HybridNodeApp app;

void setup() {
    app.begin();
}

void loop() {
    app.loop();
}
