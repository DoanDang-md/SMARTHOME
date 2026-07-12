/**
 * =============================================================================
 * NODE HỒNG NGOẠI (IR) — SmartHome ESP-NOW (OOP)
 * =============================================================================
 * Phần cứng : ESP32-C3 SuperMini
 * IR Recv   : GPIO 4
 * IR Send   : GPIO 5
 * LED học   : GPIO 8 (active LOW)
 * DeviceType: 2 (DEVICE_IR)
 *
 * Thư viện  : IRremoteESP8266 (crankyoldgit)
 *
 * Lệnh:
 *   0x00 Discovery | 0x03 ACK/Report | 0x10 IR event/blast
 *   0x11 LEARN_IR  | 0x12 SAVE_RAW_IR
 * =============================================================================
 */

#include "IrNodeApp.h"

static IrNodeApp app;

void setup() {
    app.begin();
}

void loop() {
    app.loop();
}
