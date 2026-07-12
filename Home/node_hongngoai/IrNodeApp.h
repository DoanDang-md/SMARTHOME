/**
 * @file IrNodeApp.h
 * @brief Node IR ESP32-C3 SuperMini — learn / blast / raw save + ESP-NOW.
 *
 * Link ESP-NOW: uplink BROADCAST (cùng bài học Hybrid); recover nếu im Gateway.
 */
#pragma once

#include "DeviceNode.h"
#include "EspNowManager.h"
#include "StorageManager.h"
#include "IrController.h"
#include "EspNowConfig.h"

#ifndef IR_NODE_ID
#define IR_NODE_ID 1
#endif

#ifndef IR_REPORT_INTERVAL_MS
#define IR_REPORT_INTERVAL_MS 30000UL
#endif

#ifndef IR_STORAGE_NS
#define IR_STORAGE_NS "ir_store"
#endif

/** IR scan: firmware cũ chờ ~5000ms mỗi kênh (không 400ms như Relay). */
#ifndef IR_SCAN_WAIT_MS
#define IR_SCAN_WAIT_MS 5000UL
#endif

/**
 * Gửi 0x10 lặp sau học (BCAST có thể miss).
 * GW chỉ nhận lần đầu khi is_learning; lần sau = trùng → không mở lại form.
 */
#ifndef IR_LEARN_TX_REPEAT
#define IR_LEARN_TX_REPEAT 3
#endif
#ifndef IR_LEARN_TX_GAP_MS
#define IR_LEARN_TX_GAP_MS 80UL
#endif

/** Lâu không nhận ACK/lệnh từ GW → soft Discovery */
#ifndef IR_GW_SILENCE_SOFT_MS
#define IR_GW_SILENCE_SOFT_MS 45000UL
#endif

/** Vẫn im → full rescan 1–13 */
#ifndef IR_GW_SILENCE_RESCAN_MS
#define IR_GW_SILENCE_RESCAN_MS 90000UL
#endif

class IrNodeApp : public DeviceNode, public IEspNowPacketHandler {
public:
    IrNodeApp()
        : DeviceNode(IR_NODE_ID, smarthome::DEVICE_IR),
          storage_(IR_STORAGE_NS),
          ir_(&storage_),
          lastReportMs_(0),
          lastRecoverMs_(0) {}

    void begin() override {
        Serial.begin(115200);
        delay(500);
        Serial.println("\n============================");
        Serial.println(" IR NODE — SmartHome ESP-NOW (OOP)");
        Serial.println("============================");
        Serial.printf(" GPIO Thu : %d\n", IR_RECV_PIN);
        Serial.printf(" GPIO Phat: %d\n", IR_SEND_PIN);
        Serial.printf(" GPIO LED : %d\n", IR_LED_PIN);

        storage_.begin(false);
        Serial.println("[FLASH] Preferences ir_store đã mở.");

        ir_.setStorage(&storage_);
        ir_.begin();

        espnow_.setHandler(this);
        if (!espnow_.begin()) {
            Serial.println("[ESPNOW] LỖI khởi tạo! Reset sau 5s...");
            delay(5000);
            ESP.restart();
            return;
        }

        Serial.println("[ESPNOW] Sẵn sàng! (uplink BCAST)");
        espnow_.printMac();
        Serial.println("[WIFI] Node phải cùng kênh WiFi với Gateway");

        delay(500);
        espnow_.scanGatewayChannel(&IrNodeApp::discoveryThunk, this, IR_SCAN_WAIT_MS);
        Serial.printf("[WIFI] Khóa kênh: %u | locked=%d\n",
                      espnow_.channel(), espnow_.gatewayLocked() ? 1 : 0);
        lastReportMs_  = millis();
        lastRecoverMs_ = millis();
    }

