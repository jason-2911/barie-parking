# Parking Client

Client Qt6 cho hệ thống quản lý bãi xe SmartPark. Chạy trên:
- **Orange Pi 4 LTS / Ubuntu** — phần cứng thật (barrier Modbus RS485, đầu đọc tiền ITL NV9, cảm biến xe E3F-DS10C4).
- **macOS** — chế độ demo/dev. I/O phần cứng được stub tự động qua `#ifdef __APPLE__`; luồng MQTT + HTTPS chạy thật (kết nối broker mosquitto local và demo server đi kèm).

## Kiến trúc

```
src/
├── app/
│   ├── Application.cpp          bộ điều phối trung tâm
│   ├── CommandQueue.cpp         FIFO cho lệnh server → barrier
│   ├── DeviceId.cpp             tự nhận diện hardware id (machine-id / IOPlatformUUID)
│   └── Config.cpp
├── types/                       struct kiểu tin nhắn (schema JSON dùng chung)
├── mqtt/                        MqttClient (libmosquitto) + topics / builder / parser
├── hardware/
│   ├── IBarrierController / INV9Validator / IVehicleSensor  interface trừu tượng
│   ├── BarrierManager.cpp       FSM per-barrier (open → chờ xe → đóng)
│   ├── VehicleSensorManager.cpp wrapper Qt cho cảm biến GPIO
│   ├── NV9Manager.cpp / AudioPlayer.cpp
│   ├── HardwareFactory.cpp      chọn driver theo platform
│   └── drivers/
│       ├── BarrierControllerModbus.cpp   thật trên Linux / stub trên __APPLE__
│       ├── NV9ValidatorSSP.cpp           thật trên Linux / stub trên __APPLE__
│       └── VehicleSensorGpiod.cpp        thật trên Linux (libgpiod v2) / stub trên __APPLE__
└── ui/                          MainWindow + mỗi màn hình / widget một class
```

## Luồng nghiệp vụ

### Ra cổng vé lượt (khách thanh toán tại kiosk)

1. Server gửi `vehicle_info` → thiết bị hiển thị màn hình thanh toán vé lượt, khởi động `payment_timer_` (tối thiểu 5 phút).
2. Khách thanh toán theo một trong hai cách:

   **Quét mã QR:**
   - Khách mở app ngân hàng / ví điện tử → quét mã VietQR trên màn hình.
   - App gọi callback webhook về server.
   - Server kiểm tra số tiền — nếu đủ → gửi `payment_success` xuống thiết bị (kèm `barrier_ids` cần mở).
   - Thiết bị chuyển sang màn hình thành công và mở cổng.

   **Nộp tiền mặt:**
   - NV9 báo sự kiện `cashInserted` cho thiết bị sau mỗi tờ tiền.
   - Thiết bị cộng dồn; khi `amount_paid ≥ amount_due` → publish `payment` lên server để báo hết chờ, đồng thời tắt đầu đọc tiền.
   - Server nhận `payment` → gửi `payment_success` xuống thiết bị.
   - Thiết bị chuyển màn hình thành công và mở cổng.

3. `payment_success` enqueue lệnh mở cho **tất cả barrier** trong `barrier_ids` theo thứ tự.
4. `CommandQueue` dequeue lần lượt → `BarrierManager::requestOpen` → cần barie nâng lên.
5. Cảm biến E3F-DS10C4 phát hiện xe vào vùng → FSM chuyển `WaitingVehicle → Passing`.
6. Xe qua hết → FSM chuyển sang `ClearDelay`, chạy timer `vehicle_sensor.clear_delay_ms`.
7. Timer kích → `BarrierManager::rawLower()` → tín hiệu `autoClosed` → `Application::onBarrierAutoClosed` reset session và về idle.

### Hết giờ thanh toán (≥ 5 phút)

Nếu không có tiền mặt hoặc QR trong `ui.payment_timeout_seconds`:

1. Thiết bị publish `session_abandoned` trên `…/event` để server giải phóng session.
2. Thiết bị vô hiệu hóa NV9 (từ chối tiền lẻ), phát âm thanh lỗi, hiển thị thông báo toàn màn hình.
3. Khách bấm **GỌI HỖ TRỢ** → thiết bị publish `support_request` với `reason="payment_timeout"`.
4. Ops nhận cảnh báo, bảo vệ đến thu tiền, ops gửi `open_cmd` với `reason="manual_override"`.
5. Tiếp tục từ bước 4 của luồng ra cổng bình thường.

