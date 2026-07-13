/**
 * @file GatewayWebServer.h
 * @brief WebServer routes: dashboard, control, IR, discovery, WiFi setup.
 */
#pragma once

#include <Arduino.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include "GatewayHtml.h"
#include "StorageManager.h"
#include "NodeRegistry.h"
#include "EspNowGateway.h"
#include "BackendClient.h"
#include "WifiProvisioner.h"
#include "EspNowConfig.h"
#include "GatewayTypes.h"

class GatewayWebServer {
public:
    GatewayWebServer()
        : server_(80),
          storage_(nullptr),
          registry_(nullptr),
          espnow_(nullptr),
          backend_(nullptr),
          wifi_(nullptr) {}

    void attach(StorageManager* s, NodeRegistry* r, EspNowGateway* e,
                BackendClient* b, WifiProvisioner* w) {
        storage_  = s;
        registry_ = r;
        espnow_   = e;
        backend_  = b;
        wifi_     = w;
    }

    void beginDashboard() {
        instance_ = this;
        server_.on("/", HTTP_GET, []() { instance_->handleRoot(); });
        server_.on("/save", HTTP_POST, []() { instance_->handleSaveNode(); });
        server_.on("/api/discovered_mac", HTTP_GET, []() { instance_->handleDiscoveredMac(); });
        server_.on("/api/add_peer", HTTP_POST, []() { instance_->handleApiAddPeer(); });
        server_.on("/api/control", HTTP_POST, []() { instance_->handleApiControl(); });
        server_.on("/api/ir/send", HTTP_POST, []() { instance_->handleApiIrSend(); });
        server_.on("/api/ir/save", HTTP_POST, []() { instance_->handleApiIrSave(); });
        server_.on("/api/ir/delete", HTTP_POST, []() { instance_->handleApiIrDelete(); });
        server_.on("/api/ir/learn", HTTP_POST, []() { instance_->handleApiIrLearn(); });
        server_.on("/reset_wifi", HTTP_GET, []() { instance_->handleResetWifi(); });
        server_.on("/api/nodes", HTTP_GET, []() { instance_->handleGetNodes(); });
        server_.on("/api/delete", HTTP_GET, []() { instance_->handleDeleteNode(); });
        server_.begin();
    }

    void beginSetupAp() {
        instance_ = this;
        server_.on("/", HTTP_GET, []() { instance_->handleWifiSetup(); });
        server_.on("/save_wifi", HTTP_POST, []() { instance_->handleSaveWifi(); });
        server_.on("/api/wifi_scan", HTTP_GET, []() { instance_->handleWifiScan(); });
        server_.on("/api/setup_info", HTTP_GET, []() { instance_->handleSetupInfo(); });
        server_.begin();
    }

    void handleClient() { server_.handleClient(); }

private:
    static GatewayWebServer* instance_;

    WebServer server_;
    StorageManager* storage_;
    NodeRegistry* registry_;
    EspNowGateway* espnow_;
    BackendClient* backend_;
    WifiProvisioner* wifi_;

    static constexpr const char* kHtmlUtf8 = "text/html; charset=utf-8";
    static constexpr const char* kJsonUtf8 = "application/json; charset=utf-8";

    void sendHtml(int code, const char* html) {
        server_.send(code, kHtmlUtf8, html);
    }

    void sendHtml(int code, const String& html) {
        server_.send(code, kHtmlUtf8, html);
    }

    void handleWifiSetup() {
        sendHtml(200, wifi_setup_html);
    }

    /** Trả backend_url đã lưu (prefill form setup). */
    void handleSetupInfo() {
        String be = BACKEND_BASE_URL;
        if (storage_) {
            String saved = storage_->getString("backend_url", "");
            if (saved.length() > 0) be = saved;
        }
        be.replace("\\", "\\\\");
        be.replace("\"", "\\\"");
        server_.sendHeader("Cache-Control", "no-store");
        server_.send(200, kJsonUtf8, "{\"backend_url\":\"" + be + "\"}");
    }

