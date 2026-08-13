# Hướng dẫn lắp ráp & vận hành thiết bị Kiosk Bãi xe

## Mục lục

1. [Danh sách linh kiện](#1-danh-sách-linh-kiện)
2. [Sơ đồ kết nối tổng quan](#2-sơ-đồ-kết-nối-tổng-quan)
3. [Kết nối từng thiết bị](#3-kết-nối-từng-thiết-bị)
   - 3.1 [Đầu đọc tiền NV9 USB](#31-đầu-đọc-tiền-nv9-usb)
   - 3.2 [Bộ điều khiển barie (Modbus RS485)](#32-bộ-điều-khiển-barie-modbus-rs485)
   - 3.3 [Loa âm thanh](#33-loa-âm-thanh)
   - 3.4 [Cảm biến phát hiện xe (E3F-DS10C4)](#34-cảm-biến-phát-hiện-xe-e3f-ds10c4)
   - 3.5 [Màn hình HDMI](#35-màn-hình-hdmi)
   - 3.6 [Kết nối mạng Ethernet](#36-kết-nối-mạng-ethernet)
4. [Cài đặt phần mềm](#4-cài-đặt-phần-mềm)
5. [Cấu hình thiết bị (config.json)](#5-cấu-hình-thiết-bị-configjson)
6. [Giao thức truyền nhận bản tin MQTT](#6-giao-thức-truyền-nhận-bản-tin-mqtt)
7. [Kiểm tra vận hành](#7-kiểm-tra-vận-hành)

---

## 1. Danh sách linh kiện

| STT | Linh kiện | Số lượng | Ghi chú |
|-----|-----------|----------|---------|
| 1 | Orange Pi 4 LTS | 1 | Board chính, chạy Ubuntu |
| 2 | Đầu đọc tiền ITL NV9 USB | 1 | Kết nối USB |
| 3 | Bộ điều khiển barie (Modbus RTU) | 1 | Kết nối RS485 qua adapter CH340 |
| 4 | Adapter USB-RS485 CH340 | 1 | Chuyển đổi USB ↔ RS485 |
| 5 | Loa khuếch đại | 1–2 | Jack 3.5mm + nguồn USB |
| 6 | Cảm biến hồng ngoại E3F-DS10C4 | 1 | NPN-NO, 3 dây, kết nối GPIO |
| 7 | Màn hình | 1 | HDMI, khuyến nghị 1440×900 trở lên |
| 8 | Dây cáp Ethernet Cat5e/Cat6 | 1 | Kết nối mạng LAN |
| 9 | Cáp USB type-A to type-B / micro USB | 2 | Cho NV9 và CH340 |
| 10 | Dây tín hiệu RS485 (2 lõi xoắn) | 1 cuộn | Nối CH340 với bộ điều khiển barie |
| 11 | Dây 3 lõi 24AWG | 1 m | Nối cảm biến E3F với GPIO |
| 12 | Nguồn 12V DC | 1 | Cấp cho cảm biến E3F nếu cần |

---

## 2. Sơ đồ kết nối tổng quan

```
                        ┌─────────────────────────────┐
                        │       Orange Pi 4 LTS        │
                        │                             │
  NV9 USB ──────────────┤ USB port 1 (ttyACM0)        │
                        │                             │
  CH340 RS485 ──────────┤ USB port 2 (ttyUSB0)        │
     │                  │                             │
     └── A/B dây ───────┤ → Bộ điều khiển barie       │
                        │                             │
  Loa ─────────────────┤ Jack 3.5mm (audio out)      │
  (nguồn USB) ──────────┤ USB port 3 (5V power)       │
                        │                             │
  Cảm biến E3F ─────────┤ GPIO17 (gpiochip0 line 17)  │
  (dây OUT màu đen)     │ + GND + 3.3V/5V             │
                        │                             │
  Màn hình ─────────────┤ HDMI                        │
                        │                             │
  Server/Router ─────────┤ Ethernet (eth0)             │
                        └─────────────────────────────┘
```

---

## 3. Kết nối từng thiết bị

### 3.1 Đầu đọc tiền NV9 USB

NV9 giao tiếp qua chuẩn **USB Full-Speed** (eSSP protocol). Bên trong connector USB có 4 chân:

| Chân USB | Ký hiệu | Màu dây chuẩn |
|----------|---------|---------------|
| 1 | V+ (5V) | Đỏ |
| 2 | D− | Trắng |
| 3 | D+ | Xanh lá |
| 4 | GND | Đen |

**Cách lắp:**

1. Cắm cáp USB từ cổng USB của NV9 vào **USB port trống** trên Orange Pi.
2. Sau khi cắm, kiểm tra thiết bị đã được nhận diện:

```bash
lsusb | grep -i "ITL\|NV9\|1503"
ls /dev/ttyACM*
```

3. Nếu thấy `/dev/ttyACM0` xuất hiện → NV9 đã được nhận diện thành công.
4. Cấp quyền truy cập serial cho user:

```bash
sudo usermod -aG dialout orangepi
# Đăng xuất và đăng nhập lại để quyền có hiệu lực
```

**Ghi chú:** NV9 sử dụng giao thức eSSP (Encrypted Smiley Secure Protocol) ở **9600 baud, 8N1**. Driver trong phần mềm đã xử lý toàn bộ handshake SYNC → SETUP_REQUEST → CH_INHIBITS → ENABLE khi khởi động.

---

### 3.2 Bộ điều khiển barie (Modbus RS485)

Barie giao tiếp qua **Modbus RTU RS485**, kết nối vật lý qua adapter **USB-RS485 CH340**.

#### Sơ đồ đấu dây RS485

```
  Orange Pi USB ──── Adapter CH340 USB-RS485 ──── Dây xoắn ──── Bộ điều khiển barie
                         │                                              │
                       A (D+) ────────────────────────────────────── A (D+)
                       B (D−) ────────────────────────────────────── B (D−)
                       GND   ────────────────────────────────────── GND
```

**Nguyên tắc đấu dây:**

| CH340 | Bộ điều khiển barie | Ghi chú |
|-------|---------------------|---------|
| A (D+) | A (D+) | Đấu A với A |
| B (D−) | B (D−) | Đấu B với B |
| GND | GND | Đất chung — **bắt buộc** để tránh nhiễu |

> ⚠️ **Quan trọng:** Nếu bus RS485 có nhiều thiết bị hoặc cáp dài trên 10m, cần gắn điện trở kết cuối (termination resistor) **120Ω** giữa A và B ở đầu cuối dây.

**Cách lắp:**

1. Đấu dây A-A, B-B, GND-GND theo bảng trên.
2. Cắm USB của CH340 vào Orange Pi.
3. Kiểm tra thiết bị:

```bash
ls /dev/ttyUSB*
# Kết quả mong đợi: /dev/ttyUSB0
```

4. Cấp quyền (nếu chưa làm ở bước NV9):

```bash
sudo usermod -aG dialout orangepi
```

5. Thiết lập địa chỉ Modbus slave trên bộ điều khiển barie qua DIP switch hoặc màn hình LCD của thiết bị — mặc định là **địa chỉ 1**. Ghi nhớ địa chỉ này để khai vào config.

**Thông số giao tiếp:** 19200 baud, 8N1, không parity, RTU mode.

---

### 3.3 Loa âm thanh

Loa sử dụng kết nối **jack 3.5mm stereo** cho tín hiệu âm thanh và **USB** để lấy nguồn 5V.

**Cách lắp:**

1. Cắm **jack 3.5mm** vào cổng audio out (headphone) của Orange Pi.
2. Cắm **cáp USB** của loa vào cổng USB trên Orange Pi để cấp nguồn.
3. Kiểm tra ALSA nhận thiết bị:

```bash
aplay -l
# Tìm dòng "card 0: ES8316" hoặc tên codec của Orange Pi 4 LTS
```

4. Cài file ALSA config để định tuyến âm thanh qua dmix (cho phép nhiều tiến trình dùng chung audio):

```bash
cp resources/asoundrc.example ~/.asoundrc
```

5. Kiểm tra âm thanh:

```bash
aplay -D default resources/sounds/open_exit.wav
```

6. Chỉnh âm lượng:

```bash
amixer -q sset Headphone 90%
```

> **Ghi chú:** Orange Pi 4 LTS dùng codec **ES8316** — control name là `Headphone`, không phải `Master`.

---

### 3.4 Cảm biến phát hiện xe (E3F-DS10C4)

Cảm biến hồng ngoại **E3F-DS10C4** dạng **NPN-NO** (thường hở, kéo xuống GND khi phát hiện vật cản), kết nối với **GPIO 17** của Orange Pi qua thư viện `libgpiod`.

#### Sơ đồ chân cảm biến E3F-DS10C4

```
  ┌─────────────────────────────────┐
  │        E3F-DS10C4               │
  │                                 │
  │  Dây NÂU  (VCC)  ─── 12V DC    │  ← Nguồn cấp cho cảm biến
  │  Dây XANH (GND)  ─── GND       │  ← Đất chung với Orange Pi
  │  Dây ĐEN  (OUT)  ─── GPIO17    │  ← Tín hiệu ra, NPN kéo xuống 0V
  └─────────────────────────────────┘
```

#### Đấu dây vào Orange Pi 4 LTS

| Dây cảm biến | Màu | Kết nối |
|---|---|---|
| VCC | Nâu | 12V DC từ nguồn ngoài (hoặc 5V nếu cảm biến hỗ trợ) |
| GND | Xanh lam | GND của Orange Pi (chân GND trên header GPIO) |
| OUT (signal) | Đen | **GPIO17** — chân số 11 trên header 40-pin Orange Pi |

> ⚠️ **Lưu ý điện áp:** Cảm biến E3F-DS10C4 thường cần nguồn **12V DC**. Dây OUT kéo xuống 0V khi có vật cản — tương thích với GPIO 3.3V của Orange Pi vì ngõ ra NPN chỉ kéo xuống GND, không đẩy điện áp lên. Khi không có vật cản, pull-up nội của SoC giữ mức HIGH.

#### Vị trí GPIO 17 trên header 40-pin Orange Pi 4 LTS

```
  Header 40-pin Orange Pi 4 LTS (nhìn từ trên, pin 1 ở góc trái trên)

   3.3V  [ 1][ 2]  5V
  GPIO2  [ 3][ 4]  5V
  GPIO3  [ 5][ 6]  GND     ← Nối GND cảm biến vào chân 6 (hoặc 9, 14, 20, 25...)
  GPIO4  [ 7][ 8]  GPIO14
    GND  [ 9][10]  GPIO15
 GPIO17  [11][12]  GPIO18  ← Nối dây ĐEN (OUT) cảm biến vào chân 11
  ...
```

#### Cài đặt quyền GPIO

```bash
# Thêm user vào group gpio
sudo usermod -aG gpio orangepi

# Kiểm tra quyền
ls -la /dev/gpiochip0
# Kết quả mong đợi: crw-rw---- 1 root gpio ...

# Đăng xuất và đăng nhập lại để quyền có hiệu lực
```

Nếu `/dev/gpiochip0` không thuộc group `gpio`, thêm udev rule:

```bash
sudo tee /etc/udev/rules.d/99-gpio.rules << 'EOF'
SUBSYSTEM=="gpio", GROUP="gpio", MODE="0660"
KERNEL=="gpiochip*", GROUP="gpio", MODE="0660"
EOF
sudo udevadm control --reload-rules
sudo udevadm trigger
```

#### Khai báo trong config.json

```json
"vehicle_sensor": {
    "enabled": true,
    "chip_path": "/dev/gpiochip0",
    "line_offset": 17,
    "active_low": true,
    "bias_pull_up": true,
    "debounce_us": 20000,
    "clear_delay_ms": 1500
}
```

| Tham số | Giá trị | Giải thích |
|---------|---------|-----------|
| `chip_path` | `/dev/gpiochip0` | GPIO controller chính của Orange Pi 4 LTS |
| `line_offset` | `17` | GPIO số 17, tương ứng chân 11 trên header 40-pin |
| `active_low` | `true` | NPN: OUT = LOW khi có xe, HIGH khi không có xe |
| `bias_pull_up` | `true` | Dùng pull-up nội 3.3V của SoC |
| `debounce_us` | `20000` | Lọc rung 20ms — phù hợp tốc độ xe ô tô |
| `clear_delay_ms` | `1500` | Chờ 1.5s sau khi xe qua trước khi hạ barie |

---

### 3.5 Màn hình HDMI

1. Cắm cáp HDMI từ màn hình vào cổng **HDMI** trên Orange Pi 4 LTS.
2. Bật màn hình trước khi khởi động Orange Pi để hệ thống nhận diện đúng độ phân giải.
3. Khai báo độ phân giải trong `config.json`:

```json
"ui": {
    "fullscreen": true,
    "width": 1440,
    "height": 900
}
```

> Đặt đúng `width` và `height` theo độ phân giải thực tế của màn hình. Phần mềm tự scale toàn bộ UI theo tỷ lệ `min(width/1920, height/1080)` so với thiết kế gốc 1920×1080.

---

### 3.6 Kết nối mạng Ethernet

1. Cắm dây **Ethernet Cat5e/Cat6** từ cổng LAN của Orange Pi vào switch/router nội bộ.
2. Đảm bảo Orange Pi và server MQTT cùng mạng LAN (hoặc khai đúng IP server trong config).
3. Kiểm tra kết nối:

```bash
ip addr show eth0
ping <IP_server>
```

4. Khai địa chỉ MQTT broker trong `config.json`:

```json
"mqtt": {
    "host": "192.168.1.100",
    "port": 1883,
    "username": "parking",
    "password": "parking123",
    "keepalive_seconds": 10
}
```

---

## 4. Cài đặt phần mềm

```bash
# Cài phụ thuộc
sudo apt update
sudo apt install -y cmake qt6-base-dev libqrencode-dev \
                   libmosquitto-dev libgpiod-dev

# Build
cd ~/parking
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

---

## 5. Cấu hình thiết bị (config.json)

Mẫu `config.json` đầy đủ cho thiết bị **làn ra** (exit_only):

```json
{
  "device": {
    "id_prefix": "pk_",
    "auto_id": true,
    "description": "Tầng B1 - Làn ra",
    "type": "exit_only",
    "has_cash_reader": true,
    "has_display": true,
    "has_audio": true
  },
  "vehicle_sensor": {
    "enabled": true,
    "chip_path": "/dev/gpiochip0",
    "line_offset": 17,
    "active_low": true,
    "bias_pull_up": true,
    "debounce_us": 20000,
    "clear_delay_ms": 1500
  },
  "mqtt": {
    "host": "192.168.1.100",
    "port": 1883,
    "username": "parking",
    "password": "parking123",
    "keepalive_seconds": 10
  },
  "barriers": [
    {
      "id": "01",
      "direction": "exit",
      "modbus_addr": 1,
      "description": "Cần B1 - Ra",
      "auto_close_seconds": 10
    }
  ],
  "serial": {
    "barrier_port": "/dev/ttyUSB0",
    "barrier_baud": 19200,
    "nv9_port": "/dev/ttyACM0"
  },
  "audio": {
    "sound_dir": "resources/sounds",
    "volume": 90,
    "sounds": {
      "system_ready":    "system_ready.wav",
      "open_entry":      "open_entry.wav",
      "open_exit":       "open_exit.wav",
      "payment_success": "payment_success.wav",
      "monthly_valid":   "monthly_valid.wav",
      "insert_money":    "insert_money.wav",
      "thank_you":       "thank_you.wav",
      "error":           "error.wav"
    }
  },
  "ui": {
    "fullscreen": true,
    "width": 1440,
    "height": 900,
    "payment_timeout_seconds": 300,
    "style": "dark"
  },
  "logging": {
    "path": "/var/log/parking/parking.log",
    "max_size_mb": 10,
    "max_archives": 5,
    "also_stderr": true
  }
}
```

Mẫu `config.json` cho thiết bị **làn vào** (entry_only, không có màn hình, không có NV9):

```json
{
  "device": {
    "id_prefix": "pk_",
    "auto_id": true,
    "description": "Tầng B1 - Làn vào",
    "type": "entry_only",
    "has_cash_reader": false,
    "has_display": false,
    "has_audio": true
  },
  "vehicle_sensor": {
    "enabled": true,
    "chip_path": "/dev/gpiochip0",
    "line_offset": 17,
    "active_low": true,
    "bias_pull_up": true,
    "debounce_us": 20000,
    "clear_delay_ms": 1500
  },
  "mqtt": {
    "host": "192.168.1.100",
    "port": 1883,
    "username": "parking",
    "password": "parking123",
    "keepalive_seconds": 10
  },
  "barriers": [
    {
      "id": "01",
      "direction": "entry",
      "modbus_addr": 1,
      "description": "Cần B1 - Vào",
      "auto_close_seconds": 8
    }
  ],
  "serial": {
    "barrier_port": "/dev/ttyUSB0",
    "barrier_baud": 19200
  },
  "audio": {
    "sound_dir": "resources/sounds",
    "volume": 90,
    "sounds": {
      "system_ready": "system_ready.wav",
      "open_entry":   "open_entry.wav",
      "error":        "error.wav"
    }
  },
  "logging": {
    "path": "/var/log/parking/parking-entry.log",
    "max_size_mb": 10,
    "max_archives": 5,
    "also_stderr": true
  }
}
```

### Tham số quan trọng

| Tham số | Mô tả |
|---------|-------|
| `device.type` | `entry_only` — làn vào headless; `exit_only` — làn ra có UI+NV9; `full` — cả hai |
| `barriers[].modbus_addr` | Địa chỉ Modbus slave của bộ điều khiển barie (DIP switch trên thiết bị) |
| `barriers[].direction` | `entry` hoặc `exit` — dùng để kiểm tra lệnh mở có hợp lệ không |
| `barriers[].auto_close_seconds` | Thời gian tự đóng barie nếu cảm biến không phát hiện xe |
| `serial.barrier_baud` | **19200** — baud rate Modbus của bộ điều khiển barie |
| `ui.width` / `ui.height` | Độ phân giải màn hình thực tế — dùng để scale UI |

---

## 6. Giao thức truyền nhận bản tin MQTT

### Quy ước topic

```
parking/v1/client/{device_id}/<suffix>   ← Thiết bị → Server
parking/v1/server/{device_id}/cmd        ← Server → Thiết bị
parking/v1/broadcast/cmd                 ← Server broadcast → Mọi thiết bị
```

`device_id` được tự tạo lúc khởi động: `{id_prefix}{hex_machine_id}` (ví dụ: `pk_212080ba0b844721`).  
`barrier_id` = `{device_id}_{barrier_suffix}` (ví dụ: `pk_212080ba0b844721_01`).

### Bản tin thiết bị → Server

#### `connect` — Thiết bị đăng ký online

Gửi khi khởi động hoặc nhận lệnh `reconnect` từ server.

```json
{
  "type": "connect",
  "device_id": "pk_212080ba0b844721",
  "timestamp": "2026-05-05T08:00:00",
  "firmware_version": "1.0.0",
  "has_cash_reader": true,
  "has_display": true,
  "barriers": [
    { "barrier_id": "pk_212080ba0b844721_01", "direction": "exit", "modbus_addr": 1 }
  ]
}
```

#### `payment` — Thiết bị báo đã thu đủ tiền mặt

Gửi sau khi NV9 nhận đủ tiền (`amount_paid ≥ amount_due`).

```json
{
  "type": "payment",
  "device_id": "pk_212080ba0b844721",
  "barrier_id": "pk_212080ba0b844721_01",
  "session_id": "sess_001",
  "plate": "30K-250.44",
  "method": "cash",
  "amount_due": 15000,
  "amount_paid": 15000,
  "amount_change": 0,
  "timestamp": "2026-05-05T10:35:00"
}
```

#### `event` — Sự kiện bất thường

**Hết giờ thanh toán:**
```json
{
  "type": "session_abandoned",
  "device_id": "pk_212080ba0b844721",
  "barrier_id": "pk_212080ba0b844721_01",
  "session_id": "sess_001",
  "plate": "30K-250.44",
  "amount_due": 15000,
  "amount_paid": 5000,
  "timestamp": "2026-05-05T10:40:00"
}
```

**Khách bấm GỌI HỖ TRỢ:**
```json
{
  "type": "support_request",
  "device_id": "pk_212080ba0b844721",
  "barrier_id": "pk_212080ba0b844721_01",
  "session_id": "sess_001",
  "plate": "30K-250.44",
  "reason": "payment_timeout",
  "timestamp": "2026-05-05T10:41:00"
}
```

---

### Bản tin Server → Thiết bị

Tất cả gửi lên topic `parking/v1/server/{device_id}/cmd`, phân biệt qua field `"type"`.

#### `vehicle_info` — Thông tin xe (hiển thị màn hình thanh toán)

**Vé lượt:**
```json
{
  "type": "vehicle_info",
  "session_id": "sess_001",
  "barrier_id": "pk_212080ba0b844721_01",
  "plate": "30K-250.44",
  "ticket_type": "hourly",
  "entry_time": "2026-05-05T08:13:00",
  "exit_time": "2026-05-05T10:30:00",
  "duration_minutes": 137,
  "amount_due": 15000,
  "currency": "VND",
  "payment_methods": ["cash", "qr"],
  "qr_url": "https://192.168.1.100:8443/qr/sess_001.png"
}
```

**Vé tháng:**
```json
{
  "type": "vehicle_info",
  "session_id": "sess_002",
  "barrier_id": "pk_212080ba0b844721_01",
  "plate": "30K-123.45",
  "ticket_type": "monthly",
  "owner_name": "Nguyễn Văn A",
  "valid_until": "2026-05-31",
  "amount_due": 0,
  "payment_methods": [],
  "open": true
}
```

`"open": true` → thiết bị tự mở barie ngay, không cần thanh toán.

#### `open_cmd` — Lệnh mở barie

```json
{
  "type": "open_cmd",
  "barrier_id": "pk_212080ba0b844721_01",
  "session_id": "sess_001",
  "reason": "manual_override",
  "play_sound": true,
  "auto_close_seconds": 10
}
```

Giá trị hợp lệ của `reason`:

| Giá trị | Ý nghĩa |
|---------|---------|
| `vehicle_entry` | Xe vào — camera/LPR phát hiện biển số |
| `entry_authorized` | Xe được phép vào (whitelist / vé tháng) |
| `monthly_ticket` | Vé tháng tự mở |
| `payment_confirmed` | Thanh toán thành công |
| `free_exit` | Ra miễn phí |
| `manual_override` | Bảo vệ mở thủ công — không kiểm tra direction |

#### `payment_success` — Xác nhận thanh toán, mở barie

```json
{
  "type": "payment_success",
  "session_id": "sess_001",
  "barrier_ids": ["pk_212080ba0b844721_01"],
  "plate": "30K-250.44",
  "status": "success",
  "auto_idle_seconds": 15,
  "timestamp": "2026-05-05T10:35:01"
}
```

#### `reconnect` — Server yêu cầu thiết bị re-announce

Gửi lên topic `parking/v1/broadcast/cmd`:

```json
{
  "type": "reconnect",
  "reason": "server_restart",
  "timestamp": "2026-05-05T09:00:00"
}
```

---

## 7. Kiểm tra vận hành

### Khởi động

```bash
cd ~/parking
./build/parking_client -c config.json
```

### Kiểm tra thiết bị ngoại vi

```bash
# NV9
ls /dev/ttyACM0

# Barie RS485
ls /dev/ttyUSB0

# GPIO cảm biến
gpiodetect
gpioinfo gpiochip0 | grep -A1 "line 17"

# Âm thanh
aplay -D default resources/sounds/open_exit.wav
```

### Test thủ công qua MQTT CLI

**Gửi thông tin xe (màn hình thanh toán):**
```bash
mosquitto_pub -h localhost -u parking -P parking123 \
  -t "parking/v1/server/pk_212080ba0b844721/cmd" \
  -m '{
    "type": "vehicle_info",
    "session_id": "test_001",
    "barrier_id": "pk_212080ba0b844721_01",
    "plate": "30K-250.44",
    "ticket_type": "hourly",
    "entry_time": "2026-05-05T08:00:00",
    "exit_time": "2026-05-05T10:00:00",
    "duration_minutes": 120,
    "amount_due": 15000,
    "payment_methods": ["cash"]
  }'
```

**Xác nhận thanh toán + mở barie:**
```bash
mosquitto_pub -h localhost -u parking -P parking123 \
  -t "parking/v1/server/pk_212080ba0b844721/cmd" \
  -m '{
    "type": "payment_success",
    "session_id": "test_001",
    "barrier_ids": ["pk_212080ba0b844721_01"],
    "plate": "30K-250.44",
    "status": "success",
    "auto_idle_seconds": 10
  }'
```

**Mở barie thủ công (manual override):**
```bash
mosquitto_pub -h localhost -u parking -P parking123 \
  -t "parking/v1/server/pk_212080ba0b844721/cmd" \
  -m '{
    "type": "open_cmd",
    "barrier_id": "pk_212080ba0b844721_01",
    "session_id": "manual_001",
    "reason": "manual_override",
    "play_sound": true,
    "auto_close_seconds": 8
  }'
```

**Lắng nghe bản tin từ thiết bị:**
```bash
mosquitto_sub -h localhost -u parking -P parking123 \
  -t "parking/v1/client/pk_212080ba0b844721/#" -v
```

### Checklist hoàn thành lắp ráp

- [ ] `/dev/ttyACM0` xuất hiện sau khi cắm NV9
- [ ] `/dev/ttyUSB0` xuất hiện sau khi cắm CH340 barie
- [ ] Log hiển thị `[NV9] Ready — insert note` sau khởi động
- [ ] Log hiển thị `[Barrier] opened "/dev/ttyUSB0" @ 19200 baud`
- [ ] Log hiển thị `[MQTT] connected` và `[App] MQTT connected`
- [ ] `aplay` phát được file WAV qua loa
- [ ] Cảm biến E3F: log `[VehicleSensor] open` (không có `Permission denied`)
- [ ] Màn hình hiển thị đúng độ phân giải, không bị cắt chữ
- [ ] Gửi `open_cmd manual_override` → barie nâng lên và tự đóng sau X giây
