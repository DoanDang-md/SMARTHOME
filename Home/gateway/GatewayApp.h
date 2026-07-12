/**
 * @file GatewayApp.h
 * @brief Orchestrator Gateway ESP32-S3: WiFi, ESP-NOW, FreeRTOS, Web, Backend.
 */
#pragma once

#include <Arduino.h>
#include "StorageManager.h"
#include "NodeRegistry.h"
#include "OfflineCache.h"
#include "EspNowGateway.h"
#include "BackendClient.h"
#include "WifiProvisioner.h"
#include "GatewayWebServer.h"
#include "EspNowConfig.h"
#include "GatewayTypes.h"

class GatewayApp {
public:
    GatewayApp()
        : storage_("smarthome"),
          wifi_(storage_),
          rxQueue_(nullptr) {}

    void begin() {
        instance_ = this;
        Serial.begin(115200);
        delay(1000);

        rxQueue_ = xQueueCreate(20, sizeof(EspNowPacket));
        cache_.begin();
        registry_.begin();
        storage_.begin(false);

        // Truyền storage để rehydrate registry theo MAC (Preferences) khi RX
        espnow_.attach(&registry_, rxQueue_, &storage_);
        backend_.attach(&registry_, &storage_, &espnow_);
        web_.attach(&storage_, &registry_, &espnow_, &backend_, &wifi_);

        if (wifi_.connectSta()) {
            // ESP-NOW trước khi nạp peer list
            if (espnow_.begin()) {
                // Nạp peer từ Flash + in MAC đã đăng ký (đối chiếu với node)
                for (int i = 1; i <= GW_MAX_NODES; ++i) {
                    String prefKey = "mac_" + String(i);
                    if (!storage_.isKey(prefKey.c_str())) continue;
                    uint8_t saved_mac[6];
                    storage_.getBytes(prefKey.c_str(), saved_mac, 6);
                    espnow_.addPeer(saved_mac);
                    Serial.printf("[PEER] ID=%d MAC=%02X:%02X:%02X:%02X:%02X:%02X type=%u\n",
                                  i,
                                  saved_mac[0], saved_mac[1], saved_mac[2],
                                  saved_mac[3], saved_mac[4], saved_mac[5],
                                  static_cast<unsigned>(storage_.getUInt(("type_" + String(i)).c_str(), 1)));
                }
                registry_.loadFromStorage(storage_);
                // set lại PS=NONE sau khi add peer / HTTP (một số stack bật lại sleep)
                WifiProvisioner::disableModemSleep();
                backend_.pullConfigFromServer();
                WifiProvisioner::disableModemSleep();
            }

            web_.beginDashboard();
            xTaskCreatePinnedToCore(taskServerSync, "Sync_Task", 8192, this, 1, nullptr, 1);
            xTaskCreatePinnedToCore(taskWebServer,  "Web_Task",  8192, this, 1, nullptr, 1);
        } else {
            wifi_.startApSetup();
            web_.beginSetupAp();
            xTaskCreatePinnedToCore(taskWebServer, "Web_Task", 8192, this, 1, nullptr, 1);
        }
    }

    /** loop() chính: FreeRTOS đã lo — xóa task loop Arduino. */
    void loopIdle() {
        vTaskDelete(nullptr);
    }

private:
    static GatewayApp* instance_;

    StorageManager   storage_;
    NodeRegistry     registry_;
    OfflineCache     cache_;
    EspNowGateway    espnow_;
    BackendClient    backend_;
    WifiProvisioner  wifi_;
    GatewayWebServer web_;
    QueueHandle_t    rxQueue_;

    void runServerSync() {
        EspNowPacket incoming;
        cached_record_t recordToSync;

        for (;;) {
            if (xQueueReceive(rxQueue_, &incoming, pdMS_TO_TICKS(100)) == pdPASS) {
                registry_.updateFromPacket(incoming);

                if (incoming.command == smarthome::CMD_DISCOVERY) {
                    // Discovery: ACK ngay + báo backend
                    espnow_.sendAckToNode(incoming.mac);
                    backend_.discoverDevice(incoming.mac, incoming.device_type);
                    WifiProvisioner::disableModemSleep(false);
                } else if (incoming.command == smarthome::CMD_IR_DATA) {
                    if (WiFi.status() == WL_CONNECTED) {
                        cached_record_t irRecord = {incoming, millis()};
                        backend_.postSensor(irRecord);
                        WifiProvisioner::disableModemSleep(false);
                    }
                } else {
                    // Report 0x03 (Hybrid T/H, heartbeat relay...): ACK để node không rescan oan
                    if (incoming.command == smarthome::CMD_ACK_REPORT) {
                        espnow_.sendAckToNode(incoming.mac);
                    }
                    if (WiFi.status() == WL_CONNECTED) {
                        while (cache_.count() > 0) {
                            if (cache_.pop(&recordToSync)) {
                                if (!backend_.postSensor(recordToSync)) {
                                    cache_.push(recordToSync.packet);
                                    break;
                                }
                                vTaskDelay(pdMS_TO_TICKS(50));
                            }
                        }
                        cached_record_t cur = {incoming, millis()};
                        if (!backend_.postSensor(cur)) {
                            cache_.push(incoming);
                        }
                        // HTTPClient đôi khi bật lại modem sleep → ESP-NOW RX rơi
                        WifiProvisioner::disableModemSleep(false);
                    } else {
                        cache_.push(incoming);
                    }
                }
            }
        }
    }

    void runWebServer() {
        for (;;) {
            web_.handleClient();
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }

    static void taskServerSync(void* pv) {
        static_cast<GatewayApp*>(pv)->runServerSync();
    }

    static void taskWebServer(void* pv) {
        static_cast<GatewayApp*>(pv)->runWebServer();
    }
};