    /**
     * Quét WiFi (cần WIFI_AP_STA). JSON:
     * [{ssid,rssi,secure}, ...] — gộp SSID trùng (giữ RSSI mạnh nhất).
     */
    void handleWifiScan() {
        Serial.println("[WiFi] Scanning networks...");
        // async=false, show_hidden=true
        int n = WiFi.scanNetworks(false, true);
        if (n < 0) {
            Serial.printf("[WiFi] scan failed code=%d\n", n);
            server_.send(200, kJsonUtf8, "[]");
            return;
        }

        // Gộp SSID trùng — giữ RSSI tốt nhất
        static const int kMax = 40;
        String ssids[kMax];
        int rssis[kMax];
        bool secs[kMax];
        int count = 0;

        for (int i = 0; i < n && count < kMax; ++i) {
            String ssid = WiFi.SSID(i);
            if (ssid.length() == 0) continue;
            int rssi = WiFi.RSSI(i);
            bool secure = (WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
            int found = -1;
            for (int j = 0; j < count; ++j) {
                if (ssids[j] == ssid) {
                    found = j;
                    break;
                }
            }
            if (found >= 0) {
                if (rssi > rssis[found]) {
                    rssis[found] = rssi;
                    secs[found] = secure;
                }
            } else {
                ssids[count] = ssid;
                rssis[count] = rssi;
                secs[count] = secure;
                count++;
            }
        }
        WiFi.scanDelete();

        // Sắp xếp RSSI giảm dần (bubble, n nhỏ)
        for (int i = 0; i < count; ++i) {
            for (int j = i + 1; j < count; ++j) {
                if (rssis[j] > rssis[i]) {
                    int tr = rssis[i]; rssis[i] = rssis[j]; rssis[j] = tr;
                    bool ts = secs[i]; secs[i] = secs[j]; secs[j] = ts;
                    String tn = ssids[i]; ssids[i] = ssids[j]; ssids[j] = tn;
                }
            }
        }

        String json = "[";
        for (int i = 0; i < count; ++i) {
            if (i) json += ",";
            String s = ssids[i];
            s.replace("\\", "\\\\");
            s.replace("\"", "\\\"");
            json += "{\"ssid\":\"";
            json += s;
            json += "\",\"rssi\":";
            json += String(rssis[i]);
            json += ",\"secure\":";
            json += secs[i] ? "true" : "false";
            json += "}";
        }
        json += "]";
        Serial.printf("[WiFi] Scan done: %d unique SSIDs\n", count);
        server_.sendHeader("Cache-Control", "no-store");
        server_.send(200, kJsonUtf8, json);
    }

    void handleSaveWifi() {
        if (!server_.hasArg("ssid") || !wifi_ || !storage_) {
            sendHtml(400,
                "<!DOCTYPE html><html lang=\"vi\"><head><meta charset=\"UTF-8\"></head>"
                "<body style=\"font-family:sans-serif;padding:24px;background:#0f172a;color:#e2e8f0\">"
                "<h2>Thiếu SSID WiFi.</h2><a href=\"/\" style=\"color:#22d3ee\">Quay lại</a></body></html>");
            return;
        }
        String ssid = server_.arg("ssid");
        ssid.trim();
        String pass = server_.hasArg("password") ? server_.arg("password") : "";
        String backend = server_.hasArg("backend_url") ? server_.arg("backend_url") : "";
        backend.trim();

        if (ssid.length() == 0) {
            sendHtml(400,
                "<!DOCTYPE html><html lang=\"vi\"><head><meta charset=\"UTF-8\"></head>"
                "<body style=\"font-family:sans-serif;padding:24px;background:#0f172a;color:#e2e8f0\">"
                "<h2>SSID không được trống.</h2><a href=\"/\" style=\"color:#22d3ee\">Quay lại</a></body></html>");
            return;
        }
        if (backend.length() == 0) {
            sendHtml(400,
                "<!DOCTYPE html><html lang=\"vi\"><head><meta charset=\"UTF-8\"></head>"
                "<body style=\"font-family:sans-serif;padding:24px;background:#0f172a;color:#e2e8f0\">"
                "<h2>Cần địa chỉ Backend (vd http://192.168.1.50:8000).</h2>"
                "<a href=\"/\" style=\"color:#22d3ee\">Quay lại</a></body></html>");
            return;
        }

        wifi_->saveCredentials(ssid, pass);
        wifi_->saveBackendUrl(backend);
        if (backend_) {
            backend_->setBaseUrl(backend);
        }

        Serial.printf("[SETUP] WiFi SSID=%s | Backend=%s\n", ssid.c_str(), backend.c_str());

        String okHtml =
            String("<!DOCTYPE html><html lang=\"vi\"><head><meta charset=\"UTF-8\">"
                   "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
                   "<title>Đã lưu</title>"
                   "<style>body{font-family:sans-serif;padding:28px;background:#0f172a;color:#e2e8f0}"
                   "h2{color:#22c55e}.m{color:#94a3b8;line-height:1.5}</style></head><body>"
                   "<h2>Đã lưu cấu hình!</h2>"
                   "<p class=\"m\">WiFi: <b>")
            + ssid + "</b><br>Backend: <b>" + BackendClient::normalizeBaseUrl(backend)
            + "</b></p>"
            + "<p class=\"m\">Gateway đang khởi động lại và kết nối mạng nhà bạn…</p>"
            + "</body></html>";
        sendHtml(200, okHtml);
        delay(1500);
        ESP.restart();
    }

    void handleResetWifi() {
        if (wifi_) wifi_->clearCredentials();
        sendHtml(200,
            "<!DOCTYPE html><html lang=\"vi\"><head><meta charset=\"UTF-8\">"
            "<title>Da xoa</title></head><body style=\"font-family:sans-serif;padding:24px\">"
            "<h2>Đã xóa WiFi. Gateway sẽ vào chế độ cài đặt.</h2></body></html>");
        delay(2000);
        ESP.restart();
    }

    void handleRoot() {
        sendHtml(200, index_html);
    }

    void handleDiscoveredMac() {
        char macStr[18];
        const uint8_t* m = registry_ ? registry_->unknownMac() : nullptr;
        uint8_t t = registry_ ? registry_->unknownType() : 0;
        if (m) {
            snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
                     m[0], m[1], m[2], m[3], m[4], m[5]);
        } else {
            snprintf(macStr, sizeof(macStr), "00:00:00:00:00:00");
        }
        String jsonResp = "{\"mac\":\"" + String(macStr) + "\",\"type\":" + String(t) + "}";
        server_.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
        server_.sendHeader("Pragma", "no-cache");
        server_.sendHeader("Expires", "-1");
        server_.send(200, kJsonUtf8, jsonResp);
    }

