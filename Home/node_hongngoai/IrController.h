/**
 * @file IrController.h
 * @brief Thu/phát IR (IRremoteESP8266), learn raw, blast NEC/raw, LED active-LOW.
 *        ESP32-C3 SuperMini: RECV=GPIO4, SEND=GPIO5, LED=GPIO8.
 *
 * Thu: buffer/timeout/tolerance nhạy hơn; GHI raw luôn 100% (timing trung thực).
 * Phát: IR_BLAST_GAIN_PERCENT + duty cao (chỉ khuếch đại đầu ra).
 */
#pragma once

#include <Arduino.h>
#include <IRremoteESP8266.h>
#include <IRrecv.h>
#include <IRsend.h>
#include <IRutils.h>
#include "StorageManager.h"
#include "EspNowConfig.h"

#ifndef IR_RECV_PIN
#define IR_RECV_PIN 4
#endif
#ifndef IR_SEND_PIN
#define IR_SEND_PIN 5
#endif
#ifndef IR_LED_PIN
#define IR_LED_PIN 8
#endif

/** Buffer lớn hơn — frame dài / remote suy hao không bị cắt */
#ifndef IR_RECV_BUF_SIZE
#define IR_RECV_BUF_SIZE 1536
#endif

/**
 * Timeout giữa các cạnh (ms). Lớn hơn = nhạy hơn với khoảng nghỉ dài
 * (tín hiệu yếu thường “kéo” gap).
 */
#ifndef IR_TIMEOUT_MS
#define IR_TIMEOUT_MS 50
#endif

#ifndef IR_RAW_MAX
#define IR_RAW_MAX 250
#endif

/** Sai số timing decode (%). Cao hơn → chấp nhận xung méo do suy hao */
#ifndef IR_TOLERANCE_PERCENT
#define IR_TOLERANCE_PERCENT 40
#endif

/** Ngưỡng tối thiểu cho decode UNKNOWN */
#ifndef IR_UNKNOWN_THRESHOLD
#define IR_UNKNOWN_THRESHOLD 6
#endif

/**
 * Số mẫu raw tối thiểu khi học (rawlen library gồm +1).
 * Trước 25; hạ xuống để remote xa / yếu vẫn học được.
 */
#ifndef IR_LEARN_MIN_RAWLEN
#define IR_LEARN_MIN_RAWLEN 18
#endif

/** Carrier IR (kHz) — hầu hết remote TV/AC = 38 */
#ifndef IR_CARRIER_KHZ
#define IR_CARRIER_KHZ 38
#endif

/**
 * Duty cycle PWM IR khi phát (%). sendRaw() mặc định 50%.
 * ~60–65: hơi mạnh hơn, ít méo. 80+ + gain xung dễ làm TV/AC không nhận.
 */
#ifndef IR_TX_DUTY_PERCENT
#define IR_TX_DUTY_PERCENT 60
#endif

/**
 * Gain ĐẦU RA (nhân độ rộng xung lúc blast). 100 = đúng timing đã học.
 * Gain cao (120+) thường LÀM HỎNG bật/tắt — giữ 100; mạnh hơn nhờ HW/duty.
 */
#ifndef IR_BLAST_GAIN_PERCENT
#define IR_BLAST_GAIN_PERCENT 100
#endif

/**
 * Số frame khi blast. Giữ 1 (tránh toggle x2).
 * Chỉ tăng khi đã ổn 1 frame mà vẫn yếu khoảng cách.
 */
#ifndef IR_BLAST_REPEAT
#define IR_BLAST_REPEAT 1
#endif

/** Khoảng nghỉ giữa các lần lặp (ms) — chỉ khi IR_BLAST_REPEAT > 1 */
#ifndef IR_BLAST_GAP_MS
#define IR_BLAST_GAP_MS 100UL
#endif

/** Timeout phiên học — tránh LED/learn kẹt mãi nếu không bấm remote */
#ifndef IR_LEARN_TIMEOUT_MS
#define IR_LEARN_TIMEOUT_MS 30000UL
#endif

/** Cooldown sau khi học xong / lưu — bỏ nhiễu IR lặp */
#ifndef IR_LEARN_COOLDOWN_MS
#define IR_LEARN_COOLDOWN_MS 3000UL
#endif

/** Bỏ nhiễu RF ngay sau khi vào LEARN (ms) */
#ifndef IR_LEARN_NOISE_GUARD_MS
#define IR_LEARN_NOISE_GUARD_MS 250UL
#endif

/** Sự kiện poll() trả về cho App (gửi ESP-NOW). */
struct IrPollEvent {
    bool hasEvent;
    bool fromLearning;   // true = vừa học raw xong → mới gửi 0x10 lên GW
    uint32_t code;       // raw token hoặc NEC code
};

