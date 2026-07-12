/**
 * =============================================================================
 * NODE RELAY — SmartHome ESP-NOW (OOP)
 * =============================================================================
 * Phần cứng : ESP-01S (ESP8266)
 * GPIO Relay: 0  (GPIO0 — module relay ESP-01; GPIO2 chỉ LED board)
 * DeviceType: 1 (DEVICE_RELAY)
 *
 * ESP-NOW: EspNowManager giống Hybrid (Auto Channel Scan + BCAST uplink)
 * Kiến trúc : RelayNodeApp = DeviceNode + RelayController + EspNowManager
 *             + StorageManager
 * setup()/loop() chỉ ủy quyền cho App.
 * =============================================================================
 */

#include "RelayNodeApp.h"

static RelayNodeApp app;

void setup() {
    app.begin();
}

void loop() {
    app.loop();
}