    /** MAC đã tồn tại trong Preferences? trả về node_id hoặc -1 */
    int findStoredMacId(const uint8_t mac[6]) {
        if (!storage_) return -1;
        for (int i = 1; i <= GW_MAX_NODES; ++i) {
            String macKey = "mac_" + String(i);
            if (!storage_->isKey(macKey.c_str())) continue;
            uint8_t saved[6];
            storage_->getBytes(macKey.c_str(), saved, 6);
            if (memcmp(saved, mac, 6) == 0) return i;
        }
        return -1;
    }

    void handleSaveNode() {
        if (!server_.hasArg("name") || !server_.hasArg("mac") || !storage_ || !registry_ || !espnow_) {
            return;
        }
        String nameStr = server_.arg("name");
        String macStr  = server_.arg("mac");
        macStr.toUpperCase();
        macStr.trim();
        uint8_t typeVal = server_.hasArg("type")
                              ? static_cast<uint8_t>(server_.arg("type").toInt())
                              : (registry_ ? registry_->unknownType() : 1);
        if (typeVal == 0) typeVal = 1;
        if (nameStr.length() == 0) nameStr = "Thiết bị";

        uint8_t mac_bytes[6];
        if (!smarthome::parseMacAddress(macStr.c_str(), mac_bytes)) {
            sendHtml(400,
                "<!DOCTYPE html><html lang=\"vi\"><head><meta charset=\"UTF-8\"></head>"
                "<body style=\"font-family:sans-serif;padding:24px\">"
                "<h2>MAC không hợp lệ.</h2><a href=\"/\">Quay lại</a></body></html>");
            return;
        }

        // Chặn MAC trùng (double-click / bấm nhiều lần)
        int existingId = findStoredMacId(mac_bytes);
        if (existingId > 0) {
            Serial.printf("[SAVE] MAC trùng → ID=%d (bỏ qua tạo mới)\n", existingId);
            String msg =
                String("<!DOCTYPE html><html lang=\"vi\"><head><meta charset=\"UTF-8\"></head>"
                       "<body style=\"font-family:sans-serif;padding:24px\">"
                       "<h2>MAC đã có sẵn (ID=")
                + String(existingId) + ").</h2>"
                + "<p>Không tạo thêm thiết bị trùng.</p>"
                + "<a href=\"/\">Quay lại bảng điều khiển</a></body></html>";
            sendHtml(200, msg);
            registry_->clearUnknown();
            return;
        }

        int newId = registry_->findNextFreeId(*storage_);
        if (newId == -1) {
            sendHtml(507,
                "<!DOCTYPE html><html lang=\"vi\"><head><meta charset=\"UTF-8\"></head>"
                "<body style=\"font-family:sans-serif;padding:24px\">"
                "<h2>Đã đầy 20 thiết bị! Vui lòng xóa bớt.</h2>"
                "<a href=\"/\">Quay lại</a></body></html>");
            return;
        }
        String nodeIdStr = String(newId);
        storage_->putBytes(("mac_" + nodeIdStr).c_str(), mac_bytes, 6);
        storage_->putString(("name_" + nodeIdStr).c_str(), nameStr);
        storage_->putUInt(("type_" + nodeIdStr).c_str(), typeVal);
        espnow_->addPeer(mac_bytes);
        registry_->registerSlot(newId, mac_bytes, typeVal, true);

        if (backend_) backend_->registerNode(nodeIdStr, macStr, nameStr, typeVal);
        registry_->clearUnknown();

        Serial.printf("[SAVE] Node ID=%d Name='%s' MAC=%s\n",
                      newId, nameStr.c_str(), macStr.c_str());
        String successMsg =
            String("<!DOCTYPE html><html lang=\"vi\"><head><meta charset=\"UTF-8\"></head>"
                   "<body style=\"font-family:sans-serif;padding:24px\"><h2>Đã thêm «")
            + nameStr + "» (ID=" + nodeIdStr + ") thành công!</h2>"
            + "<a href=\"/\">Quay lại bảng điều khiển</a></body></html>";
        sendHtml(200, successMsg);
    }