class IrController {
public:
    explicit IrController(StorageManager* storage = nullptr)
        : receiver_(IR_RECV_PIN, IR_RECV_BUF_SIZE, IR_TIMEOUT_MS, true),
          sender_(IR_SEND_PIN),
          storage_(storage),
          lastIrCode_(0),
          hasIrCode_(false),
          learning_(false),
          learnCooldownUntil_(0),
          learnStartTime_(0),
          lastLearnedToken_(0),
          rawLen_(0) {
        memset(rawBuf_, 0, sizeof(rawBuf_));
    }

    void setStorage(StorageManager* storage) { storage_ = storage; }

    void begin() {
        pinMode(IR_LED_PIN, OUTPUT);
        ledOff();

        sender_.begin();
        // Prefill carrier + duty cao (sendRawStrong gọi lại mỗi lần blast)
        sender_.enableIROut(IR_CARRIER_KHZ, IR_TX_DUTY_PERCENT);
        Serial.printf("[IR] Bóng phát: carrier=%ukHz duty=%u%% blastGain=%u%% x%u\n",
                      static_cast<unsigned>(IR_CARRIER_KHZ),
                      static_cast<unsigned>(IR_TX_DUTY_PERCENT),
                      static_cast<unsigned>(IR_BLAST_GAIN_PERCENT),
                      static_cast<unsigned>(IR_BLAST_REPEAT));

        // --- Độ nhạy thu (tín hiệu đã suy hao) ---
        receiver_.setTolerance(IR_TOLERANCE_PERCENT);
        receiver_.setUnknownThreshold(IR_UNKNOWN_THRESHOLD);
        receiver_.enableIRIn();
        Serial.printf("[IR] Mắt thu: buf=%u timeout=%ums tol=%u%% minRaw=%u | store=100%% (trung thực)\n",
                      static_cast<unsigned>(IR_RECV_BUF_SIZE),
                      static_cast<unsigned>(IR_TIMEOUT_MS),
                      static_cast<unsigned>(IR_TOLERANCE_PERCENT),
                      static_cast<unsigned>(IR_LEARN_MIN_RAWLEN));
        Serial.println("[IR] Gợi ý HW: LED IR + transistor (100–200mA xung), không chỉ GPIO thuần.");
    }

    // ---- LED SuperMini: active LOW ----
    void ledOn()  { digitalWrite(IR_LED_PIN, LOW); }
    void ledOff() { digitalWrite(IR_LED_PIN, HIGH); }

    bool isLearning() const { return learning_; }
    bool hasIrCode() const { return hasIrCode_; }
    uint32_t lastIrCode() const { return lastIrCode_; }
    uint8_t statusByte() const { return hasIrCode_ ? 1 : 0; }

    /**
     * Bắt đầu LEARN_IR (0x11).
     * QUAN TRỌNG: gán learnStartTime_ TRƯỚC learning_=true.
     * Không delay() ở đây — có thể gọi từ callback ESP-NOW (WiFi task);
     * delay + learning_ bật sớm → loop/poll thấy millis()-0 >= 30s → hủy ngay.
     */
    void startLearning() {
        if (learning_) {
            Serial.println("[IR LEARN] Đã trong phiên học — bỏ qua startLearning trùng");
            return;
        }
        // Timestamp trước flag — tránh race với poll() trên task chính
        learnStartTime_   = millis();
        lastLearnedToken_ = 0;
        learning_         = true;
        ledOn();
        receiver_.resume();
        Serial.println("[IR LEARN] Bật LED, đang chờ remote (tối đa 30s)...");
    }

    /** Hủy phiên học (timeout / lưu xong). */
    void cancelLearning() {
        if (!learning_ && lastLearnedToken_ == 0) return;
        learning_       = false;
        learnStartTime_ = 0;
        ledOff();
        learnCooldownUntil_ = millis() + IR_LEARN_COOLDOWN_MS;
        Serial.println("[IR LEARN] Đã thoát phiên học / cooldown");
    }

    /** Sau SAVE 0x12: không còn pending; cooldown chống nhiễu. */
    void onSaveAcknowledged() {
        learning_         = false;
        learnStartTime_   = 0;
        lastLearnedToken_ = 0;
        ledOff();
        learnCooldownUntil_ = millis() + IR_LEARN_COOLDOWN_MS;
        Serial.println("[IR SAVE] Pending học đã xóa, cooldown 3s");
    }