    void loop() override {
        maybeRecoverIfGatewaySilent();

        // 1) IR poll — học xong: gửi 0x10 lặp (tăng tỉ lệ GW nhận; GW dedupe)
        IrPollEvent ev = ir_.poll();
        if (ev.hasEvent && ev.fromLearning) {
            for (int n = 0; n < IR_LEARN_TX_REPEAT; ++n) {
                sendIr(smarthome::CMD_IR_DATA, ev.code);
                if (n + 1 < IR_LEARN_TX_REPEAT) {
                    delay(IR_LEARN_TX_GAP_MS);
                }
            }
            Serial.printf("[IR LEARN] Đã gửi token 0x%08X lên Gateway x%d (BCAST)\n",
                          static_cast<unsigned>(ev.code), IR_LEARN_TX_REPEAT);
        }

        // 2) Heartbeat 30s — ir_data=0 (không mang token học)
        if (millis() - lastReportMs_ >= IR_REPORT_INTERVAL_MS) {
            lastReportMs_ = millis();
            const unsigned long silence = (espnow_.lastGatewayRxMs() == 0)
                ? 0UL
                : (millis() - espnow_.lastGatewayRxMs());
            Serial.printf("[REPORT] alive | learning=%d | GW last RX %lu ms | ch=%u\n",
                          ir_.isLearning() ? 1 : 0,
                          silence,
                          espnow_.channel());
            sendIr(smarthome::CMD_ACK_REPORT, 0);
        }
    }

    void handleCommand(const EspNowPacket& packet) override {
        using namespace smarthome;

        // ACK 0x03 keep-alive từ Gateway — không xử lý nghiệp vụ
        if (packet.command == CMD_ACK_REPORT) {
            return;
        }

        Serial.printf("[LENH] command=0x%02X  ir_data=0x%08X ch=%u\n",
                      packet.command, static_cast<unsigned>(packet.ir_data),
                      espnow_.channel());

        if (packet.command == CMD_IR_DATA) {
            ir_.blast(packet.ir_data);
            sendIr(CMD_ACK_REPORT, 0);
        } else if (packet.command == CMD_IR_LEARN) {
            sendIr(CMD_ACK_REPORT, 0);  // ACK trước khi bật RX
            ir_.startLearning();
        } else if (packet.command == CMD_IR_SAVE) {
            if (ir_.saveRawToSlot(packet.ir_data)) {
                ir_.onSaveAcknowledged();
                sendIr(CMD_ACK_REPORT, 0);
                Serial.printf("[IR SAVE] Slot #%u OK — đã xóa pending học\n",
                              static_cast<unsigned>(packet.ir_data));
            }
        }
    }

    void onEspNowPacket(const uint8_t* srcMac, const EspNowPacket& packet) override {
        if (packet.command == smarthome::CMD_IR_LEARN
            || packet.command == smarthome::CMD_IR_DATA
            || packet.command == smarthome::CMD_IR_SAVE) {
            espnow_.ensureGatewayMac(srcMac);
        }
        handleCommand(packet);
    }

private:
    void maybeRecoverIfGatewaySilent() {
        if (millis() - lastRecoverMs_ < IR_GW_SILENCE_SOFT_MS) return;

        const unsigned long lastRx = espnow_.lastGatewayRxMs();
        const bool neverHeard = (lastRx == 0);
        const unsigned long silence = neverHeard ? millis() : (millis() - lastRx);

        if (neverHeard && millis() > IR_GW_SILENCE_RESCAN_MS) {
            lastRecoverMs_ = millis();
            Serial.println("\n[WIFI] IR chưa từng nhận ACK Gateway → full rescan");
            espnow_.rescanGatewayChannel(&IrNodeApp::discoveryThunk, this, IR_SCAN_WAIT_MS);
            return;
        }
        if (neverHeard) return;

        if (silence >= IR_GW_SILENCE_RESCAN_MS) {
            lastRecoverMs_ = millis();
            Serial.printf("\n[WIFI] IR im %lu ms → FULL RESCAN\n", silence);
            espnow_.rescanGatewayChannel(&IrNodeApp::discoveryThunk, this, IR_SCAN_WAIT_MS);
        } else if (silence >= IR_GW_SILENCE_SOFT_MS) {
            lastRecoverMs_ = millis();
            Serial.printf("\n[WIFI] IR im %lu ms → soft Discovery BCAST\n", silence);
            espnow_.softRecoverLink(&IrNodeApp::discoveryThunk, this);
        }
    }

    void sendIr(uint8_t cmd, uint32_t irCode) {
        espnow_.sendToGateway(
            nodeId_,
            static_cast<uint8_t>(deviceType_),
            cmd,
            ir_.statusByte(),
            0.0f,
            0.0f,
            irCode);
    }

    void sendDiscovery() {
        sendIr(smarthome::CMD_DISCOVERY, 0);
    }

    static void discoveryThunk(void* ctx) {
        if (ctx != nullptr) {
            static_cast<IrNodeApp*>(ctx)->sendDiscovery();
        }
    }

    StorageManager storage_;
    IrController   ir_;
    EspNowManager  espnow_;
    unsigned long  lastReportMs_;
    unsigned long  lastRecoverMs_;
};
