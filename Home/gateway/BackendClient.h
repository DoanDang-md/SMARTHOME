/**
 * @file BackendClient.h
 * @brief HTTP client: sensors, discover, register, delete, pull sync.
 * Base URL lấy từ Preferences (backend_url) — cấu hình lúc setup WiFi.
 */
#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "GatewayTypes.h"
#include "NodeRegistry.h"
#include "StorageManager.h"
#include "EspNowGateway.h"
#include "EspNowConfig.h"

// Mặc định khi chưa cấu hình trong flash
#ifndef BACKEND_BASE_URL
#define BACKEND_BASE_URL "http://192.168.1.218:8000"
#endif

class BackendClient {
public:
    BackendClient() { setBaseUrl(String(BACKEND_BASE_URL)); }

    void attach(NodeRegistry* reg, StorageManager* storage, EspNowGateway* espnow) {
        registry_ = reg;
        storage_  = storage;
        espnow_   = espnow;
        reloadFromStorage();
    }

    /** Chuẩn hóa: bỏ / cuối, thêm http:// nếu thiếu. */
    static String normalizeBaseUrl(String base) {
        base.trim();
        while (base.endsWith("/")) {
            base.remove(base.length() - 1);
        }
        if (base.length() == 0) {
            return String(BACKEND_BASE_URL);
        }
        if (!base.startsWith("http://") && !base.startsWith("https://")) {
            base = "http://" + base;
        }
        return base;
    }

    void setBaseUrl(const String& base) {
        baseUrl_ = normalizeBaseUrl(base);
        urlSensors_  = baseUrl_ + "/api/sensors";
        urlRegister_ = baseUrl_ + "/api/devices/register_from_gateway";
        urlDiscover_ = baseUrl_ + "/api/devices/discover";
        urlSync_     = baseUrl_ + "/api/gateway/sync";
        urlDelete_   = baseUrl_ + "/api/devices/delete_from_gateway";
        Serial.printf("[BE] Base URL = %s\n", baseUrl_.c_str());
    }

    void reloadFromStorage() {
        if (!storage_) {
            setBaseUrl(String(BACKEND_BASE_URL));
            return;
        }
        String saved = storage_->getString("backend_url", "");
        if (saved.length() == 0) {
            setBaseUrl(String(BACKEND_BASE_URL));
        } else {
            setBaseUrl(saved);
        }
    }

    const String& baseUrl() const { return baseUrl_; }

    bool postSensor(const cached_record_t& record) {
        if (WiFi.status() != WL_CONNECTED) return false;
        HTTPClient http;
        http.setTimeout(2500);
        http.begin(urlSensors_);
        http.addHeader("Content-Type", "application/json");

        StaticJsonDocument<384> doc;
        char macStr[18];
        snprintf(macStr, sizeof(macStr), "%02x:%02x:%02x:%02x:%02x:%02x",
                 record.packet.mac[0], record.packet.mac[1], record.packet.mac[2],
                 record.packet.mac[3], record.packet.mac[4], record.packet.mac[5]);

        int realId = registry_ ? registry_->getRealNodeId(record.packet.mac) : 0;
        doc["mac_address"]       = macStr;
        doc["node_id"]           = (realId != 0) ? realId : record.packet.node_id;
        doc["temperature"]       = record.packet.temperature;
        doc["humidity"]          = record.packet.humidity;
        doc["status"]            = record.packet.status;
        doc["command"]           = record.packet.command;
        doc["ir_data"]           = record.packet.ir_data;
        doc["gw_ip"]             = WiFi.localIP().toString();
        doc["offline_timestamp"] = record.timestamp;

        String body;
        serializeJson(doc, body);
        int code = http.POST(body);
        http.end();
        return (code == 200 || code == 201);
    }