    void handleApiAddPeer() {
        if (server_.method() != HTTP_POST || !storage_ || !registry_ || !espnow_) return;
        String body = server_.arg("plain");
        StaticJsonDocument<200> doc;
        deserializeJson(doc, body);

        String nameStr = doc["name"].as<String>();
        String macStr  = doc["mac"].as<String>();
        uint8_t mac_bytes[6];
        if (!smarthome::parseMacAddress(macStr.c_str(), mac_bytes)) {
            server_.send(400, kJsonUtf8, "{\"error\":\"bad_mac\"}");
            return;
        }

        int existingId = findStoredMacId(mac_bytes);
        if (existingId > 0
            && !(doc.containsKey("node_id") || doc.containsKey("id"))) {
            server_.send(200, kJsonUtf8,
                         "{\"status\":\"exists\",\"id\":" + String(existingId) + "}");
            registry_->clearUnknown();
            return;
        }

        int newId = -1;
        if (doc.containsKey("node_id") && doc["node_id"].as<int>() >= 1
            && doc["node_id"].as<int>() <= GW_MAX_NODES) {
            newId = doc["node_id"].as<int>();
        } else if (doc.containsKey("id") && doc["id"].as<int>() >= 1
                   && doc["id"].as<int>() <= GW_MAX_NODES) {
            newId = doc["id"].as<int>();
        } else {
            newId = registry_->findNextFreeId(*storage_);
        }
        if (newId == -1) {
            server_.send(507, kJsonUtf8, "{\"error\":\"Full\"}");
            return;
        }
        if (nameStr.length() == 0) nameStr = "Thiết bị";
        uint8_t typeVal = doc.containsKey("type")
                              ? doc["type"].as<uint8_t>()
                              : (registry_ ? registry_->unknownType() : 1);
        if (typeVal == 0) typeVal = 1;

        String nodeIdStr = String(newId);
        storage_->putBytes(("mac_" + nodeIdStr).c_str(), mac_bytes, 6);
        storage_->putString(("name_" + nodeIdStr).c_str(), nameStr);
        storage_->putUInt(("type_" + nodeIdStr).c_str(), typeVal);
        espnow_->addPeer(mac_bytes);
        registry_->registerSlot(newId, mac_bytes, typeVal, false);

        if (!doc.containsKey("node_id") && !doc.containsKey("id") && backend_) {
            backend_->registerNode(nodeIdStr, macStr, nameStr, typeVal);
        }
        registry_->clearUnknown();
        server_.send(200, kJsonUtf8,
                     "{\"status\":\"success\",\"id\":" + nodeIdStr + "}");
    }

