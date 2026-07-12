# SmartHome ESP-NOW — Ghi chú Refactor OOP

> Cập nhật sau mỗi phiên code: **làm gì · vì sao · tránh lỗi gì**. Rồi gộp/tóm tắt phiên cũ để file không phình.
> Cập nhật lần cuối: 2026-07-13 (Kế hoạch + sync Gateway ↔ SmartHome backend)

---

## 1. Phần cứng

| Thiết bị | Chip | Ghi chú |
|----------|------|---------|
| Gateway | ESP32-S3 N16R8 | FreeRTOS, dual-core |
| Node Relay | ESP-01S (ESP8266) | `RELAY_PIN=0` (GPIO2=LED board), `#include <espnow.h>` |
| Node Hybrid | ESP-01S (ESP8266) | `RELAY_PIN=0`, `DHTPIN=2`, DHT11 |
| Node IR | ESP32-C3 SuperMini | RECV=4, SEND=5, LED=8 (active LOW) |

- ESP8266: `espnow.h` + `ESP8266WiFi.h`
- ESP32/S3/C3: `esp_now.h` + `WiFi.h` + `esp_wifi.h`

---

## 2. Protocol (KHÔNG ĐỔI LAYOUT BINARY)

```cpp
typedef struct __attribute__((packed)) {
    uint8_t  mac[6];
    uint8_t  node_id;
    uint8_t  device_type;  // 1=Relay, 2=IR, 3=Sensor, 4=Hybrid
    uint8_t  command;
    uint16_t seq;
    float    temperature;
    float    humidity;
    uint8_t  status;       // 0=OFF, 1=ON
    uint32_t ir_data;
} esp_now_packet_t;
```

| Cmd | Tên | Ý nghĩa |
|-----|-----|---------|
| `0x00` | DISCOVERY | Node → GW báo danh / quét kênh |
| `0x01` / `0x02` | RELAY_ON/OFF | GW → Node |
| `0x03` | ACK/REPORT | ACK discovery, heartbeat, ACK lệnh |
| `0x10` | IR_EVENT/BLAST | Node→GW mã IR; GW→Node phát IR |
| `0x11` | LEARN_IR | GW → Node học IR |
| `0x12` | SAVE_RAW_IR | GW → Node lưu raw Flash |

**IR raw:** token `0x80000000|raw_len`; Node keys `l_<slot>`, `r_<slot>`; GW `ircmds_<nodeId>` JSON; slot từ 100.

**Preferences:** GW `smarthome` (wifi, mac_N, name_N, type_N, ircmds_N, slot_N); Relay `relay_node`; Hybrid `hybrid_node`; IR `ir_store`.

**Backend hardcode:** `http://192.168.1.100:8000/api/...` (đổi trong `BackendClient`).

---

## 3. Luồng chính (rút gọn)

1. **Channel scan (Node):** STA, peer broadcast, quét ch 1→13 gửi `0x00` → nhận ACK `0x03` → lock unicast GW.
2. **Relay:** Web → `0x01/02` → GPIO + Preferences → ACK `0x03`. Heartbeat: Relay 15s / Hybrid **5s** / IR 30s.
3. **IR learn:** `/api/ir/learn` → `0x11` → Node `0x10` rawToken → save → `0x12` + `ircmds_*` → send qua `0x10`.
4. **Gateway FreeRTOS:** recv → queue → `Task_ServerSync` (HTTP/cache); `Task_LocalWebServer`; `loop` = `vTaskDelete`. AP setup: `SmartHome_Setup` / `12345678`.

---

## 4. Kiến trúc OOP (đã xong ✅)

| Class | Vai trò |
|-------|---------|
| `EspNowPacket` / `EspNowConfig` | Protocol + constants (`common/` → copy vào mỗi sketch) |
| `EspNowManager` / `EspNowGateway` | WiFi STA, peer, send/recv, scan; static thunk cho C-callback |
| `StorageManager` | Wrap Preferences |
| `DeviceNode` | Base: begin/loop/handleCommand |
| `RelayController` / `DhtSensor` / `IrController` | Peripheral composition |
| `RelayNodeApp` / `HybridNodeApp` / `IrNodeApp` / `GatewayApp` | App concrete |
| `NodeRegistry` / `OfflineCache` / `BackendClient` / `WifiProvisioner` / `GatewayWebServer` / `GatewayHtml` | Gateway only |