### Mở lại khi đang đóng

Nếu `open_cmd` đến khi barrier FSM đang ở trạng thái `Lowering`, state machine hủy lệnh hạ và nâng lại — tránh cần đập vào xe đang đi sau.

### Server restart → reconnect broadcast

Khi server (khởi động lại), server publish tin nhắn `reconnect` trên `parking/v1/{loc}/broadcast/cmd`. Mọi thiết bị đăng ký topic đó sẽ phản hồi bằng cách publish lại payload `connect` của mình, giúp server xây dựng lại state ngay lập tức thay vì phải đợi LWT timeout phát hiện thiết bị offline.

## Khởi động nhanh trên macOS

### Cài đặt phụ thuộc

```bash
brew install qt qrencode mosquitto cjson
```

### Khởi động broker

```bash
brew services start mosquitto   # hoặc: /opt/homebrew/opt/mosquitto/sbin/mosquitto -v
```

Thêm user `parking` vào `/opt/homebrew/etc/mosquitto/mosquitto.conf` hoặc dùng `allow_anonymous true` để test.

### Build client

```bash
mkdir build && cd build
cmake ..
make -j$(sysctl -n hw.ncpu)
```

### Chạy tất cả

Terminal 1 — build:
```bash
mkdir -p build && cd build
cmake ..
make -j$(sysctl -n hw.ncpu)
```

Terminal 2 — client (Qt UI, self-test hooks bật sẵn):
```bash
cd build
./parking_client --config ../config.selftest.json
```

Terminal 3 — server C++ tự chạy full suite:
```bash
cd build
./parking_test_server --config ../config.selftest.json
```

`parking_test_server` là server C++ mới để test đồng bộ với client qua MQTT. Nó tự:
- chờ client `connect`
- broadcast `reconnect`
- chạy lần lượt từng case có delay + logger
- tự gửi `vehicle_info`, `qr_payment_confirmed`, `payment_success`, `open_cmd`, `display_msg`
- chờ các event ngược từ client như `payment`, `session_abandoned`, `support_request`

Python demo server cũ vẫn còn ở `server/server.py` cho luồng HTTPS QR/VietQR thủ công, nhưng không còn là đường test mặc định.

Self-test cases mặc định:
- reconnect broadcast
- hourly QR flow
- hourly cash flow
- payment timeout → `session_abandoned` → `support_request` → manual override
- monthly auto-open
- display message

## Linux (Orange Pi) — production

```bash
sudo apt install cmake qt6-base-dev qt6-multimedia-dev \
                 libqrencode-dev libmosquitto-dev libgpiod-dev
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
sudo make install
```

Trên Linux các class driver biên dịch với I/O serial termios/select thật và sự kiện edge libgpiod thật (xem các nhánh `#ifndef __APPLE__`). Điền các chuỗi lệnh SSP vào `NV9ValidatorSSP.cpp` trước khi đưa vào production.

## Cấu hình (`config.json`)

| Trường | Mục đích |
|---|---|
| `device.id` | ID thiết bị duy nhất (bị ghi đè khi khởi động nếu `auto_id: true`) |
| `device.auto_id` | `true` → tự nhận diện `/etc/machine-id` (Linux) hoặc `IOPlatformUUID` (macOS) |
| `device.id_prefix` | Tiền tố gắn vào ID tự nhận diện (ví dụ `"pk-"`) |
| `device.type` | `entry_only` / `exit_only` / `full` — lái conditional init (xem phần Device Profiles) |
| `mqtt.host` / `mqtt.port` | Địa chỉ broker (`localhost` cho demo) |
| `mqtt.keepalive_seconds` | Tần suất `PINGREQ` của MQTT (mặc định **10s**). LWT fire sau ~1.5× giá trị này khi mất kết nối |
| `barriers[].direction` | `entry` hoặc `exit` — còn là role trong profile `full` |
| `barriers[].modbus_addr` | Địa chỉ Modbus slave của controller barrier |
| `barriers[].auto_close_seconds` | Fallback an toàn nếu cảm biến xe không kích hoạt |
| `serial.*_port` | `/dev/ttyUSB*` trên Linux; bỏ qua trên macOS (stub) |
| `vehicle_sensor.enabled` | Bật/tắt cảm biến E3F-DS10C4 |
| `vehicle_sensor.chip_path` | Ví dụ `/dev/gpiochip0` |
| `vehicle_sensor.line_offset` | Chân BCM kết nối với OUT của cảm biến (dây đen) |
| `vehicle_sensor.active_low` | `true` cho NPN-NO (mặc định cho cảm biến này) |
| `vehicle_sensor.bias_pull_up` | `true` → dùng pull-up nội của SoC |
| `vehicle_sensor.debounce_us` | Debounce mức kernel; 20 000 µs phù hợp cho xe ô tô |
| `vehicle_sensor.clear_delay_ms` | Chờ sau khi xe qua trước khi hạ cần |
| `ui.payment_timeout_seconds` | ≥ 300; hard-floor xuống 300 có cảnh báo nếu đặt thấp hơn |
| `ui.payment_success_countdown_seconds` | Countdown idle trên màn hình thành công khi server không đặt `auto_idle_seconds`; floor 5 giây (mặc định 15) |

