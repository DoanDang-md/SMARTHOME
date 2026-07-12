/**
 * @file RelayNodeApp.h
 * @brief Node Relay ESP-01S — cùng luồng Hybrid: scan kênh, BCAST uplink, recover.
 *
 * HW: RELAY_PIN = GPIO0 (module relay ESP-01 phổ biến — GPIO2 là LED, không điều khiển relay).
 *     RELAY_ACTIVE_LOW=1 nếu module active-low.
 */
#pragma once

#include "DeviceNode.h"
#include "EspNowManager.h"
#include "RelayController.h"
#include "StorageManager.h"
#include "EspNowConfig.h"

#ifndef RELAY_NODE_PIN
#define RELAY_NODE_PIN 0
#endif

/** 1 = digitalWrite LOW = bật */
#ifndef RELAY_ACTIVE_LOW
#define RELAY_ACTIVE_LOW 0
#endif

#ifndef RELAY_REPORT_INTERVAL_MS
#define RELAY_REPORT_INTERVAL_MS 15000UL
#endif

/** Soft recover — cùng logic Hybrid (Discovery giữ kênh) */
#ifndef RELAY_GW_SILENCE_SOFT_MS
#define RELAY_GW_SILENCE_SOFT_MS 15000UL
#endif

/** Full rescan 1–13 — cùng Hybrid */
#ifndef RELAY_GW_SILENCE_RESCAN_MS
#define RELAY_GW_SILENCE_RESCAN_MS 40000UL
#endif

#ifndef RELAY_STORAGE_NS
#define RELAY_STORAGE_NS "relay_node"
#endif

class RelayNodeApp : public DeviceNode, public IEspNowPacketHandler {
public:
    RelayNodeApp()
        : DeviceNode(1, smarthome::DEVICE_RELAY),
          storage_(RELAY_STORAGE_NS),
          relay_(RELAY_NODE_PIN, &storage_),
          lastReportMs_(0),
          lastRecoverMs_(0) {}

    void begin() override {
        Serial.begin(115200);
        delay(100);

        storage_.begin(false);
        setNodeId(storage_.getNodeId(1));

        relay_.setStorage(&storage_);
#if RELAY_ACTIVE_LOW
        relay_.setActiveLow(true);
#endif
        relay_.begin();
        relay_.restoreFromStorage();

        Serial.printf("\n[KHỞI ĐỘNG] Relay Node ID: %u | Relay: %s | GPIO%u activeLow=%d\n",
                      nodeId(),
                      relay_.isOn() ? "BẬT" : "TẮT",
                      static_cast<unsigned>(RELAY_NODE_PIN),
                      RELAY_ACTIVE_LOW ? 1 : 0);

        espnow_.setHandler(this);
        if (!espnow_.begin()) {
            Serial.println("[RELAY] Dừng: ESP-NOW init failed");
            return;
        }

        Serial.println("\n[RELAY NODE] Sẵn sàng (Relay GPIO0 + ESP-NOW BCAST)!");
        espnow_.printMac();
        Serial.println("[WIFI] Node phải cùng kênh WiFi với Gateway (xem Serial GW: Channel=?)");

        // Cùng Hybrid: delay ngắn rồi Auto Channel Scan 1→13
        delay(200);
        espnow_.scanGatewayChannel(&RelayNodeApp::discoveryThunk, this, 400);
        Serial.printf("[WIFI] Khóa kênh hiện tại: %u (phải trùng Channel trên Gateway!)\n",
                      espnow_.channel());
        lastReportMs_  = millis();
        lastRecoverMs_ = millis();
    }

