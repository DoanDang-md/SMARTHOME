/**
 * @file StorageManager.h
 * @brief Bao bọc Preferences — lưu node_id, trạng thái relay, v.v.
 */
#pragma once

#include <Arduino.h>
#include <Preferences.h>

class StorageManager {
public:
    explicit StorageManager(const char* nsName)
        : nsName_(nsName), opened_(false) {}

    bool begin(bool readOnly = false) {
        opened_ = preferences_.begin(nsName_, readOnly);
        return opened_;
    }

    void end() {
        if (opened_) {
            preferences_.end();
            opened_ = false;
        }
    }

    uint8_t getNodeId(uint8_t defaultId = 1) {
        return static_cast<uint8_t>(preferences_.getUInt("node_id", defaultId));
    }

    void putNodeId(uint8_t id) {
        preferences_.putUInt("node_id", id);
    }

    bool getRelay(bool defaultVal = false) {
        return preferences_.getBool("relay", defaultVal);
    }

    void putRelay(bool on) {
        preferences_.putBool("relay", on);
    }

    // API generic cho các node khác (Hybrid/IR/Gateway) tái sử dụng
    uint32_t getUInt(const char* key, uint32_t def = 0) {
        return preferences_.getUInt(key, def);
    }

    void putUInt(const char* key, uint32_t val) {
        preferences_.putUInt(key, val);
    }

    bool getBool(const char* key, bool def = false) {
        return preferences_.getBool(key, def);
    }

    void putBool(const char* key, bool val) {
        preferences_.putBool(key, val);
    }

    String getString(const char* key, const String& def = String()) {
        return preferences_.getString(key, def);
    }

    void putString(const char* key, const String& val) {
        preferences_.putString(key, val);
    }

    size_t getBytes(const char* key, void* buf, size_t maxLen) {
        return preferences_.getBytes(key, buf, maxLen);
    }

    size_t putBytes(const char* key, const void* buf, size_t len) {
        return preferences_.putBytes(key, buf, len);
    }

    bool isKey(const char* key) {
        return preferences_.isKey(key);
    }

    bool remove(const char* key) {
        return preferences_.remove(key);
    }

    Preferences& raw() { return preferences_; }

private:
    const char* nsName_;
    bool opened_;
    Preferences preferences_;
};
