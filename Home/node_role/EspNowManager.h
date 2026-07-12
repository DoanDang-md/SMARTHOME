/**
 * @file EspNowManager.h
 * @brief Khởi tạo ESP-NOW, peer, auto channel scan, send/recv.
 *        ESP8266: <espnow.h> | ESP32: <esp_now.h>
 *
 * Callback C-API không bind method → dùng static instance pointer (thunk).
 *
 * Node Relay — đồng bộ EspNowManager từ Hybrid:
 * - Auto Channel Scan 1→13; uplink BROADCAST; soft recover / full rescan.
 * - Unicast Node→GW fail trên ESP8266→ESP32; BCAST OK; GW→Node unicast OK.
 */
#pragma once

#include <Arduino.h>
#include "EspNowPacket.h"
#include "EspNowConfig.h"

#if defined(ESP8266)
  #include <ESP8266WiFi.h>
  #include <espnow.h>
#elif defined(ESP32)
  #include <WiFi.h>
  #include <esp_now.h>
  #include <esp_wifi.h>
#endif

/** Interface nhận packet — Device/App implement để xử lý lệnh. */
class IEspNowPacketHandler {
public:
    virtual ~IEspNowPacketHandler() {}
    virtual void onEspNowPacket(const uint8_t srcMac[6], const EspNowPacket& packet) = 0;
};

class EspNowManager {
public:
    EspNowManager()
        : handler_(nullptr),
          gatewayFound_(false),
          gatewayLocked_(false),
          packetSeq_(0),
          lastGatewayRxMs_(0) {
        smarthome::EspNowConfig::copyBroadcast(gatewayMac_);
        smarthome::EspNowConfig::copyBroadcast(bcastMac_);
    }

    void setHandler(IEspNowPacketHandler* handler) { handler_ = handler; }

    bool begin(uint8_t initialChannel = smarthome::EspNowConfig::CHANNEL_MIN) {
        config_.channel = initialChannel;
        instance_ = this;

        WiFi.mode(WIFI_STA);
        WiFi.disconnect();

#if defined(ESP8266)
        if (esp_now_init() != 0) {
            Serial.println("[ESPNOW] Lỗi khởi tạo ESP-NOW (ESP8266)");
            return false;
        }
        esp_now_set_self_role(ESP_NOW_ROLE_COMBO);
        esp_now_register_recv_cb(&EspNowManager::recvThunk8266);
        ensureBroadcastPeer();
#elif defined(ESP32)
        if (esp_now_init() != ESP_OK) {
            Serial.println("[ESPNOW] Lỗi khởi tạo ESP-NOW (ESP32)");
            return false;
        }
#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
        esp_now_register_recv_cb(&EspNowManager::recvThunk32_v3);
#else
        esp_now_register_recv_cb(reinterpret_cast<esp_now_recv_cb_t>(&EspNowManager::recvThunk32_v2));
#endif
        ensureBroadcastPeer();
#else
        #error "Platform không hỗ trợ ESP-NOW"
#endif
        Serial.println("[ESPNOW] Uplink = BROADCAST (ESP8266 unicast→GW không tin cậy)");
        return true;
    }

    /**
     * Gửi packet lên Gateway qua BROADCAST (ổn định trên ESP8266→ESP32).
     * gatewayMac_ (unicast) chỉ dùng để biết đã lock / log — không dùng làm dest TX.
     */
    bool sendToGateway(uint8_t nodeId, uint8_t deviceType, uint8_t command,
                       uint8_t status, float temperature = 0.0f,
                       float humidity = 0.0f, uint32_t irData = 0) {
        EspNowPacket pkt;
        memset(&pkt, 0, sizeof(pkt));
        WiFi.macAddress(pkt.mac);
        pkt.node_id     = nodeId;
        pkt.device_type = deviceType;
        pkt.command     = command;
        pkt.seq         = packetSeq_++;
        pkt.temperature = temperature;
        pkt.humidity    = humidity;
        pkt.status      = status;
        pkt.ir_data     = irData;

        prepareRadioForTx();

#if defined(ESP8266)
        int r = esp_now_send(const_cast<uint8_t*>(bcastMac_),
                             reinterpret_cast<uint8_t*>(&pkt), sizeof(pkt));
        bool ok = (r == 0);
#elif defined(ESP32)
        esp_err_t err = esp_now_send(bcastMac_, reinterpret_cast<uint8_t*>(&pkt), sizeof(pkt));
        bool ok = (err == ESP_OK);
#endif
        if (command == smarthome::CMD_ACK_REPORT || command == smarthome::CMD_DISCOVERY) {
            Serial.printf("[ESPNOW TX] cmd=0x%02X type=%u T=%.1f H=%.1f st=%u size=%u ok=%d "
                          "dest=BCAST locked_gw=%02X:%02X:%02X:%02X:%02X:%02X "
                          "ch_cfg=%u ch_hw=%u locked=%d\n",
                          command, deviceType, temperature, humidity, status,
                          static_cast<unsigned>(sizeof(pkt)), ok ? 1 : 0,
                          gatewayMac_[0], gatewayMac_[1], gatewayMac_[2],
                          gatewayMac_[3], gatewayMac_[4], gatewayMac_[5],
                          config_.channel, currentHwChannel(), gatewayLocked_ ? 1 : 0);
        } else if (!ok) {
            Serial.printf("[ESPNOW TX] Gửi lỗi cmd=0x%02X\n", command);
        }
        return ok;
    }

