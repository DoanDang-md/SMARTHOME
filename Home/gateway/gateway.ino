/**
 * =============================================================================
 * SMART HOME GATEWAY — ESP32-S3 N16R8 (OOP)
 * =============================================================================
 * Flash 16MB / PSRAM 8MB | FreeRTOS dual-core
 * - ESP-NOW hub + Web dashboard + Backend HTTP + Offline cache
 * - AP Setup: SmartHome_Setup / 12345678
 *
 * setup()/loop() chỉ ủy quyền GatewayApp.
 * =============================================================================
 */

#include "GatewayApp.h"

static GatewayApp app;

void setup() {
    app.begin();
}

void loop() {
    app.loopIdle();  // vTaskDelete — FreeRTOS tasks lo toàn bộ
}