**Cấu trúc:** `common/` + `Home/{gateway,node_role,node_role-nhietdo,node_hongngoai}/` — mỗi sketch chỉ file trong folder (Arduino không `../../common`).

**Checklist code:**
- Không global nghiệp vụ (chỉ static callback thunk)
- Packet binary-compatible 100%
- `.ino` chỉ tạo App + begin/loop
- ESP8266 header-only + 1 `.cpp` static; không FreeRTOS
- Không `delay()` dài trong loop chính
- DHT11; IR LED active LOW GPIO8

---

## 5. Checklist thiết bị

| # | Hạng mục | Status |
|---|----------|--------|
| 1–5 | common + Relay + Hybrid + IR + Gateway OOP | ✅ |

---

## 6. Bài học / tránh lỗi (gộp phiên 5–6c)

### Gateway OOP (phiên 5)
- ESP-NOW **init trước** load peer / pull sync.
- HTML PROGMEM: **không UTF-8 BOM**.
- Static instance cho C-callback & WebServer routes; SRP từng class.
- `isRegisteredMac` trên đường recv — tránh lock nặng.

### Hybrid sensor không hiện UI (phiên 6–6c) — **quan trọng**

| Hiện tượng | Nguyên nhân | Fix / vận hành |
|------------|-------------|----------------|
| UI không T/H, relay vẫn OK | `xQueueSendFromISR` trong callback WiFi **task** (không phải ISR) → queue hỏng | Dùng `xQueueSend` khi không trong ISR |
| Registry RAM ≠ Flash | Update sensor theo MAC fail → gói bị coi Discovery | `ensureRegistered()` rehydrate từ Preferences |
| Node `ok=1` nhưng GW im | `ok=1` chỉ = radio đã TX, **không** = GW nhận | Bật log `[GATEWAY RX]` / `[ESPNOW TX]` |
| TX chủ động OK, heartbeat rơi | ESP32 modem sleep | `WiFi.setSleep(false)` + `WIFI_PS_NONE` sau connect / ESP-NOW / pullConfig |
| Node ch2, GW ch1 | ESP-NOW **cùng channel** bắt buộc | Node chỉ lock khi cmd GW→Node; soft→bcast→rescan; kênh = kênh router |
| TX ok unicast, GW chỉ RX lúc BCAST | ESP8266→ESP32 **unicast Node→GW fail** | Hybrid uplink **luôn BROADCAST**; GW ACK `0x03` |
| MAC lạ / type sai | Đăng ký web ≠ MAC thật; type=1 thay vì 4 | Match MAC thật; Hybrid = type **4** |

**Debug nhanh:** Serial GW có `[GATEWAY RX] cmd=0x03 type=4 T=… H=…`? Sai size → wire lệch; Thiết bị lạ → MAC; không RX → sleep/channel.

**Hybrid thêm:** `ensureGatewayPeer()` trước mỗi TX (ESP8266); mutex registry timeout 200ms.

---

## 7. Nhật ký phiên (mới nhất trên; gộp cũ)

### 2026-07-13 — Kế hoạch đồng bộ Gateway ↔ Backend (SmartHome + nexushome-web)

**Luồng tổng:**
```
Node ESP-NOW → Gateway (local) → HTTP → SmartHome FastAPI → (command) → Gateway HTTP API
                                      ↑
                              nexushome-web (Bearer JWT)
```

| # | Phase | Việc | Status |
|---|-------|------|--------|
| 1 | Schema/route | `register_from_gateway` + GatewayDeviceRegister | ✅ |
| 2 | Control | BE → `POST /api/control` {node_id, command 1|2} | ✅ |
| 3 | BackendClient GW | BASE_URL, MAC lower, đúng path | ✅ |
| 4 | Telemetry | MAC norm + Event TURN_ON/OFF / IR_LEARNED | ✅ |
| 5 | IR | parse array hoặc `{nodes}` | ✅ |
| 6 | Web | DeviceApi baseURL env | ⏳ |

