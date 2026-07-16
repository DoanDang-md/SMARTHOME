/**
 * @file WifiProvisioner.h
 * @brief STA connect từ Preferences hoặc AP Setup mode.
 *
 * Setup AP dùng WIFI_AP_STA để vẫn quét được WiFi xung quanh.
 * Tắt WiFi modem sleep — ESP-NOW RX ổn định.
 * mDNS: hostname "gateway" → http://gateway.local (LAN, sau khi STA OK).
 */
#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <esp_wifi.h>
#include "StorageManager.h"

class WifiProvisioner {
public:
    /** Hostname mDNS (không kèm .local). Truy cập: http://gateway.local */
    static constexpr const char* kMdnsHostname = "gateway";

    explicit WifiProvisioner(StorageManager& storage)
        : storage_(storage), setupMode_(false) {}

    static void disableModemSleep(bool verbose = true) {
        WiFi.setSleep(false);
        esp_wifi_set_ps(WIFI_PS_NONE);
        if (verbose) {
            Serial.println("[WiFi] Modem sleep OFF (WIFI_PS_NONE) — ESP-NOW RX ổn định");
        }
    }

    /**
     * Bật mDNS responder + quảng bá HTTP service.
     * Gọi sau khi WiFi STA đã connected. Idempotent (end rồi begin lại nếu cần).
     */
    static bool startMdns() {
        // Đặt hostname mạng (DHCP / mDNS) — gọi an toàn sau khi đã STA
        WiFi.setHostname(kMdnsHostname);

        // Restart nếu đã start (reconnect / gọi lại)
        MDNS.end();
        if (!MDNS.begin(kMdnsHostname)) {
            Serial.println("[mDNS] Khởi động thất bại — dùng IP LAN thay thế");
            return false;
        }
        // Bonjour / Avahi: _http._tcp → trình duyệt tìm được dịch vụ web
        MDNS.addService("http", "tcp", 80);
        Serial.printf("[mDNS] Web UI: http://%s.local  (IP %s)\n",
                      kMdnsHostname, WiFi.localIP().toString().c_str());
        return true;
    }

    bool connectSta(int maxRetries = 20) {
        String ssid = storage_.getString("wifi_ssid", "");
        String pass = storage_.getString("wifi_pass", "");
        if (ssid.length() == 0) return false;

        Serial.printf("\n[WiFi] Đang kết nối: %s\n", ssid.c_str());
        WiFi.mode(WIFI_STA);
        // Hostname trước begin — một số stack DHCP dùng tên này
        WiFi.setHostname(kMdnsHostname);
        WiFi.setSleep(false);
        WiFi.begin(ssid.c_str(), pass.c_str());

        int retries = 0;
        while (WiFi.status() != WL_CONNECTED && retries < maxRetries) {
            delay(500);
            Serial.print(".");
            retries++;
        }
        if (WiFi.status() == WL_CONNECTED) {
            disableModemSleep();
            Serial.print("\n[WiFi] IP LAN: ");
            Serial.println(WiFi.localIP());
            Serial.printf("[WiFi] STA MAC: %s | Channel: %u\n",
                          WiFi.macAddress().c_str(), WiFi.channel());
            startMdns();
            setupMode_ = false;
            return true;
        }
        return false;
    }

    void startApSetup() {
        setupMode_ = true;
        // AP + STA: giữ softAP và cho phép scanNetworks()
        WiFi.mode(WIFI_AP_STA);
        WiFi.softAP("SmartHome_Setup", "12345678");
        Serial.print("\n[AP MODE] IP: ");
        Serial.println(WiFi.softAPIP());
        Serial.println("[AP MODE] SSID=SmartHome_Setup  pass=12345678");
        Serial.println("[AP MODE] Mở http://192.168.4.1 — quét WiFi + nhập Backend");
        Serial.println("[AP MODE] Sau khi vào LAN: http://gateway.local");
    }

    void saveCredentials(const String& ssid, const String& pass) {
        storage_.putString("wifi_ssid", ssid);
        storage_.putString("wifi_pass", pass);
    }

    void saveBackendUrl(const String& url) {
        String u = url;
        u.trim();
        while (u.endsWith("/")) {
            u.remove(u.length() - 1);
        }
        if (u.length() > 0 && !u.startsWith("http://") && !u.startsWith("https://")) {
            u = "http://" + u;
        }
        storage_.putString("backend_url", u);
        Serial.printf("[WiFi] Lưu backend_url=%s\n", u.c_str());
    }

    void clearCredentials() {
        storage_.remove("wifi_ssid");
        storage_.remove("wifi_pass");
        // Giữ backend_url để lần setup sau khỏi gõ lại (xóa hẳn nếu muốn reset full)
    }

    bool isSetupMode() const { return setupMode_; }

private:
    StorageManager& storage_;
    bool setupMode_;
};
