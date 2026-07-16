/**
 * @file EspNowManager.h
 * @brief Khởi tạo ESP-NOW, peer, auto channel scan, send/recv.
 *        ESP8266: <espnow.h> | ESP32: <esp_now.h>
 *
 * Callback C-API không bind method → dùng static instance pointer (thunk).
 *
 * Node Relay — đồng bộ EspNowManager từ Hybrid:
 * - Score-scan kênh 1→13 (hits; không first-hit — tránh bleed kênh kề).
 * - Uplink BROADCAST; soft recover / full rescan.
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
    static constexpr uint8_t SCAN_PROBES_PER_CH = 3;
    static constexpr unsigned long SCAN_SETTLE_MS = 80UL;
    static constexpr unsigned long SCAN_WAIT_MIN_MS = 300UL;
    static constexpr unsigned long SCAN_WAIT_MAX_MS = 900UL;

    EspNowManager()
        : handler_(nullptr),
          gatewayFound_(false),
          gatewayLocked_(false),
          packetSeq_(0),
          lastGatewayRxMs_(0),
          scanActive_(false),
          currentScanChannel_(0) {
        smarthome::EspNowConfig::copyBroadcast(gatewayMac_);
        smarthome::EspNowConfig::copyBroadcast(bcastMac_);
        clearScanScores();
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
     * Score-scan kênh 1–13: đếm ACK (hits) mỗi kênh, chọn best — không first-hit.
     * @param waitMsPerChannel tổng ms lắng nghe / kênh (clamp 300–900)
     */
    typedef void (*DiscoverySendFn)(void* ctx);

    void scanGatewayChannel(DiscoverySendFn sendDiscovery, void* ctx,
                            unsigned long waitMsPerChannel = 400) {
        Serial.println("\n[WIFI] Score-scan kênh Gateway (1–13, chọn best hits)...");
        gatewayFound_  = false;
        gatewayLocked_ = false;
        clearScanScores();
        scanActive_ = true;

        unsigned long totalWait = waitMsPerChannel;
        if (totalWait < SCAN_WAIT_MIN_MS) totalWait = SCAN_WAIT_MIN_MS;
        if (totalWait > SCAN_WAIT_MAX_MS) totalWait = SCAN_WAIT_MAX_MS;
        const unsigned long waitPerProbe =
            totalWait / static_cast<unsigned long>(SCAN_PROBES_PER_CH);
        const int passes = (smarthome::EspNowConfig::SCAN_ATTEMPTS > 2)
                               ? 2
                               : static_cast<int>(smarthome::EspNowConfig::SCAN_ATTEMPTS);

        for (int attempt = 0; attempt < passes; ++attempt) {
            for (uint8_t ch = smarthome::EspNowConfig::CHANNEL_MIN;
                 ch <= smarthome::EspNowConfig::CHANNEL_MAX; ++ch) {
                currentScanChannel_ = ch;
                config_.channel     = ch;
                setWifiChannel(ch);
                readdGatewayPeer(ch);
                delay(SCAN_SETTLE_MS);

                Serial.printf("[WIFI] Thử Kênh %u (pass %d/%d)...\r",
                              ch, attempt + 1, passes);

                for (uint8_t p = 0; p < SCAN_PROBES_PER_CH; ++p) {
                    if (sendDiscovery != nullptr) {
                        sendDiscovery(ctx);
                    }
                    const unsigned long startWait = millis();
                    while (millis() - startWait < waitPerProbe) {
                        delay(10);
                    }
                }
            }
        }

        scanActive_         = false;
        currentScanChannel_ = 0;

        for (uint8_t ch = smarthome::EspNowConfig::CHANNEL_MIN;
             ch <= smarthome::EspNowConfig::CHANNEL_MAX; ++ch) {
            if (scanHits_[ch] > 0) {
                Serial.printf("\n[WIFI]  score ch=%u hits=%u", ch, scanHits_[ch]);
            }
        }
        Serial.println();

        const uint8_t bestCh = pickBestScanChannel();
        if (bestCh != 0) {
            config_.channel = bestCh;
            setWifiChannel(bestCh);
            readdGatewayPeer(bestCh);
            gatewayFound_ = true;
            lockGatewayMac(scanMac_[bestCh]);
            Serial.printf("[WIFI] >>> CHỌN KÊNH %u (hits=%u) <<<\n",
                          bestCh, scanHits_[bestCh]);
            if (sendDiscovery != nullptr) {
                delay(SCAN_SETTLE_MS);
                sendDiscovery(ctx);
                delay(120);
            }
            return;
        }

        Serial.printf("[WIFI] Cảnh báo: Không thấy Gateway sau score-scan. Giữ kênh %u.\n",
                      config_.channel);
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

    void clearScanScores() {
        memset(scanHits_, 0, sizeof(scanHits_));
        for (uint8_t i = 0; i < 14; ++i) {
            scanBestRssi_[i] = -128;
            memset(scanMac_[i], 0xFF, 6);
        }
    }

    uint8_t pickBestScanChannel() const {
        uint8_t bestCh    = 0;
        uint16_t bestHits = 0;
        int8_t bestRssi   = -128;
        for (uint8_t ch = smarthome::EspNowConfig::CHANNEL_MIN;
             ch <= smarthome::EspNowConfig::CHANNEL_MAX; ++ch) {
            if (scanHits_[ch] == 0) continue;
            const bool betterHits = scanHits_[ch] > bestHits;
            const bool tieHitsBetterRssi =
                (scanHits_[ch] == bestHits) && (scanBestRssi_[ch] > bestRssi);
            if (betterHits || tieHitsBetterRssi) {
                bestCh   = ch;
                bestHits = scanHits_[ch];
                bestRssi = scanBestRssi_[ch];
            }
        }
        return bestCh;
    }

    void handleRecv(const uint8_t* srcMac, const uint8_t* data, int len, int8_t rssi = -128) {
        if (len != static_cast<int>(sizeof(EspNowPacket))) return;

        EspNowPacket packet;
        memcpy(&packet, data, sizeof(packet));

        if (isGatewayToNodeCommand(packet.command)) {
            if (scanActive_) {
                const uint8_t ch = currentScanChannel_;
                if (ch >= smarthome::EspNowConfig::CHANNEL_MIN
                    && ch <= smarthome::EspNowConfig::CHANNEL_MAX) {
                    scanHits_[ch]++;
                    if (rssi > scanBestRssi_[ch]) {
                        scanBestRssi_[ch] = rssi;
                    }
                    memcpy(scanMac_[ch], srcMac, 6);
                }
                lastGatewayRxMs_ = millis();
            } else {
                gatewayFound_    = true;
                lastGatewayRxMs_ = millis();
                lockGatewayMac(srcMac);
            }
        }

        if (handler_ != nullptr) {
            handler_->onEspNowPacket(srcMac, packet);
        }
    }

#if defined(ESP8266)
    static void recvThunk8266(uint8_t* mac, uint8_t* incomingData, uint8_t len) {
        if (instance_ != nullptr) {
            instance_->handleRecv(mac, incomingData, static_cast<int>(len), -128);
        }
    }
#elif defined(ESP32)
#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
    static void recvThunk32_v3(const esp_now_recv_info* info, const uint8_t* incomingData, int len) {
        if (instance_ != nullptr && info != nullptr) {
            int8_t rssi = -128;
            if (info->rx_ctrl != nullptr) {
                rssi = info->rx_ctrl->rssi;
            }
            instance_->handleRecv(info->src_addr, incomingData, len, rssi);
        }
    }
#else
    static void recvThunk32_v2(const uint8_t* mac, const uint8_t* incomingData, int len) {
        if (instance_ != nullptr) {
            instance_->handleRecv(mac, incomingData, len, -128);
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

    bool     scanActive_;
    uint8_t  currentScanChannel_;
    uint16_t scanHits_[14];
    int8_t   scanBestRssi_[14];
    uint8_t  scanMac_[14][6];
};