    /**
     * Phát IR mạnh hơn remote GPIO thuần:
     * - Tắt mắt thu trong lúc TX (timing sạch hơn)
     * - Duty PWM cao (enableIROut) — không dùng sendRaw() vì nó ép duty=50%
     * - Nhân xung lúc phát (IR_BLAST_GAIN_PERCENT)
     * - Lặp frame nhiều lần
     */
    void blast(uint32_t slotOrCode) {
        if (storage_ != nullptr && slotOrCode > 0) {
            String keyLen = "l_" + String(slotOrCode);
            String keyBuf = "r_" + String(slotOrCode);
            if (storage_->isKey(keyLen.c_str())) {
                uint16_t playLen = static_cast<uint16_t>(storage_->getUInt(keyLen.c_str(), 0));
                if (playLen > 0 && playLen <= IR_RAW_MAX) {
                    uint16_t playBuf[IR_RAW_MAX];
                    storage_->getBytes(keyBuf.c_str(), playBuf, playLen * sizeof(uint16_t));
                    applyPulseGain(playBuf, playLen, IR_BLAST_GAIN_PERCENT);
                    Serial.printf("[IR BLAST] Raw Slot #%u (%u xung) duty=%u%% gain=%u%% x%u\n",
                                  static_cast<unsigned>(slotOrCode), playLen,
                                  static_cast<unsigned>(IR_TX_DUTY_PERCENT),
                                  static_cast<unsigned>(IR_BLAST_GAIN_PERCENT),
                                  static_cast<unsigned>(IR_BLAST_REPEAT));
                    blastRawStrong(playBuf, playLen);
                    Serial.println("[IR BLAST] Xong!");
                    return;
                }
            }
        }

        // Fallback NEC: lặp + duty cao
        uint32_t codeToSend = (slotOrCode != 0) ? slotOrCode : lastIrCode_;
        if (codeToSend != 0 && !smarthome::isRawIrToken(codeToSend)) {
            Serial.printf("[IR SEND] NEC: 0x%08X duty=%u%% x%u\n",
                          static_cast<unsigned>(codeToSend),
                          static_cast<unsigned>(IR_TX_DUTY_PERCENT),
                          static_cast<unsigned>(IR_BLAST_REPEAT));
            receiver_.disableIRIn();
            for (int n = 0; n < IR_BLAST_REPEAT; ++n) {
                sender_.enableIROut(IR_CARRIER_KHZ, IR_TX_DUTY_PERCENT);
                sender_.sendNEC(codeToSend, 32);
                if (n + 1 < IR_BLAST_REPEAT) delay(IR_BLAST_GAP_MS);
            }
            receiver_.enableIRIn();
            Serial.println("[IR SEND] Xong!");
        } else if (codeToSend != 0) {
            Serial.println("[IR SEND] Token raw nhưng không có slot Flash — không phát được.");
        } else {
            Serial.println("[IR SEND] Chưa có mã để phát!");
        }
    }

    /** SAVE_RAW_IR (0x12): RAM raw → Flash slot. */
    bool saveRawToSlot(uint32_t slotId) {
        if (slotId == 0 || rawLen_ == 0 || storage_ == nullptr) {
            Serial.println("[IR SAVE] Lỗi: Chưa có raw trong RAM hoặc slot không hợp lệ!");
            return false;
        }
        String keyLen = "l_" + String(slotId);
        String keyBuf = "r_" + String(slotId);
        storage_->putUInt(keyLen.c_str(), rawLen_);
        storage_->putBytes(keyBuf.c_str(), rawBuf_, rawLen_ * sizeof(uint16_t));
        Serial.printf("[IR SAVE] Đã lưu %u xung → Slot #%u\n",
                      rawLen_, static_cast<unsigned>(slotId));
        return true;
    }

