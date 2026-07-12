/**
 * @file EspNowGateway.h
 * @brief ESP-NOW phía Gateway: init, peer, send command/ACK, recv → queue.
 *
 * Lưu ý: callback ESP-NOW chạy trong WiFi task (KHÔNG phải ISR) trên ESP32 Arduino.
 * Phải dùng xQueueSend, không dùng xQueueSendFromISR.
 */
#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include "EspNowPacket.h"
#include "EspNowConfig.h"
#include "NodeRegistry.h"
#include "StorageManager.h"
#include "GatewayTypes.h"

class EspNowGateway {
public:
    EspNowGateway()
        : registry_(nullptr), storage_(nullptr), rxQueue_(nullptr) {}

    void attach(NodeRegistry* registry, QueueHandle_t rxQueue, StorageManager* storage = nullptr) {
        registry_ = registry;
        rxQueue_  = rxQueue;
        storage_  = storage;
    }

    bool begin() {
        instance_ = this;

        // Bắt buộc: không modem sleep khi dùng ESP-NOW (nhận report từ node)
        WiFi.setSleep(false);
        esp_wifi_set_ps(WIFI_PS_NONE);

        if (esp_now_init() != ESP_OK) {
            Serial.println("[ESP-NOW] Init failed");
            return false;
        }
        esp_now_register_send_cb(reinterpret_cast<esp_now_send_cb_t>(&EspNowGateway::onSentThunk));
#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
        esp_now_register_recv_cb(&EspNowGateway::onRecvThunk_v3);
#else
        esp_now_register_recv_cb(reinterpret_cast<esp_now_recv_cb_t>(&EspNowGateway::onRecvThunk_v2));
#endif
        Serial.printf("[ESP-NOW] Sẵn sàng! sizeof(EspNowPacket)=%u\n",
                      static_cast<unsigned>(sizeof(EspNowPacket)));
        Serial.printf("[ESP-NOW] Gateway STA MAC=%s ch=%u (node phải gửi đúng MAC+kênh này)\n",
                      WiFi.macAddress().c_str(), WiFi.channel());
        return true;
    }

    /**
     * Thêm/refresh peer. channel=0 = theo kênh WiFi hiện tại (tránh lệch khi router đổi kênh).
     * Gọi lại khi RX để đường ACK GW→Node luôn có peer hợp lệ.
     */
    bool addPeer(uint8_t* mac) {
        if (mac == nullptr) return false;
        if (esp_now_is_peer_exist(mac)) {
            esp_now_del_peer(mac);
        }
        esp_now_peer_info_t peerInfo;
        memset(&peerInfo, 0, sizeof(peerInfo));
        memcpy(peerInfo.peer_addr, mac, 6);
        peerInfo.channel = 0;  // follow current WiFi channel
        peerInfo.ifidx   = WIFI_IF_STA;
        peerInfo.encrypt = false;
        return esp_now_add_peer(&peerInfo) == ESP_OK;
    }

    void removePeer(const uint8_t* mac) {
        if (esp_now_is_peer_exist(mac)) {
            esp_now_del_peer(mac);
        }
    }

    /**
     * ACK 0x03 về node (discovery + report định kỳ).
     * Node Hybrid dựa vào RX này để biết link còn sống; thiếu ACK → rescan oan.
     * Dest MAC = tham số; payload.mac = STA MAC Gateway (node lock theo src radio).
     */
    void sendAckToNode(const uint8_t* mac) {
        if (mac == nullptr) return;
        EspNowPacket ack;
        memset(&ack, 0, sizeof(ack));
        WiFi.macAddress(ack.mac);
        ack.command = smarthome::CMD_ACK_REPORT;
        ack.status  = 1;
        addPeer(const_cast<uint8_t*>(mac));
        esp_err_t res = esp_now_send(mac, reinterpret_cast<uint8_t*>(&ack), sizeof(ack));
        if (res != ESP_OK) {
            Serial.printf("[ACK TX] FAIL -> %02X:%02X:%02X:%02X:%02X:%02X err=%d ch=%u\n",
                          mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
                          static_cast<int>(res), WiFi.channel());
        }
    }

    bool sendPacket(const uint8_t* mac, EspNowPacket& pkt) {
        if (!addPeer(const_cast<uint8_t*>(mac))) return false;
        return esp_now_send(mac, reinterpret_cast<uint8_t*>(&pkt), sizeof(pkt)) == ESP_OK;
    }

    bool sendControl(const uint8_t* mac, uint8_t nodeId, uint8_t cmd) {
        EspNowPacket p;
        memset(&p, 0, sizeof(p));
        WiFi.macAddress(p.mac);
        p.node_id = nodeId;
        p.command = cmd;
        // status mirror lệnh — node Hybrid/Relay đọc command; status dự phòng
        p.status  = (cmd == smarthome::CMD_RELAY_ON) ? 1 : 0;
        // Gửi 2 lần (gap ngắn): ESP8266 đôi khi miss unicast 1 frame; ON/OFF tuyệt đối → an toàn
        bool ok = sendPacket(mac, p);
        delay(15);
        ok = sendPacket(mac, p) || ok;
        Serial.printf("[CTRL TX] cmd=0x%02X node=%u -> %02X:%02X:%02X:%02X:%02X:%02X ok=%d ch=%u\n",
                      cmd, nodeId,
                      mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
                      ok ? 1 : 0, WiFi.channel());
        return ok;
    }