**Đã fix lệch:**
- Register GW: `/api/devices/register_from_gateway` (no JWT), field `type`|`device_type`.
- Control: `/api/control` + `command` 1/2 (không còn `/control` + status).
- IR sync: chấp nhận mảng JSON từ GW.
- MAC: normalize lowercase colon.

### 2026-07-13 — Relay: GPIO0 + scan kênh = Hybrid
- **GPIO:** `RELAY_PIN` **2→0** (GPIO2 chỉ LED ESP-01 — đó là lý do “không bật/tắt”).
- **EspNowManager:** copy từ Hybrid (scan 1→13, BCAST uplink, soft 15s / rescan 40s).
- **Nạp:** `node_role`. Boot: `GPIO0`; `[WIFI] Khóa kênh…` trùng GW.

### 2026-07-13 — UI web: MAC trùng + thời gian Bật/Tắt
- **Lag / double-click:** disable nút khi đang POST; server `findStoredMacId` từ chối MAC trùng.
- **Thời gian:** `status_changed_ms` khi status đổi; API `status_changed_ago_s` / `last_seen_ago_s` (không dùng Date.now−millis).
- UI relay: «Bật · X phút trước» / «Tắt · …»; dòng phụ «Cập nhật · …».
- **Nạp:** `gateway`.

### 2026-07-13 — Node Relay bật/tắt (căn Hybrid)
- **Uplink BCAST** + peer **unicast GW** khi lock (RX lệnh 0x01/02 ổn hơn trên ESP8266).
- **App:** giống Hybrid (ID sync trước ACK filter; soft 35s / rescan 70s — ít giật kênh).
- **GW:** `sendControl` gửi **2 lần** + log `[CTRL TX]`; set `status` mirror.
- **GPIO:** pin 2; `RELAY_ACTIVE_LOW=1` nếu module active-low.
- **Nạp:** `node_role` + `gateway`. Serial node phải có `[NHẬN LỆNH] cmd=0x01/02`.

### 2026-07-13 — IR: store 100% / không kéo gain xung lúc phát
- **Học:** raw **100%**. **Phát:** `IR_BLAST_GAIN=100` (gain 125+ → không bật/tắt được).
- Duty **60%** (nhẹ hơn 50% mặc định sendRaw, an toàn hơn 80%).
- Mạnh khoảng cách → **HW** (5V, 2 LED, transistor), không tăng gain xung.
- Nạp `node_hongngoai`.

### 2026-07-13 — IR learn timeout ngay 0ms (bug)
- **Log:** `Bật LED...` và `Hết 30s` cùng timestamp → không chờ remote.
- **Nguyên nhân:** `learning_=true` rồi `delay(50)` trong callback ESP-NOW; `learnStartTime_` gán sau → `poll()` task chính thấy `millis()-0 >= 30s` (uptime >30s) hoặc timestamp cũ → hủy ngay.
- **Fix:** gán `learnStartTime_=millis()` **trước** `learning_=true`; bỏ `delay` trong `startLearning`; clear `learnStartTime_` khi cancel/save.
- **Nạp:** `node_hongngoai`.

### 2026-07-13 — IR: gửi 0x10 x3 + khuếch đại/nhạy thu
- **TX ESP-NOW:** học xong gửi `0x10` **3 lần** (gap 80ms); GW dedupe (chỉ lần đầu khi `is_learning`).
- **Thu nhạy hơn:** buf 1536, timeout 50ms, tolerance 40%, min rawlen 18, noise guard 250ms.
- **Gain raw 110%** khi ghi xung (bù suy hao); blast raw 2 lần. (`setDuty` không có trên IRremoteESP8266 2.9)
- **Nạp:** `node_hongngoai`. Tùy chỉnh: `IR_RAW_GAIN_PERCENT`, `IR_LEARN_TX_REPEAT`.