    void discoverDevice(uint8_t* mac, uint8_t type) {
        if (WiFi.status() != WL_CONNECTED) return;
        HTTPClient http;
        http.setTimeout(2500);
        http.begin(urlDiscover_);
        http.addHeader("Content-Type", "application/json");
        StaticJsonDocument<200> doc;
        char macStr[18];
        snprintf(macStr, sizeof(macStr), "%02x:%02x:%02x:%02x:%02x:%02x",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        doc["mac_address"] = macStr;
        doc["device_type"] = type;
        doc["timestamp"]   = millis();
        String body;
        serializeJson(doc, body);
        http.POST(body);
        http.end();
    }

    bool registerNode(const String& nodeId, const String& macStr,
                      const String& nameStr, uint8_t typeVal) {
        if (WiFi.status() != WL_CONNECTED) return false;
        HTTPClient http;
        http.setTimeout(2500);
        http.begin(urlRegister_);
        http.addHeader("Content-Type", "application/json");
        StaticJsonDocument<256> doc;
        doc["node_id"]     = nodeId.toInt();
        doc["mac_address"] = macStr;
        doc["type"]        = typeVal;
        doc["name"]        = nameStr;
        String payload;
        serializeJson(doc, payload);
        int code = http.POST(payload);
        http.end();
        if (code != 200 && code != 201) {
            Serial.printf("[BE REG] HTTP %d node=%s mac=%s\n", code, nodeId.c_str(), macStr.c_str());
            return false;
        }
        Serial.printf("[BE REG] OK node=%s mac=%s\n", nodeId.c_str(), macStr.c_str());
        return true;
    }

    bool deleteNode(int nodeId, const char* macStr) {
        if (WiFi.status() != WL_CONNECTED) {
            Serial.println("[BE DEL] WiFi offline — bỏ qua sync backend");
            return false;
        }
        HTTPClient http;
        http.setTimeout(2500);
        http.begin(urlDelete_);
        http.addHeader("Content-Type", "application/json");
        StaticJsonDocument<192> doc;
        doc["node_id"]   = nodeId;
        doc["device_id"] = nodeId;
        if (macStr && macStr[0]) {
            doc["mac_address"] = macStr;
        }
        String payload;
        serializeJson(doc, payload);
        int code = http.POST(payload);
        String resp = http.getString();
        http.end();
        if (code == 200 || code == 201) {
            Serial.printf("[BE DEL] OK id=%d mac=%s resp=%s\n",
                          nodeId, macStr ? macStr : "-", resp.c_str());
            return true;
        }
        Serial.printf("[BE DEL] HTTP %d id=%d mac=%s resp=%s\n",
                      code, nodeId, macStr ? macStr : "-", resp.c_str());
        return false;
    }

    void pullConfigFromServer() {
        if (WiFi.status() != WL_CONNECTED || !storage_ || !registry_ || !espnow_) return;
        reloadFromStorage();
        HTTPClient http;
        http.setTimeout(5000);
        String url = urlSync_ + "?gw_ip=" + WiFi.localIP().toString();
        http.begin(url);
        int httpCode = http.GET();
        if (httpCode == 200) {
            String payload = http.getString();
            // Tăng buffer: nodes + ir_commands (n,c) từ Backend
            DynamicJsonDocument doc(8192);
            if (deserializeJson(doc, payload) == DeserializationError::Ok) {
                JsonArray nodes = doc["nodes"].as<JsonArray>();
                bool active_ids[GW_MAX_NODES + 1] = {false};
                int irRestored = 0;
                for (JsonObject node : nodes) {
                    int nid = node["node_id"];
                    const char* macStr  = node["mac_address"];
                    const char* nameStr = node["name"];
                    int typeVal = node["type"];
                    if (nid >= 1 && nid <= GW_MAX_NODES && macStr != nullptr) {
                        active_ids[nid] = true;
                        uint8_t mac_bytes[6];
                        if (smarthome::parseMacAddress(macStr, mac_bytes)) {
                            String nidStr = String(nid);
                            storage_->putBytes(("mac_" + nidStr).c_str(), mac_bytes, 6);
                            if (nameStr) storage_->putString(("name_" + nidStr).c_str(), nameStr);
                            storage_->putUInt(("type_" + nidStr).c_str(), typeVal);
                            espnow_->addPeer(mac_bytes);
                            registry_->registerSlot(nid, mac_bytes, static_cast<uint8_t>(typeVal), true);

                            // Khôi phục lệnh IR đã học từ Backend → Preferences ircmds_*
                            // Form BE: [{"n":"Power","c":123}, ...] — trùng form Gateway UI
                            if (node["ir_commands"].is<JsonArray>()) {
                                String irJson;
                                serializeJson(node["ir_commands"], irJson);
                                storage_->putString(("ircmds_" + nidStr).c_str(), irJson);
                                irRestored += node["ir_commands"].as<JsonArray>().size();
                                Serial.printf("[STARTUP SYNC] IR node %d: %d lệnh\n",
                                              nid, (int)node["ir_commands"].as<JsonArray>().size());
                            }
                        }
                    }
                }
                for (int i = 1; i <= GW_MAX_NODES; ++i) {
                    if (active_ids[i]) continue;
                    String nidStr = String(i);
                    String macKey = "mac_" + nidStr;
                    if (!storage_->isKey(macKey.c_str())) continue;
                    uint8_t old_mac[6];
                    storage_->getBytes(macKey.c_str(), old_mac, 6);
                    espnow_->removePeer(old_mac);
                    storage_->remove(macKey.c_str());
                    storage_->remove(("name_" + nidStr).c_str());
                    storage_->remove(("ircmds_" + nidStr).c_str());
                    storage_->remove(("type_" + nidStr).c_str());
                    registry_->clearSlot(i);
                }
                Serial.printf("[STARTUP SYNC] Đồng bộ %d thiết bị, %d lệnh IR từ Backend!\n",
                              nodes.size(), irRestored);
            } else {
                Serial.println("[STARTUP SYNC] JSON parse lỗi (buffer/format)");
            }
        } else {
            Serial.printf("[STARTUP SYNC] Lỗi HTTP %d url=%s\n", httpCode, url.c_str());
        }
        http.end();
    }

private:
    String baseUrl_;
    String urlSensors_;
    String urlRegister_;
    String urlDiscover_;
    String urlSync_;
    String urlDelete_;
    NodeRegistry* registry_ = nullptr;
    StorageManager* storage_ = nullptr;
    EspNowGateway* espnow_ = nullptr;
};