    void loop() override {
        // Bậc thang recover giống Hybrid: soft → full rescan
        maybeRecoverIfGatewaySilent();

        if (millis() - lastReportMs_ >= RELAY_REPORT_INTERVAL_MS) {
            lastReportMs_ = millis();
            Serial.println("\n[TIMER] Báo cáo trạng thái định kỳ lên Gateway...");
            sendReport(smarthome::CMD_ACK_REPORT);
            const unsigned long silence = (espnow_.lastGatewayRxMs() == 0)
                ? 0UL
                : (millis() - espnow_.lastGatewayRxMs());
            Serial.printf("[WIFI] TX ch_cfg=%u ch_hw=%u | Gateway last RX %lu ms trước | Relay=%s\n",
                          espnow_.channel(),
                          espnow_.currentHwChannel(),
                          silence,
                          relay_.isOn() ? "ON" : "OFF");
        }
    }

    void handleCommand(const EspNowPacket& packet) override {
        using namespace smarthome;

        // Đồng bộ Node ID từ Gateway (giống Hybrid)
        if (packet.node_id > 0 && packet.node_id <= EspNowConfig::MAX_NODE_ID
            && packet.node_id != nodeId_) {
            setNodeId(packet.node_id);
            storage_.putNodeId(nodeId_);
            Serial.printf("[NODE ID] Cập nhật ID mới từ Gateway: %u\n", nodeId_);
        }

        // ACK 0x03 keep-alive
        if (packet.command == CMD_ACK_REPORT) {
            return;
        }

        Serial.printf("[NHẬN LỆNH] cmd=0x%02X status=%d (channel=%u)\n",
                      packet.command, packet.status, espnow_.channel());

        if (packet.command == CMD_RELAY_ON) {
            relay_.turnOn(true);
            Serial.println("-> Đã BẬT Relay!");
            sendReport(CMD_ACK_REPORT);
        } else if (packet.command == CMD_RELAY_OFF) {
            relay_.turnOff(true);
            Serial.println("-> Đã TẮT Relay!");
            sendReport(CMD_ACK_REPORT);
        }
    }

    void onEspNowPacket(const uint8_t* /*srcMac*/, const EspNowPacket& packet) override {
        handleCommand(packet);
    }

private:
    void maybeRecoverIfGatewaySilent() {
        if (millis() - lastRecoverMs_ < RELAY_GW_SILENCE_SOFT_MS) return;

        const unsigned long lastRx = espnow_.lastGatewayRxMs();
        const bool neverHeard = (lastRx == 0);
        const unsigned long silence = neverHeard ? millis() : (millis() - lastRx);

        if (neverHeard && millis() > RELAY_GW_SILENCE_RESCAN_MS) {
            lastRecoverMs_ = millis();
            Serial.println("\n[WIFI] Chưa từng nhận ACK Gateway → full rescan");
            espnow_.rescanGatewayChannel(&RelayNodeApp::discoveryThunk, this, 400);
            return;
        }
        if (neverHeard) return;

        if (silence >= RELAY_GW_SILENCE_RESCAN_MS) {
            lastRecoverMs_ = millis();
            Serial.printf("\n[WIFI] Im %lu ms → FULL RESCAN (nghi lệch channel)\n", silence);
            espnow_.rescanGatewayChannel(&RelayNodeApp::discoveryThunk, this, 400);
            Serial.printf("[WIFI] Rescan xong → channel=%u\n", espnow_.channel());
        } else if (silence >= RELAY_GW_SILENCE_SOFT_MS) {
            lastRecoverMs_ = millis();
            Serial.printf("\n[WIFI] Im %lu ms → soft recover Discovery\n", silence);
            espnow_.softRecoverLink(&RelayNodeApp::discoveryThunk, this);
        }
    }

    void sendReport(uint8_t cmd) {
        espnow_.sendToGateway(
            nodeId_,
            static_cast<uint8_t>(deviceType_),
            cmd,
            relay_.statusByte(),
            0.0f,
            0.0f,
            0);
    }

    void sendDiscovery() {
        sendReport(smarthome::CMD_DISCOVERY);
    }

    static void discoveryThunk(void* ctx) {
        if (ctx != nullptr) {
            static_cast<RelayNodeApp*>(ctx)->sendDiscovery();
        }
    }

    StorageManager  storage_;
    RelayController relay_;
    EspNowManager   espnow_;
    unsigned long   lastReportMs_;
    unsigned long   lastRecoverMs_;
};