### 2026-07-12 — IR learn: form «mã mới» lặp + học liên tục
- **Triệu chứng:** Sau Lưu, UI lại «Đã thu được mã IR mới»; cảm giác học bật mãi.
- **Nguyên nhân:**
  1. GW ghi `last_ir_data` với **mọi** `0x10` (kể cả gói 2/queue trễ sau clear).
  2. Node gửi **2×** `0x10` + passive IR cũng gửi `0x10`.
  3. `clearIrLearnUi` chỉ xóa khi `device_type==IR` (dễ miss).
  4. Không timeout phiên học (LED/learn kẹt).
- **Fix:** GW chỉ nhận `0x10` khi `is_learning`; clear theo `node_id`; node chỉ gửi `0x10` 1 lần khi học xong; passive không uplink; timeout 30s; save → cooldown.
- **Nạp:** `gateway` + `node_hongngoai`.

### 2026-07-12 — Node IR audit + fix giống Hybrid
- **Chip:** ESP32-C3 (không phải ESP8266) — unicast C3→S3 *có thể* OK hơn 8266, nhưng code cũ **cùng pattern hỏng**:
  1. Sau lock: TX unicast + **xóa peer BCAST**
  2. Heartbeat 30s / IR event `0x10` / learn report đều unicast
  3. Không track `lastGatewayRx`, không soft/full rescan khi lệch kênh
  4. Không tắt modem sleep (RX lệnh LEARN/BLAST dễ rơi)
  5. `handleRecv` lock mọi gói (không lọc GW→Node)
- **Fix:** uplink **BROADCAST**; sleep OFF; soft 45s / rescan 90s; filter cmd GW→Node; log TX `dest=BCAST`.
- **Nạp:** `node_hongngoai`. Gateway đã ACK `0x03` (bản trước).
- **Kiểm tra:** GW có `[GATEWAY RX] cmd=0x03 type=2` mỗi ~30s; học IR → `cmd=0x10`; Serial IR `dest=BCAST`.

### 2026-07-12 — UI web tiếng Việt có dấu
- **Làm:** Viết lại `GatewayHtml.h` UTF-8 (không BOM); font **Be Vietnam Pro**; meta charset + `Content-Type: text/html; charset=utf-8` / JSON tương tự trong `GatewayWebServer.h`.
- **Vì sao:** File cũ mojibake (`CĂ i Ä‘áº·t…`) do double-encoding; Inter kém hơn cho tiếng Việt; thiếu charset trên response.
- **Tránh:** Không lưu HTML PROGMEM bằng ANSI/Windows-1252; **không thêm UTF-8 BOM** (Arduino/ESP có thể lệch). Nạp lại `gateway`.

### 2026-07-12 — Hybrid uplink BROADCAST (root cause thật)
- **Log chứng minh:** GW chỉ RX `cmd=0x00` mỗi ~24s (lúc fallback BCAST). Unicast `0x03`/`0x00` → `DC:B4:…` **không bao giờ** có `[GATEWAY RX]`. Sau BCAST, node nhận ACK (GW→Node unicast OK).
- **Kết luận:** ESP8266→ESP32 **unicast Node→GW hỏng**; **broadcast OK**; **unicast GW→Node OK**.
- **Fix Hybrid:** `sendToGateway` luôn dest `FF:FF:FF:FF:FF:FF`; `gatewayMac_` chỉ lock từ ACK (log/RX); không xóa peer BCAST khi lock.
- **Fix Gateway (giữ):** ACK mọi `0x03`; peer channel=0; sleep OFF sau HTTP.
- **Nạp:** `node_role-nhietdo` (bắt buộc) + `gateway` (ACK report).
- **Kỳ vọng:** GW mỗi ~5s `[GATEWAY RX] cmd=0x03 type=4`; node `last RX` < 6s; log TX `dest=BCAST`.

### 2026-07-12 — Hybrid report 5s + notes gọn
- Interval 5s; bài học queue/sleep/channel/BCAST uplink.