    /**
     * Auto channel scanning: gửi Discovery 0x00 trên kênh 1–13.
     * @param waitMsPerChannel thời gian chờ phản hồi mỗi kênh
     * @param buildAndSend callback gửi discovery (App cung cấp nodeId/type/status)
     */
    typedef void (*DiscoverySendFn)(void* ctx);

    void scanGatewayChannel(DiscoverySendFn sendDiscovery, void* ctx,
                            unsigned long waitMsPerChannel = 400) {
        Serial.println("\n[WIFI] Đang dò tìm kênh WiFi của Gateway (Auto Channel Scanning)...");
        gatewayFound_ = false;

        for (int attempt = 0; attempt < smarthome::EspNowConfig::SCAN_ATTEMPTS; ++attempt) {
            for (uint8_t ch = smarthome::EspNowConfig::CHANNEL_MIN;
                 ch <= smarthome::EspNowConfig::CHANNEL_MAX; ++ch) {
                config_.channel = ch;
                setWifiChannel(ch);
                readdGatewayPeer(ch);

                Serial.printf("[WIFI] Thử Kênh %d...\r", ch);
                if (sendDiscovery != nullptr) {
                    sendDiscovery(ctx);
                }

                unsigned long startWait = millis();
                while (millis() - startWait < waitMsPerChannel) {
                    delay(20);  // Chỉ trong phase scan (setup), không dùng trong loop chính
                    if (gatewayFound_) {
                        Serial.printf("\n[WIFI] >>> ĐÃ TÌM THẤY GATEWAY TẠI KÊNH SỐ %d! <<<\n", ch);
                        return;
                    }
                }
            }
        }
        Serial.printf("\n[WIFI] Cảnh báo: Không thấy Gateway sau %d vòng. Giữ kênh %d.\n",
                      smarthome::EspNowConfig::SCAN_ATTEMPTS, config_.channel);
    }

    bool gatewayFound() const { return gatewayFound_; }
    bool gatewayLocked() const { return gatewayLocked_; }
    uint8_t channel() const { return config_.channel; }
    const uint8_t* gatewayMac() const { return gatewayMac_; }
    unsigned long lastGatewayRxMs() const { return lastGatewayRxMs_; }

    void printMac() const {
        Serial.print("[MAC] ");
        Serial.println(WiFi.macAddress());
    }

    /**
     * Mở khóa peer, về Broadcast, quét lại kênh 1–13.
     * Dùng khi node bị kẹt sai kênh (VD: node ch2, Gateway WiFi ch1).
     */
    void rescanGatewayChannel(DiscoverySendFn sendDiscovery, void* ctx,
                              unsigned long waitMsPerChannel = 400) {
        Serial.println("\n[WIFI] === RESCAN kênh Gateway (có thể do lệch channel) ===");
        unlockGatewayToBroadcast();
        scanGatewayChannel(sendDiscovery, ctx, waitMsPerChannel);
        Serial.printf("[WIFI] Sau rescan: channel=%u locked=%d\n",
                      config_.channel, gatewayLocked_ ? 1 : 0);
    }

    /**
     * Soft recover: giữ kênh, refresh peer broadcast, gửi Discovery (uplink vẫn BCAST).
     */
    void softRecoverLink(DiscoverySendFn sendDiscovery, void* ctx) {
        Serial.printf("\n[WIFI] Soft recover (ch=%u) — Discovery BCAST\n", config_.channel);
        prepareRadioForTx();
        if (sendDiscovery != nullptr) {
            sendDiscovery(ctx);
        }
    }

    uint8_t currentHwChannel() const {
#if defined(ESP8266)
        return static_cast<uint8_t>(wifi_get_channel());
#elif defined(ESP32)
        uint8_t primary = 0;
        wifi_second_chan_t second = WIFI_SECOND_CHAN_NONE;
        if (esp_wifi_get_channel(&primary, &second) == ESP_OK) return primary;
        return config_.channel;
#else
        return config_.channel;
#endif
    }

private:
    void prepareRadioForTx() {
        setWifiChannel(config_.channel);
        ensureBroadcastPeer();
    }

    void unlockGatewayToBroadcast() {
        // Chỉ quên MAC GW đã lock; peer broadcast luôn giữ
        if (!smarthome::EspNowConfig::isBroadcast(gatewayMac_)) {
            esp_now_del_peer(gatewayMac_);
        }
        smarthome::EspNowConfig::copyBroadcast(gatewayMac_);
        gatewayLocked_ = false;
        gatewayFound_  = false;
        config_.channel = smarthome::EspNowConfig::CHANNEL_MIN;
        setWifiChannel(config_.channel);
        ensureBroadcastPeer();
    }

