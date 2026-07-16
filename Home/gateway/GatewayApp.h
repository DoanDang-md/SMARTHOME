/**
 * @file GatewayApp.h
 * @brief Orchestrator Gateway ESP32-S3: WiFi, ESP-NOW, FreeRTOS, Web, Backend.
 *
 * Task_ServerSync ưu tiên: drain RX queue + ACK ESP-NOW trước, HTTP backend sau
 * (batch nhỏ + backoff). Tránh: BE down → flush cache * 2.5s → rxQueue đầy → mất ACK.
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

/** Độ sâu hàng đợi ESP-NOW → Task_ServerSync (cũ 20 dễ đầy khi HTTP kẹt) */
#ifndef GW_RX_QUEUE_LEN
#define GW_RX_QUEUE_LEN 48
#endif
/** Số HTTP post tối đa mỗi vòng loop (sau khi drain RX) */
#ifndef GW_HTTP_BATCH_MAX
#define GW_HTTP_BATCH_MAX 2
#endif
/** Sau 1 lần post fail: không spam retry cache trong khoảng này */
#ifndef GW_BE_BACKOFF_MS
#define GW_BE_BACKOFF_MS 5000UL
#endif

class GatewayApp {
public:
    GatewayApp()
        : storage_("smarthome"),
          wifi_(storage_),
          rxQueue_(nullptr),
          beBackoffUntilMs_(0) {}

    void begin() {
        instance_ = this;
        Serial.begin(115200);
        delay(1000);

        rxQueue_ = xQueueCreate(GW_RX_QUEUE_LEN, sizeof(EspNowPacket));
        if (!rxQueue_) {
            Serial.println("[BOOT] LỖI xQueueCreate rxQueue — ESP-NOW→BE sẽ không chạy!");
        } else {
            Serial.printf("[BOOT] rxQueue len=%d sizeof(EspNowPacket)=%u\n",
                          GW_RX_QUEUE_LEN, static_cast<unsigned>(sizeof(EspNowPacket)));
        }
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

            // mDNS đã start trong connectSta(); log nhắc UI local
            Serial.println("[Web] Dashboard: http://gateway.local  (hoặc IP LAN ở trên)");
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
    unsigned long    beBackoffUntilMs_;

    /**
     * Xử lý 1 gói từ queue: ACK ESP-NOW ngay, HTTP chỉ đưa vào cache (gửi batch sau).
     * Discovery: ACK + discoverDevice (hiếm, chấp nhận HTTP ngắn).
     */
    void processQueuedPacket(const EspNowPacket& incoming) {
        registry_.updateFromPacket(incoming);

        // incoming là const& → mac decay thành const uint8_t*; copy local cho API cần uint8_t*
        uint8_t macCopy[6];
        memcpy(macCopy, incoming.mac, 6);

        if (incoming.command == smarthome::CMD_DISCOVERY) {
            espnow_.sendAckToNode(macCopy);
            // Discover hiếm — gọi BE ngay; không flush cả offline cache ở đây
            if (WiFi.status() == WL_CONNECTED) {
                backend_.discoverDevice(macCopy, incoming.device_type);
                WifiProvisioner::disableModemSleep(false);
            }
            return;
        }

        if (incoming.command == smarthome::CMD_ACK_REPORT) {
            // ACK trước HTTP — node Hybrid/IR/Relay cần RX để không rescan
            espnow_.sendAckToNode(macCopy);
            cache_.push(incoming, true);  // coalesce heartbeat cùng MAC
            return;
        }

        if (incoming.command == smarthome::CMD_IR_DATA) {
            cache_.push(incoming, false);  // không coalesce IR event
            return;
        }

        // Lệnh khác (nếu có): vẫn cache để BE biết
        cache_.push(incoming, false);
    }

    /**
     * Gửi tối đa GW_HTTP_BATCH_MAX bản ghi cache lên BE.
     * Fail → nhét lại + backoff (tránh 20×2.5s block khi BE down).
     */
    void flushBackendLimited() {
        if (WiFi.status() != WL_CONNECTED) return;
        if (millis() < beBackoffUntilMs_) return;
        if (cache_.count() <= 0) return;

        int sent = 0;
        cached_record_t recordToSync;
        while (sent < GW_HTTP_BATCH_MAX && cache_.pop(&recordToSync)) {
            if (!backend_.postSensor(recordToSync)) {
                // Không coalesce khi nhét lại fail — giữ nguyên loại gói
                cache_.push(recordToSync.packet,
                            recordToSync.packet.command == smarthome::CMD_ACK_REPORT);
                beBackoffUntilMs_ = millis() + GW_BE_BACKOFF_MS;
                Serial.printf("[BE] post fail → backoff %lu ms | cache=%d\n",
                              GW_BE_BACKOFF_MS, cache_.count());
                WifiProvisioner::disableModemSleep(false);
                return;
            }
            ++sent;
            vTaskDelay(pdMS_TO_TICKS(20));
        }
        if (sent > 0) {
            WifiProvisioner::disableModemSleep(false);
        }
    }

    void runServerSync() {
        EspNowPacket incoming;

        for (;;) {
            // 1) Chờ gói đầu (hoặc timeout → vẫn thử flush BE nhẹ)
            if (xQueueReceive(rxQueue_, &incoming, pdMS_TO_TICKS(50)) == pdPASS) {
                processQueuedPacket(incoming);

                // 2) Drain hết queue còn lại TRƯỚC HTTP (ACK realtime)
                while (xQueueReceive(rxQueue_, &incoming, 0) == pdPASS) {
                    processQueuedPacket(incoming);
                }
            }

            // 3) HTTP batch nhỏ — không bao giờ while(cache) full khi BE chết
            flushBackendLimited();
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
