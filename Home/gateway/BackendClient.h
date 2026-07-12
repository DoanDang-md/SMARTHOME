/**
 * @file BackendClient.h
 * @brief HTTP client: sensors, discover, register, pull sync.
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

class BackendClient {
public:
    BackendClient()
        : urlSensors_("http://192.168.1.218:8000/api/sensors"),
          urlRegister_("http://192.168.1.218:8000/api/devices/register"),
          urlDiscover_("http://192.168.1.218:8000/api/devices/discover"),
          urlSync_("http://192.168.1.218:8000/api/gateway/sync") {}

    void attach(NodeRegistry* reg, StorageManager* storage, EspNowGateway* espnow) {
        registry_ = reg;
        storage_  = storage;
        espnow_   = espnow;
    }

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
        StaticJsonDocument<200> doc;
        doc["node_id"]     = nodeId.toInt();
        doc["mac_address"] = macStr;
        doc["type"]        = typeVal;
        doc["name"]        = nameStr;
        String payload;
        serializeJson(doc, payload);
        int code = http.POST(payload);
        http.end();
        return (code == 200 || code == 201);
    }

    void pullConfigFromServer() {
        if (WiFi.status() != WL_CONNECTED || !storage_ || !registry_ || !espnow_) return;
        HTTPClient http;
        http.setTimeout(3000);
        String url = String(urlSync_) + "?gw_ip=" + WiFi.localIP().toString();
        http.begin(url);
        int httpCode = http.GET();
        if (httpCode == 200) {
            String payload = http.getString();
            StaticJsonDocument<2048> doc;
            if (deserializeJson(doc, payload) == DeserializationError::Ok) {
                JsonArray nodes = doc["nodes"].as<JsonArray>();
                bool active_ids[GW_MAX_NODES + 1] = {false};
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
                Serial.printf("[STARTUP SYNC] Đồng bộ %d thiết bị từ Backend!\n", nodes.size());
            }
        } else {
            Serial.printf("[STARTUP SYNC] Lỗi HTTP %d\n", httpCode);
        }
        http.end();
    }

private:
    const char* urlSensors_;
    const char* urlRegister_;
    const char* urlDiscover_;
    const char* urlSync_;
    NodeRegistry* registry_ = nullptr;
    StorageManager* storage_ = nullptr;
    EspNowGateway* espnow_ = nullptr;
};
