/**
 * @file WifiProvisioner.h
 * @brief STA connect từ Preferences hoặc AP Setup mode.
 *
 * Quan trọng: tắt WiFi modem sleep — nếu bật PS, ESP32 hay RƠI gói ESP-NOW
 * định kỳ từ node (TX chủ động vẫn OK → điều khiển relay được, sensor mất).
 */
#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include "StorageManager.h"

class WifiProvisioner {
public:
    explicit WifiProvisioner(StorageManager& storage)
        : storage_(storage), setupMode_(false) {}

    /** Tắt power-save để ESP-NOW RX ổn định. verbose=false khi gọi sau mỗi HTTP. */
    static void disableModemSleep(bool verbose = true) {
        WiFi.setSleep(false);
        esp_wifi_set_ps(WIFI_PS_NONE);
        if (verbose) {
            Serial.println("[WiFi] Modem sleep OFF (WIFI_PS_NONE) — ESP-NOW RX ổn định");
        }
    }

    bool connectSta(int maxRetries = 20) {
        String ssid = storage_.getString("wifi_ssid", "");
        String pass = storage_.getString("wifi_pass", "");
        if (ssid.length() == 0) return false;

        Serial.printf("\n[WiFi] Đang kết nối: %s\n", ssid.c_str());
        WiFi.mode(WIFI_STA);
        // Tắt sleep trước khi begin (một số core cần set lại sau khi connected)
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
            setupMode_ = false;
            return true;
        }
        return false;
    }

    void startApSetup() {
        setupMode_ = true;
        WiFi.mode(WIFI_AP);
        WiFi.softAP("SmartHome_Setup", "12345678");
        Serial.print("\n[AP MODE] IP: ");
        Serial.println(WiFi.softAPIP());
    }

    void saveCredentials(const String& ssid, const String& pass) {
        storage_.putString("wifi_ssid", ssid);
        storage_.putString("wifi_pass", pass);
    }

    void clearCredentials() {
        storage_.remove("wifi_ssid");
        storage_.remove("wifi_pass");
    }

    bool isSetupMode() const { return setupMode_; }

private:
    StorageManager& storage_;
    bool setupMode_;
};