    /**
     * Gọi mỗi vòng loop — decode IR, xử lý learn.
     * Chỉ báo hasEvent+fromLearning khi vừa học xong (App mới gửi 0x10 lên GW).
     * Passive động: chỉ cập nhật local, KHÔNG gửi 0x10 (tránh UI «mã mới» lặp).
     */
    IrPollEvent poll() {
        IrPollEvent ev;
        ev.hasEvent = false;
        ev.fromLearning = false;
        ev.code = 0;

        // Timeout phiên học (cần learnStartTime_ hợp lệ — tránh false timeout lúc vừa bật)
        if (learning_ && learnStartTime_ != 0
            && (millis() - learnStartTime_ >= IR_LEARN_TIMEOUT_MS)) {
            Serial.println("[IR LEARN] Hết 30s — hủy phiên học");
            cancelLearning();
        }

        if (!receiver_.decode(&results_)) {
            return ev;
        }

        if (learning_) {
            Serial.printf("[IR LEARN DEBUG] type=%d bits=%d rawlen=%d\n",
                          static_cast<int>(results_.decode_type),
                          results_.bits, results_.rawlen);

            // Guard 1: bỏ nhiễu ngay sau ESP-NOW / bật LED
            if (millis() - learnStartTime_ < IR_LEARN_NOISE_GUARD_MS) {
                receiver_.resume();
                return ev;
            }
            // Guard 2: tối thiểu IR_LEARN_MIN_RAWLEN (nhạy hơn khi tín hiệu yếu)
            if (results_.rawlen < static_cast<uint16_t>(IR_LEARN_MIN_RAWLEN)) {
                Serial.printf("[IR LEARN] Bỏ qua nhiễu ngắn (%d xung, cần >=%u). Vẫn chờ...\n",
                              results_.rawlen - 1,
                              static_cast<unsigned>(IR_LEARN_MIN_RAWLEN));
                receiver_.resume();
                return ev;
            }

            learning_ = false;
            learnCooldownUntil_ = millis() + IR_LEARN_COOLDOWN_MS;
            ledOff();

            rawLen_ = static_cast<uint16_t>(min(static_cast<int>(results_.rawlen - 1), IR_RAW_MAX));
            for (uint16_t i = 0; i < rawLen_; ++i) {
                // RAWTICK 2µs → µs — LUÔN 100%, không nhân gain khi học
                uint32_t us = static_cast<uint32_t>(results_.rawbuf[i + 1]) * 2u;
                if (us > 0xFFFFu) us = 0xFFFFu;
                if (us < 1u) us = 1u;
                rawBuf_[i] = static_cast<uint16_t>(us);
            }

            uint32_t rawToken = smarthome::makeRawIrToken(rawLen_);
            lastIrCode_       = rawToken;
            hasIrCode_        = true;
            lastLearnedToken_ = rawToken;

            Serial.printf("[IR LEARN] Ghi âm OK — %u xung @100%% (gain chỉ lúc phát=%u%%)\n",
                          rawLen_, static_cast<unsigned>(IR_BLAST_GAIN_PERCENT));
            delay(50);
            ev.hasEvent     = true;
            ev.fromLearning = true;
            ev.code         = rawToken;
            receiver_.resume();
            return ev;
        }

        // Passive: chỉ log + nhớ local — KHÔNG tạo event gửi Gateway
        if (millis() >= learnCooldownUntil_
            && results_.decode_type != UNKNOWN
            && results_.bits >= 8) {
            uint32_t code = static_cast<uint32_t>(results_.value);
            if (code != 0 && code != 0xFFFFFFFFu) {
                lastIrCode_ = code;
                hasIrCode_  = true;
                Serial.printf("[IR RECV local] 0x%08X (không gửi GW — chỉ phiên học mới gửi)\n",
                              static_cast<unsigned>(code));
            }
        }

        receiver_.resume();
        return ev;
    }

private:
    /** Nhân độ rộng xung (µs) — bù LED yếu / khoảng cách. */
    static void applyPulseGain(uint16_t* buf, uint16_t len, uint16_t gainPercent) {
        if (buf == nullptr || gainPercent == 100) return;
        for (uint16_t i = 0; i < len; ++i) {
            uint32_t us = (static_cast<uint32_t>(buf[i]) * gainPercent) / 100u;
            if (us < 1u) us = 1u;
            if (us > 0xFFFFu) us = 0xFFFFu;
            buf[i] = static_cast<uint16_t>(us);
        }
    }

    /**
     * Phát raw với duty tùy chọn.
     * Không gọi IRsend::sendRaw() — hàm đó luôn enableIROut(hz) với duty mặc định 50%.
     */
    void blastRawStrong(const uint16_t* buf, uint16_t len) {
        if (buf == nullptr || len == 0) return;

        // Tắt RX trong lúc TX — giảm nhiễu timing (cùng chip)
        receiver_.disableIRIn();

        for (int n = 0; n < IR_BLAST_REPEAT; ++n) {
            sender_.enableIROut(IR_CARRIER_KHZ, IR_TX_DUTY_PERCENT);
            for (uint16_t i = 0; i < len; ++i) {
                if (i & 1u) {
                    sender_.space(buf[i]);
                } else {
                    sender_.mark(buf[i]);
                }
            }
            // ledOff() là protected trên IRremoteESP8266 2.9 — space() tắt LED
            sender_.space(0);
            if (n + 1 < IR_BLAST_REPEAT) {
                delay(IR_BLAST_GAP_MS);
            }
        }

        receiver_.enableIRIn();
        receiver_.resume();
    }

    IRrecv receiver_;
    IRsend sender_;
    StorageManager* storage_;
    decode_results results_;

    uint32_t lastIrCode_;
    bool hasIrCode_;
    bool learning_;
    unsigned long learnCooldownUntil_;
    unsigned long learnStartTime_;
    uint32_t lastLearnedToken_;  // token vừa học — tránh gửi lặp

    uint16_t rawBuf_[IR_RAW_MAX];
    uint16_t rawLen_;
};