    void handleGetNodes() {
        // Tắt «Đang chờ remote» nếu quá 35s (node timeout 30s)
        if (registry_) registry_->expireStaleLearning(35000UL);

        String json = "[";
        bool first = true;
        for (int i = 1; i <= GW_MAX_NODES; ++i) {
            String macKey  = "mac_" + String(i);
            String nameKey = "name_" + String(i);
            if (!storage_->isKey(macKey.c_str())) continue;

            uint8_t mac[6];
            storage_->getBytes(macKey.c_str(), mac, 6);
            char macStr[18];
            snprintf(macStr, 18, "%02X:%02X:%02X:%02X:%02X:%02X",
                     mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
            String nodeName = storage_->getString(nameKey.c_str(), "Node #" + String(i));

            if (!first) json += ",";
            first = false;

            bool active = false;
            uint8_t devType = static_cast<uint8_t>(storage_->getUInt(("type_" + String(i)).c_str(), 1));
            uint8_t status = 0;
            float temp = 0, hum = 0;
            uint32_t irData = 0;
            unsigned long ls = 0;
            unsigned long statusCh = 0;
            bool isLearning = false;
            const unsigned long nowMs = millis();

            if (registry_ && registry_->tryLock()) {
                int idx = i - 1;
                node_status_t* ns = registry_->nodes();
                // Đọc status kể cả khi chưa active (sau lệnh web setRelayStatus)
                if (ns[idx].node_id != 0) {
                    if (ns[idx].active) active = true;
                    if (ns[idx].device_type != 0) devType = ns[idx].device_type;
                    status     = ns[idx].status;
                    temp       = ns[idx].temperature;
                    hum        = ns[idx].humidity;
                    irData     = ns[idx].last_ir_data;
                    ls         = ns[idx].last_seen;
                    statusCh   = ns[idx].status_changed_ms;
                    isLearning = ns[idx].is_learning;
                }
                registry_->unlock();
            }

            // Đổi millis tuyệt đối → số giây trước (trình duyệt không có millis ESP)
            unsigned long lastSeenAgo = (ls > 0 && nowMs >= ls) ? ((nowMs - ls) / 1000UL) : 0;
            unsigned long statusAgo =
                (statusCh > 0 && nowMs >= statusCh) ? ((nowMs - statusCh) / 1000UL) : 0;
            const bool hasStatusTime = (statusCh > 0);

            String escapedName = nodeName;
            escapedName.replace("\"", "'");

            char buf[400];
            snprintf(buf, sizeof(buf),
                     "{\"id\":%d,\"name\":\"%s\",\"mac\":\"%s\",\"active\":%s,\"device_type\":%d,"
                     "\"status\":%d,\"temperature\":%.1f,\"humidity\":%.1f,"
                     "\"last_ir_data\":%lu,\"is_learning\":%s,"
                     "\"last_seen_ago_s\":%lu,\"status_changed_ago_s\":%lu,\"has_status_time\":%s",
                     i, escapedName.c_str(), macStr,
                     active ? "true" : "false",
                     devType, status, temp, hum,
                     static_cast<unsigned long>(irData),
                     isLearning ? "true" : "false",
                     lastSeenAgo, statusAgo,
                     hasStatusTime ? "true" : "false");
            json += buf;
            if (devType == smarthome::DEVICE_IR) {
                String irCmds = storage_->getString(("ircmds_" + String(i)).c_str(), "[]");
                json += ",\"ir_commands\":" + irCmds;
            }
            json += "}";
        }
        json += "]";
        server_.sendHeader("Access-Control-Allow-Origin", "*");
        server_.send(200, kJsonUtf8, json);
    }

    void handleDeleteNode() {
        String id = server_.arg("id");
        int nodeId = id.toInt();
        int idx = nodeId - 1;
        String macKey = "mac_" + id;
        char macStr[18] = {0};
        bool hadMac = false;

        if (storage_->isKey(macKey.c_str())) {
            uint8_t mac[6];
            storage_->getBytes(macKey.c_str(), mac, 6);
            snprintf(macStr, sizeof(macStr), "%02x:%02x:%02x:%02x:%02x:%02x",
                     mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
            hadMac = true;
            if (espnow_) {
                espnow_->removePeer(mac);
                Serial.printf("[DELETE] Peer %02X:%02X:%02X:%02X:%02X:%02X\n",
                              mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
            }
        }

        // Đồng bộ xóa trên Backend (ưu tiên MAC) — trước khi gỡ local
        if (backend_ && nodeId > 0) {
            backend_->deleteNode(nodeId, hadMac ? macStr : nullptr);
        }

        if (registry_ && idx >= 0 && idx < GW_MAX_NODES) {
            registry_->clearSlot(nodeId);
        }
        storage_->remove(macKey.c_str());
        storage_->remove(("name_" + id).c_str());
        storage_->remove(("ircmds_" + id).c_str());
        storage_->remove(("type_" + id).c_str());
        server_.send(200, "text/plain", "OK");
    }

    void handleApiIrSend() {
        if (server_.method() != HTTP_POST) {
            server_.send(405, kJsonUtf8, "{\"error\":\"Method Not Allowed\"}");
            return;
        }
        StaticJsonDocument<128> doc;
        if (deserializeJson(doc, server_.arg("plain")) != DeserializationError::Ok) {
            server_.send(400, kJsonUtf8, "{\"error\":\"Bad JSON\"}");
            return;
        }
        int nodeId = doc["node_id"];
        uint32_t irCode = doc["ir_data"].as<unsigned long>();
        if (nodeId < 1 || nodeId > GW_MAX_NODES) {
            server_.send(400, kJsonUtf8, "{\"error\":\"Invalid node_id\"}");
            return;
        }
        String prefKey = "mac_" + String(nodeId);
        if (!storage_->isKey(prefKey.c_str())) {
            server_.send(404, kJsonUtf8, "{\"error\":\"Node not found\"}");
            return;
        }
        uint8_t mac[6];
        storage_->getBytes(prefKey.c_str(), mac, 6);
        if (espnow_ && espnow_->sendIr(mac, static_cast<uint8_t>(nodeId),
                                       smarthome::CMD_IR_DATA, irCode)) {
            server_.send(200, kJsonUtf8, "{\"status\":\"sent\"}");
            Serial.printf("[IR CTRL] 0x%08X -> Node %d\n", static_cast<unsigned>(irCode), nodeId);
        } else {
            server_.send(500, kJsonUtf8, "{\"error\":\"ESP-NOW send failed\"}");
        }
    }

    void handleApiIrSave() {
        if (server_.method() != HTTP_POST) {
            server_.send(405, kJsonUtf8, "{\"error\":\"Method Not Allowed\"}");
            return;
        }
        StaticJsonDocument<256> doc;
        if (deserializeJson(doc, server_.arg("plain")) != DeserializationError::Ok) {
            server_.send(400, kJsonUtf8, "{\"error\":\"Bad JSON\"}");
            return;
        }
        int nodeId = doc["node_id"];
        String name = doc["name"].as<String>();
        uint32_t code = doc["code"].as<unsigned long>();
        uint32_t savedCode = code;

        if (smarthome::isRawIrToken(code)) {
            String slotKey = "slot_" + String(nodeId);
            uint32_t slotId = storage_->getUInt(slotKey.c_str(), 100);
            storage_->putUInt(slotKey.c_str(), slotId + 1);
            savedCode = slotId;

            String macKey = "mac_" + String(nodeId);
            if (storage_->isKey(macKey.c_str()) && espnow_) {
                uint8_t mac[6];
                storage_->getBytes(macKey.c_str(), mac, 6);
                espnow_->sendIr(mac, static_cast<uint8_t>(nodeId),
                                smarthome::CMD_IR_SAVE, slotId);
                Serial.printf("[IR SAVE RAW] Node %d Slot #%u\n", nodeId, static_cast<unsigned>(slotId));
            }
        }

        String prefKey = "ircmds_" + String(nodeId);
        String currentJson = storage_->getString(prefKey.c_str(), "[]");
        StaticJsonDocument<1024> arrDoc;
        deserializeJson(arrDoc, currentJson);
        JsonArray arr = arrDoc.as<JsonArray>();
        JsonObject newItem = arr.createNestedObject();
        newItem["n"] = name;
        newItem["c"] = savedCode;
        String newJson;
        serializeJson(arrDoc, newJson);
        storage_->putString(prefKey.c_str(), newJson);

        if (registry_) registry_->clearIrLearnUi(nodeId);

        char respBuf[128];
        snprintf(respBuf, sizeof(respBuf), "{\"status\":\"ok\",\"saved_code\":%lu}",
                 static_cast<unsigned long>(savedCode));
        server_.send(200, kJsonUtf8, respBuf);
    }

    void handleApiIrDelete() {
        if (server_.method() != HTTP_POST) {
            server_.send(405, kJsonUtf8, "{\"error\":\"Method Not Allowed\"}");
            return;
        }
        StaticJsonDocument<128> doc;
        if (deserializeJson(doc, server_.arg("plain")) != DeserializationError::Ok) {
            server_.send(400, kJsonUtf8, "{\"error\":\"Bad JSON\"}");
            return;
        }
        int nodeId = doc["node_id"];
        String prefKey = "ircmds_" + String(nodeId);
        String currentJson = storage_->getString(prefKey.c_str(), "[]");
        StaticJsonDocument<1024> arrDoc;
        deserializeJson(arrDoc, currentJson);
        JsonArray arr = arrDoc.as<JsonArray>();

        if (doc.containsKey("index")) {
            arr.remove(doc["index"].as<int>());
        } else if (doc.containsKey("code")) {
            uint32_t targetCode = doc["code"].as<unsigned long>();
            for (int i = 0; i < static_cast<int>(arr.size()); ++i) {
                if (arr[i]["c"].as<unsigned long>() == targetCode) {
                    arr.remove(i);
                    break;
                }
            }
        }
        String newJson;
        serializeJson(arrDoc, newJson);
        storage_->putString(prefKey.c_str(), newJson);
        server_.send(200, kJsonUtf8, "{\"status\":\"ok\"}");
    }

    void handleApiIrLearn() {
        if (server_.method() != HTTP_POST) {
            server_.send(405, kJsonUtf8, "{\"error\":\"Method Not Allowed\"}");
            return;
        }
        StaticJsonDocument<128> doc;
        if (deserializeJson(doc, server_.arg("plain")) != DeserializationError::Ok) {
            server_.send(400, kJsonUtf8, "{\"error\":\"Bad JSON\"}");
            return;
        }
        int nodeId = doc["node_id"];
        String macKey = "mac_" + String(nodeId);
        if (!storage_->isKey(macKey.c_str())) {
            server_.send(404, kJsonUtf8, "{\"error\":\"Node not found\"}");
            return;
        }
        uint8_t mac[6];
        storage_->getBytes(macKey.c_str(), mac, 6);
        if (registry_) registry_->setLearning(nodeId, true);

        if (espnow_ && espnow_->sendIr(mac, static_cast<uint8_t>(nodeId),
                                       smarthome::CMD_IR_LEARN, 0)) {
            server_.send(200, kJsonUtf8, "{\"status\":\"learning\"}");
            Serial.printf("[IR LEARN] 0x11 -> Node %d\n", nodeId);
        } else {
            server_.send(500, kJsonUtf8, "{\"error\":\"ESP-NOW send failed\"}");
        }
    }

    void handleApiControl() {
        if (server_.method() != HTTP_POST) {
            server_.send(405, kJsonUtf8, "{\"error\":\"Method Not Allowed\"}");
            return;
        }
        StaticJsonDocument<128> doc;
        if (deserializeJson(doc, server_.arg("plain")) != DeserializationError::Ok) {
            server_.send(400, kJsonUtf8, "{\"error\":\"Bad JSON\"}");
            return;
        }
        int nodeId  = doc["node_id"];
        uint8_t cmd = doc["command"];
        if (nodeId < 1 || nodeId > GW_MAX_NODES
            || (cmd != smarthome::CMD_RELAY_ON && cmd != smarthome::CMD_RELAY_OFF)) {
            server_.send(400, kJsonUtf8, "{\"error\":\"Invalid params\"}");
            return;
        }
        String prefKey = "mac_" + String(nodeId);
        if (!storage_->isKey(prefKey.c_str())) {
            server_.send(404, kJsonUtf8, "{\"error\":\"Node not found\"}");
            return;
        }
        uint8_t mac[6];
        storage_->getBytes(prefKey.c_str(), mac, 6);
        if (espnow_ && espnow_->sendControl(mac, static_cast<uint8_t>(nodeId), cmd)) {
            if (registry_) {
                registry_->setRelayStatus(nodeId, (cmd == smarthome::CMD_RELAY_ON) ? 1 : 0);
            }
            server_.send(200, kJsonUtf8, "{\"status\":\"sent\"}");
            Serial.printf("[CTRL] %s -> Node %d\n",
                          cmd == smarthome::CMD_RELAY_ON ? "BAT" : "TAT", nodeId);
        } else {
            server_.send(500, kJsonUtf8, "{\"error\":\"ESP-NOW send failed\"}");
        }
    }
};