### Self-test config

`config.selftest.json` thêm block `self_test` để máy mới có thể chạy full flow không cần phần cứng thật:

| Trường | Mục đích |
|---|---|
| `self_test.enabled` | Bật hook test tự động trong client |
| `self_test.auto_vehicle_sensor` | Tự giả lập xe đi qua sau khi barrier mở |
| `self_test.auto_cash_insert_delay_ms` | Delay trước khi client tự bơm tiền mặt |
| `self_test.auto_call_support` | Tự phát `support_request` sau case timeout |
| `self_test.allow_short_payment_timeout` | Cho phép timeout ngắn hơn 300 giây chỉ cho self-test |

## Bảng tin MQTT

### Quy ước đặt tên topic

```
parking/v1/client/{dev}/<suffix>   ← thiết bị publish, server subscribe
parking/v1/server/{dev}/cmd        ← server publish, thiết bị subscribe
parking/v1/broadcast/cmd           ← server broadcast, mọi thiết bị subscribe
```

`{dev}` = `device_id` (ví dụ `pk_abc123def456789a`).

**Quy ước đặt tên:**
- `device_id` — tự ghép từ `id_prefix` + hex hardware id, dạng `pk_<hex16>` (ví dụ `pk_abc123def456789a`)
- `barrier_id` — tự ghép tại runtime: `{device_id}_01`, `{device_id}_02` — config chỉ cần khai suffix `"01"`, `"02"`

### Tổng quan topic

| Topic | QoS | Retain | Ghi chú |
|---|---|---|---|
| `parking/v1/client/{dev}/connect`    | 1 | ✓ | Retain để server vừa khởi động thấy thiết bị ngay |
| `parking/v1/client/{dev}/disconnect` | 1 | ✓ | Graceful shutdown **và** LWT (xem phần Presence) |
| `parking/v1/client/{dev}/payment`    | 1 | — | Thiết bị báo đã thu đủ tiền mặt, yêu cầu server xác nhận |
| `parking/v1/client/{dev}/event`      | 1 | — | Sự kiện bất thường: `session_abandoned`, `support_request` |
| `parking/v1/server/{dev}/cmd`        | 1 | — | Lệnh từ server xuống thiết bị |
| `parking/v1/broadcast/cmd`           | 1 | — | Broadcast tới mọi thiết bị |

---

### Thiết bị → Server (`client/{dev}/…`)

---

#### `connect` — thiết bị đăng ký online

**Topic:** `parking/v1/client/{dev}/connect`  
**Khi nào:** lúc khởi động và khi nhận broadcast `reconnect`.

```json
{
  "type": "connect",
  "device_id": "pk_abc123def456789a",
  "timestamp": "2026-04-20T08:00:00",
  "firmware_version": "1.0.0",
  "has_cash_reader": true,
  "has_display": true,
  "barriers": [
    { "barrier_id": "pk_abc123def456789a_01", "direction": "exit", "modbus_addr": 1 }
  ]
}
```

---

#### `disconnect` — thiết bị offline (hai lối)

**Topic:** `parking/v1/client/{dev}/disconnect` (retain=true, QoS 1)

Cùng một topic phục vụ **hai** kịch bản — phân biệt qua field `reason`:

- `reason: "graceful"` — thiết bị publish ngay trước khi `mosquitto_disconnect()`
  (systemd stop, OTA, reboot có chủ đích).
- `reason: "lwt"` — **broker tự publish** nếu thiết bị không gửi `DISCONNECT`
  và cũng không trả lời `PINGREQ` trong khoảng `1.5 × keepalive_seconds`.
  Với `keepalive_seconds = 10`, server phát hiện thiết bị chết trong **≤15 s**.