    /** Gói từ Gateway → Node (không tính gói node-to-node / nhiễu). */
    static bool isGatewayToNodeCommand(uint8_t cmd) {
        using namespace smarthome;
        return cmd == CMD_ACK_REPORT
            || cmd == CMD_RELAY_ON
            || cmd == CMD_RELAY_OFF
            || cmd == CMD_IR_DATA
            || cmd == CMD_IR_LEARN
            || cmd == CMD_IR_SAVE;
    }
    void setWifiChannel(uint8_t ch) {
#if defined(ESP8266)
        wifi_set_channel(ch);
#elif defined(ESP32)
        esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
#endif
    }

    void addPeerInternal(const uint8_t mac[6], uint8_t channel) {
#if defined(ESP8266)
        esp_now_add_peer(const_cast<uint8_t*>(mac), ESP_NOW_ROLE_COMBO, channel, NULL, 0);
#elif defined(ESP32)
        if (esp_now_is_peer_exist(mac)) return;
        esp_now_peer_info_t peerInfo;
        memset(&peerInfo, 0, sizeof(peerInfo));
        memcpy(peerInfo.peer_addr, mac, 6);
        peerInfo.channel = channel;
        peerInfo.ifidx   = WIFI_IF_STA;
        peerInfo.encrypt = false;
        esp_now_add_peer(&peerInfo);
#endif
    }

    /** Trong phase scan: luôn peer broadcast trên kênh đang thử. */
    void readdGatewayPeer(uint8_t channel) {
        ensureBroadcastPeerOnChannel(channel);
    }

    void ensureBroadcastPeer() {
        ensureBroadcastPeerOnChannel(config_.channel);
    }

    void ensureBroadcastPeerOnChannel(uint8_t channel) {
#if defined(ESP8266)
        esp_now_del_peer(const_cast<uint8_t*>(bcastMac_));
        esp_now_add_peer(const_cast<uint8_t*>(bcastMac_), ESP_NOW_ROLE_COMBO, channel, NULL, 0);
#elif defined(ESP32)
        if (esp_now_is_peer_exist(bcastMac_)) {
            esp_now_del_peer(bcastMac_);
        }
        addPeerInternal(bcastMac_, channel);
#endif
    }

    /**
     * Ghi nhớ MAC Gateway từ ACK (GW→Node unicast OK).
     * KHÔNG xóa peer broadcast — uplink vẫn cần BCAST.
     */
    void lockGatewayMac(const uint8_t* gwMac) {
        if (gwMac == nullptr) return;

        if (gatewayLocked_ && memcmp(gatewayMac_, gwMac, 6) == 0) {
            setWifiChannel(config_.channel);
            ensureBroadcastPeer();
            return;
        }

        memcpy(gatewayMac_, gwMac, 6);
        setWifiChannel(config_.channel);
        ensureBroadcastPeer();
        gatewayLocked_ = true;

        Serial.printf("[ESPNOW] Lock GW MAC (RX): %02X:%02X:%02X:%02X:%02X:%02X ch=%d | TX vẫn BCAST\n",
                      gwMac[0], gwMac[1], gwMac[2], gwMac[3], gwMac[4], gwMac[5], config_.channel);
    }

    void handleRecv(const uint8_t* srcMac, const uint8_t* data, int len) {
        if (len != static_cast<int>(sizeof(EspNowPacket))) return;

        EspNowPacket packet;
        memcpy(&packet, data, sizeof(packet));

        // Chỉ coi là "tìm thấy Gateway" khi nhận lệnh chiều GW→Node (tránh khóa nhầm kênh)
        if (isGatewayToNodeCommand(packet.command)) {
            gatewayFound_ = true;
            lastGatewayRxMs_ = millis();
            lockGatewayMac(srcMac);
        }

        if (handler_ != nullptr) {
            handler_->onEspNowPacket(srcMac, packet);
        }
    }

    // ---- Platform-specific thunks (static) ----
#if defined(ESP8266)
    static void recvThunk8266(uint8_t* mac, uint8_t* incomingData, uint8_t len) {
        if (instance_ != nullptr) {
            instance_->handleRecv(mac, incomingData, static_cast<int>(len));
        }
    }
#elif defined(ESP32)
#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
    static void recvThunk32_v3(const esp_now_recv_info* info, const uint8_t* incomingData, int len) {
        if (instance_ != nullptr && info != nullptr) {
            instance_->handleRecv(info->src_addr, incomingData, len);
        }
    }
#else
    static void recvThunk32_v2(const uint8_t* mac, const uint8_t* incomingData, int len) {
        if (instance_ != nullptr) {
            instance_->handleRecv(mac, incomingData, len);
        }
    }
#endif
#endif

    static EspNowManager* instance_;  // defined in EspNowManager.cpp

    IEspNowPacketHandler* handler_;
    smarthome::EspNowConfig config_;
    uint8_t gatewayMac_[6];   // MAC Gateway đã biết (từ ACK) — không dùng làm dest TX
    uint8_t bcastMac_[6];     // FF:FF:FF:FF:FF:FF — dest uplink
    bool gatewayFound_;
    bool gatewayLocked_;
    uint16_t packetSeq_;
    unsigned long lastGatewayRxMs_;
};