    bool sendIr(const uint8_t* mac, uint8_t nodeId, uint8_t cmd, uint32_t irData) {
        EspNowPacket p;
        memset(&p, 0, sizeof(p));
        WiFi.macAddress(p.mac);
        p.node_id     = nodeId;
        p.device_type = smarthome::DEVICE_IR;
        p.command     = cmd;
        p.ir_data     = irData;
        return sendPacket(mac, p);
    }

private:
    /** Tìm node trong Preferences theo MAC; nạp lại RAM registry nếu thiếu. */
    bool ensureRegistered(const uint8_t* mac, uint8_t packetDeviceType) {
        if (!registry_) return false;
        if (registry_->isRegisteredMac(mac)) return true;

        if (!storage_) return false;

        for (int i = 1; i <= GW_MAX_NODES; ++i) {
            String macKey = "mac_" + String(i);
            if (!storage_->isKey(macKey.c_str())) continue;

            uint8_t saved[6];
            storage_->getBytes(macKey.c_str(), saved, 6);
            if (memcmp(saved, mac, 6) != 0) continue;

            uint8_t typeVal = static_cast<uint8_t>(
                storage_->getUInt(("type_" + String(i)).c_str(), 1));
            if (packetDeviceType != 0) {
                typeVal = packetDeviceType;
            }
            registry_->registerSlot(i, mac, typeVal, true);
            Serial.printf("[ESP-NOW] Rehydrate registry: ID=%d type=%u từ Preferences\n",
                          i, typeVal);
            return true;
        }
        return false;
    }

    void enqueuePacket(const EspNowPacket& packet) {
        if (!rxQueue_) return;

        // Callback ESP-NOW = WiFi task, không phải ISR → xQueueSend
#if defined(ESP_PLATFORM)
        if (xPortInIsrContext()) {
            BaseType_t hpw = pdFALSE;
            xQueueSendFromISR(rxQueue_, &packet, &hpw);
            if (hpw) portYIELD_FROM_ISR();
            return;
        }
#endif
        if (xQueueSend(rxQueue_, &packet, 0) != pdTRUE) {
            Serial.println("[ESP-NOW] CẢNH BÁO: rxQueue đầy, mất gói (UI vẫn cập nhật realtime nếu registered)");
        }
    }

    void handleRecv(const uint8_t* mac_addr, const uint8_t* incomingData, int len) {
        // Chấp nhận len == sizeof; nếu lớn hơn một chút vẫn lấy sizeof (padding)
        if (len < static_cast<int>(sizeof(EspNowPacket))) {
            Serial.printf("[ESP-NOW] Sai size: %d (expect >= %d) — gói bị bỏ!\n",
                          len, static_cast<int>(sizeof(EspNowPacket)));
            return;
        }
        if (len != static_cast<int>(sizeof(EspNowPacket))) {
            Serial.printf("[ESP-NOW] Size khác kỳ vọng: %d vs %d (vẫn parse %d bytes)\n",
                          len, static_cast<int>(sizeof(EspNowPacket)),
                          static_cast<int>(sizeof(EspNowPacket)));
        }

        EspNowPacket packet;
        memcpy(&packet, incomingData, sizeof(packet));
        // Luôn dùng MAC nguồn radio (chuẩn xác hơn MAC trong payload node)
        memcpy(packet.mac, mac_addr, 6);

        // Đảm bảo có peer để ACK (không del ở đây — tránh race với Task_ServerSync)
        if (!esp_now_is_peer_exist(mac_addr)) {
            addPeer(const_cast<uint8_t*>(mac_addr));
        }

        // Log sensor/report để debug Hybrid
        if (packet.command == smarthome::CMD_ACK_REPORT
            || packet.command == smarthome::CMD_DISCOVERY) {
            Serial.printf("[GATEWAY RX] cmd=0x%02X type=%u T=%.1f H=%.1f st=%u MAC=%02X:%02X:%02X:%02X:%02X:%02X ch=%u\n",
                          packet.command, packet.device_type,
                          packet.temperature, packet.humidity, packet.status,
                          mac_addr[0], mac_addr[1], mac_addr[2],
                          mac_addr[3], mac_addr[4], mac_addr[5],
                          WiFi.channel());
        } else if (packet.command == smarthome::CMD_IR_DATA) {
            Serial.printf("[GATEWAY RX] IR MAC %02X:%02X:%02X:%02X:%02X:%02X | 0x%08X\n",
                          mac_addr[0], mac_addr[1], mac_addr[2],
                          mac_addr[3], mac_addr[4], mac_addr[5],
                          static_cast<unsigned>(packet.ir_data));
        }

        bool isRegistered = ensureRegistered(mac_addr, packet.device_type);

        if (!isRegistered && packet.command != smarthome::CMD_IR_DATA) {
            if (registry_) {
                registry_->setUnknownDiscovery(mac_addr, packet.device_type);
            }
            packet.command = smarthome::CMD_DISCOVERY;
            Serial.println("[ESP-NOW] Thiết bị lạ → Discovery");
        } else if (registry_) {
            // Realtime update (không chờ Task HTTP)
            registry_->updateFromPacket(packet);
        }

        enqueuePacket(packet);
    }

    static void onSentThunk(const uint8_t* /*mac*/, esp_now_send_status_t /*st*/) {}

#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
    static void onRecvThunk_v3(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
        if (instance_ && info) instance_->handleRecv(info->src_addr, data, len);
    }
#else
    static void onRecvThunk_v2(const uint8_t* mac, const uint8_t* data, int len) {
        if (instance_) instance_->handleRecv(mac, data, len);
    }
#endif

    static EspNowGateway* instance_;
    NodeRegistry* registry_;
    StorageManager* storage_;
    QueueHandle_t rxQueue_;
};