Không có ping telemetry tầng ứng dụng — presence hoàn toàn dựa vào MQTT
keepalive + LWT. Nếu cần thống kê CPU/RAM/nhiệt độ gửi định kỳ, thêm kênh
riêng ở phiên bản sau.

```json
{
  "type": "disconnect",
  "device_id": "pk_abc123def456789a",
  "reason": "graceful",
  "timestamp": "2026-04-20T22:00:00"
}
```

Server nên xử lý `reason == "lwt"` như crash / mất điện — mark thiết bị
offline, release session đang treo, gửi cảnh báo cho ops.

---

#### `payment` — thu đủ tiền mặt, yêu cầu xác nhận

**Topic:** `parking/v1/client/{dev}/payment`  
**Khi nào:** NV9 nhận đủ tiền (`amount_paid ≥ amount_due`). Thiết bị gửi để báo server ngừng chờ; server trả `payment_success` để thiết bị mở cổng và chuyển màn hình thành công.

```json
{
  "type": "payment",
  "device_id": "pk_abc123def456789a",
  "barrier_id": "pk_abc123def456789a_01",
  "session_id": "sess_20260420_103000_30K25044",
  "plate": "30K-250.44",
  "method": "cash",
  "amount_due": 10000,
  "amount_paid": 10000,
  "amount_change": 0,
  "timestamp": "2026-04-20T10:35:00"
}
```

---

#### `event` — sự kiện bất thường

**Topic:** `parking/v1/client/{dev}/event`

**`session_abandoned`** — hết giờ thanh toán, thiết bị báo server ngừng chờ:

```json
{
  "type": "session_abandoned",
  "device_id": "pk_abc123def456789a",
  "barrier_id": "pk_abc123def456789a_01",
  "session_id": "sess_20260420_103000_30K25044",
  "plate": "30K-250.44",
  "amount_due": 10000,
  "amount_paid": 0,
  "timestamp": "2026-04-20T10:40:00"
}
```

**`support_request`** — khách bấm nút GỌI HỖ TRỢ:

```json
{
  "type": "support_request",
  "device_id": "pk_abc123def456789a",
  "barrier_id": "pk_abc123def456789a_01",
  "session_id": "sess_20260420_103000_30K25044",
  "plate": "30K-250.44",
  "reason": "payment_timeout",
  "timestamp": "2026-04-20T10:41:00"
}
```

`reason` ∈ `payment_timeout` | `user_request` | `idle_press`

---

### Server → Thiết bị (`server/{dev}/cmd`)

Tất cả lệnh server publish lên `parking/v1/server/{dev}/cmd`, phân biệt bằng trường `"type"`.

---

#### `vehicle_info` — thông tin xe vào/ra

**Khi nào:** server nhận sự kiện xe đến từ camera/LPR → gửi xuống thiết bị để hiển thị màn hình thanh toán.

**Vé lượt (hourly):**

```json
{
  "type": "vehicle_info",
  "session_id": "sess_20260420_103000_30K25044",
  "barrier_id": "pk_abc123def456789a_01",
  "direction": "exit",
  "plate": "30K-250.44",
  "ticket_type": "hourly",
  "entry_time": "2026-04-20T08:13:00",
  "exit_time": "2026-04-20T10:30:00",
  "duration_minutes": 137,
  "amount_due": 10000,
  "currency": "VND",
  "payment_methods": ["cash", "qr"],
  "qr_url": "https://192.168.1.100:8443/qr/sess_20260420_103000_30K25044.png",
  "qr_expires_at": "2026-04-20T10:35:00"
}
```

**Vé tháng (monthly):**

```json
{
  "type": "vehicle_info",
  "session_id": "sess_20260420_103500_30K12345",
  "barrier_id": "pk_abc123def456789a_01",
  "direction": "exit",
  "plate": "30K-123.45",
  "ticket_type": "monthly",
  "owner_name": "Nguyễn Văn A",
  "valid_until": "2026-05-31",
  "amount_due": 0,
  "payment_methods": [],
  "open": true
}
```

`open: true` → thiết bị tự enqueue mở barrier ngay, không cần thanh toán.

---

#### `open_cmd` — lệnh mở barrier

**Khi nào:** bảo vệ thao tác manual override trên dashboard, hoặc server gửi thêm barrier cho vé tháng nhiều làn.

