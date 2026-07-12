/**
 * @file HybridNodeApp.h
 * @brief Node Hybrid ESP-01S: Relay (GPIO0) + DHT11 (GPIO2) + ESP-NOW.
 *
 * ESP-NOW bắt buộc cùng kênh WiFi với Gateway (kênh do router quyết định).
 * Nếu node khóa nhầm kênh → TX ok=1 nhưng Gateway không RX.
 */
#pragma once

#include "DeviceNode.h"
#include "EspNowManager.h"
#include "RelayController.h"
#include "StorageManager.h"
#include "DhtSensor.h"
#include "EspNowConfig.h"

#ifndef HYBRID_RELAY_PIN
#define HYBRID_RELAY_PIN 0
#endif

#ifndef HYBRID_DHT_PIN
#define HYBRID_DHT_PIN 2
#endif

#ifndef HYBRID_DHT_TYPE
#define HYBRID_DHT_TYPE DHT11
#endif

#ifndef HYBRID_REPORT_INTERVAL_MS
#define HYBRID_REPORT_INTERVAL_MS 5000UL  // ~5s đọc DHT + gửi Gateway
#endif

/** Lâu không nhận ACK → soft Discovery (uplink đã BCAST) */
#ifndef HYBRID_GW_SILENCE_SOFT_MS
#define HYBRID_GW_SILENCE_SOFT_MS 15000UL
#endif

/** Vẫn im → full rescan 1–13 (lệch channel thật) */
#ifndef HYBRID_GW_SILENCE_RESCAN_MS
#define HYBRID_GW_SILENCE_RESCAN_MS 40000UL
#endif

#ifndef HYBRID_STORAGE_NS
#define HYBRID_STORAGE_NS "hybrid_node"
#endif

class HybridNodeApp : public DeviceNode, public IEspNowPacketHandler {
public:
    HybridNodeApp()
        : DeviceNode(1, smarthome::DEVICE_HYBRID),
          storage_(HYBRID_STORAGE_NS),
          relay_(HYBRID_RELAY_PIN, &storage_),
          dht_(HYBRID_DHT_PIN, HYBRID_DHT_TYPE),
          lastReportMs_(0),
          lastRecoverMs_(0) {}

    void begin() override {
        Serial.begin(115200);
        delay(100);

        storage_.begin(false);
        setNodeId(storage_.getNodeId(1));

        relay_.setStorage(&storage_);
        relay_.begin();
        relay_.restoreFromStorage();

        dht_.begin();

        Serial.printf("\n[KHỞI ĐỘNG] Hybrid Node ID: %u | Relay: %s\n",
                      nodeId(), relay_.isOn() ? "BẬT" : "TẮT");

        espnow_.setHandler(this);
        if (!espnow_.begin()) {
            Serial.println("[HYBRID] Dừng: ESP-NOW init failed");
            return;
        }

        Serial.println("\n[HYBRID NODE] Sẵn sàng (Relay + DHT11)!");
        espnow_.printMac();
        Serial.println("[WIFI] Node phải cùng kênh WiFi với Gateway (xem Serial GW: Channel=?)");

        dht_.warmupAndRead(1500);

        delay(200);
        espnow_.scanGatewayChannel(&HybridNodeApp::discoveryThunk, this, 400);
        Serial.printf("[WIFI] Khóa kênh hiện tại: %u (phải trùng Channel trên Gateway!)\n",
                      espnow_.channel());
        lastReportMs_  = millis();
        lastRecoverMs_ = millis();
    }

    void loop() override {
        // Bậc thang recover: soft → broadcast → full rescan (tránh chỉ sống sau rescan)
        maybeRecoverIfGatewaySilent();

        if (millis() - lastReportMs_ >= HYBRID_REPORT_INTERVAL_MS) {
            lastReportMs_ = millis();
            Serial.println("\n[TIMER] Đến giờ đọc cảm biến định kỳ...");

            if (dht_.read()) {
                Serial.println("Đang gửi báo cáo định kỳ về Gateway...");
            } else {
                Serial.println("Đang gửi báo cáo duy trì kết nối (giữ giá trị cũ)...");
            }
            sendReport(smarthome::CMD_ACK_REPORT);
            const unsigned long silence = (espnow_.lastGatewayRxMs() == 0)
                ? 0UL
                : (millis() - espnow_.lastGatewayRxMs());
            Serial.printf("[WIFI] TX ch_cfg=%u ch_hw=%u | Gateway last RX %lu ms trước\n",
                          espnow_.channel(), espnow_.currentHwChannel(), silence);
        }
    }

    void handleCommand(const EspNowPacket& packet) override {
        using namespace smarthome;

        if (packet.node_id > 0 && packet.node_id <= EspNowConfig::MAX_NODE_ID
            && packet.node_id != nodeId_) {
            setNodeId(packet.node_id);
            storage_.putNodeId(nodeId_);
            Serial.printf("[NODE ID] Cập nhật ID mới từ Gateway: %u\n", nodeId_);
        }

        // ACK 0x03 định kỳ từ GW: chỉ keep-alive (đã cập nhật lastGatewayRx trong EspNowManager)
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
        if (millis() - lastRecoverMs_ < HYBRID_GW_SILENCE_SOFT_MS) return;

        const unsigned long lastRx = espnow_.lastGatewayRxMs();
        const bool neverHeard = (lastRx == 0);
        const unsigned long silence = neverHeard ? millis() : (millis() - lastRx);

        if (neverHeard && millis() > HYBRID_GW_SILENCE_RESCAN_MS) {
            lastRecoverMs_ = millis();
            Serial.println("\n[WIFI] Chưa từng nhận ACK Gateway → full rescan");
            espnow_.rescanGatewayChannel(&HybridNodeApp::discoveryThunk, this, 400);
            return;
        }
        if (neverHeard) return;

        if (silence >= HYBRID_GW_SILENCE_RESCAN_MS) {
            lastRecoverMs_ = millis();
            Serial.printf("\n[WIFI] Im %lu ms → FULL RESCAN (nghi lệch channel)\n", silence);
            espnow_.rescanGatewayChannel(&HybridNodeApp::discoveryThunk, this, 400);
            Serial.printf("[WIFI] Rescan xong → channel=%u\n", espnow_.channel());
        } else if (silence >= HYBRID_GW_SILENCE_SOFT_MS) {
            lastRecoverMs_ = millis();
            Serial.printf("\n[WIFI] Im %lu ms → soft recover Discovery\n", silence);
            espnow_.softRecoverLink(&HybridNodeApp::discoveryThunk, this);
        }
    }

    void ensureSensorSample() {
        if (dht_.needsInitialRead()) {
            dht_.read();
        }
    }

    void sendReport(uint8_t cmd) {
        ensureSensorSample();
        espnow_.sendToGateway(
            nodeId_,
            static_cast<uint8_t>(deviceType_),
            cmd,
            relay_.statusByte(),
            dht_.temperature(),
            dht_.humidity(),
            0);
    }

    void sendDiscovery() {
        sendReport(smarthome::CMD_DISCOVERY);
    }

    static void discoveryThunk(void* ctx) {
        if (ctx != nullptr) {
            static_cast<HybridNodeApp*>(ctx)->sendDiscovery();
        }
    }

    StorageManager  storage_;
    RelayController relay_;
    DhtSensor       dht_;
    EspNowManager   espnow_;
    unsigned long   lastReportMs_;
    unsigned long   lastRecoverMs_;
};