```json
{
  "type": "open_cmd",
  "barrier_id": "pk_abc123def456789a_02",
  "session_id": "sess_20260420_103500_30K12345",
  "reason": "manual_override",
  "play_sound": true,
  "sound_file": "open_exit.wav",
  "auto_close_seconds": 10
}
```

`reason` ∈ `payment_confirmed` | `monthly_ticket` | `manual_override` | `free_exit`

---

#### `payment_success` — xác nhận thanh toán, mở cổng

**Khi nào:**
- Server nhận callback QR từ ngân hàng/ví điện tử → gửi ngay xuống thiết bị.
- Server nhận `payment` (tiền mặt) từ thiết bị → gửi xác nhận lại.

Thiết bị dùng tin này để **chuyển sang màn hình thành công** và **mở tất cả barrier** trong `barrier_ids` theo thứ tự. `barrier_ids` chỉ được chứa barrier thuộc thiết bị đang nhận — thiết bị sẽ bỏ qua bất kỳ barrier_id nào không có trong config của nó.

```json
{
  "type": "payment_success",
  "session_id": "sess_20260420_103000_30K25044",
  "barrier_ids": ["pk_abc123def456789a_01"],
  "plate": "30K-250.44",
  "status": "success",
  "auto_idle_seconds": 15,
  "timestamp": "2026-04-20T10:35:01"
}
```

`auto_idle_seconds = 0` → thiết bị dùng giá trị mặc định từ `config.json`.

---

#### `display_msg` — hiển thị thông báo tuỳ chỉnh

**Khi nào:** ops muốn hiển thị thông báo trực tiếp lên màn hình thiết bị (ví dụ: bãi đầy, sự cố...).

```json
{
  "type": "display_msg",
  "barrier_id": "pk_abc123def456789a_01",
  "message": "Bãi xe đang bảo trì. Xin lỗi vì sự bất tiện.",
  "duration_seconds": 30,
  "play_sound": true,
  "sound_text": "Bãi xe đang bảo trì"
}
```

---

#### `reconnect` — yêu cầu thiết bị re-announce

**Topic:** `parking/v1/broadcast/cmd` (broadcast tới tất cả thiết bị)  
**Khi nào:** server khởi động lại và cần rebuild state.

```json
{
  "type": "reconnect",
  "reason": "server_restart",
  "timestamp": "2026-04-20T09:00:00"
}
```

Thiết bị nhận tin này → publish lại `connect` ngay lập tức.

---

> **Session nhiều barrier**: một session có thể điều khiển nhiều barrier vật lý (ví dụ vào + ra trong làn song song). Luôn gửi `barrier_ids` là mảng trong `payment_success`. Với `vehicle_info` (vé tháng), thiết bị tự mở barrier chính qua cờ `open: true`; server gửi `open_cmd` riêng cho các barrier bổ sung.

## HTTPS endpoint QR

Demo server cũng chạy một HTTPS file server (`ThreadingHTTPServer` + `ssl`) để thiết bị tải ảnh VietQR đã render sẵn.

| Method | Đường dẫn | Phản hồi | Ghi chú |
|---|---|---|---|
| `GET` | `/health` | `{"ok": true, "ts": …}` | Liveness probe |
| `GET` | `/qr/<session_id>.png` | `image/png` | Tạo lần đầu qua VietQR (`img.vietqr.io`) và cache trên đĩa (`$TMPDIR/smartpark_qr_cache/`) |

- Tài khoản VietQR được khai báo ở đầu `server.py` (`VIETQR_BIN`, `VIETQR_ACCOUNT`, `VIETQR_ACCOUNT_NM`, `VIETQR_TEMPLATE`).
- `qr_url` trong `vehicle_info` trỏ đến endpoint này; `QRWidget` tải PNG qua `QNetworkAccessManager`, fallback sang render local `qrencode` cho chuỗi không phải HTTP.
- Cert tự ký được tạo lần đầu vào `server/certs/`; thiết bị bỏ qua xác minh TLS trong chế độ demo. **Chuyển sang cert CA-pinned trước khi production.**

## Thêm loại thiết bị mới

1. Thêm interface trong `src/hardware/` (ví dụ `INewSensor.h`).
2. Thêm driver trong `src/hardware/drivers/` implement interface đó — theo mẫu `#ifdef __APPLE__` của các driver hiện có.
3. Thêm manager (wrapper Qt) trong `src/hardware/`.
4. Mở rộng `HardwareFactory.cpp`, `Config.*`, và `Application.cpp`.
5. Bổ sung block mới vào `config.json`.
