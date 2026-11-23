# Kế hoạch: OTA cho receiver + nạp qua ESP32 passthrough + nút toggle bơm (IO2)

Người thực thi: đọc hết file trước khi sửa. Làm đúng thứ tự Phần 1 → 5. Không đụng tới code CH32V003 và `sender.cpp`. Không commit những thay đổi đang dở sẵn trong working tree (`include/user_config.h`, `src/ch32v003/main.c`, `platformio.ini` dòng `board_build.ldscript`) theo cách làm mất chúng: giữ nguyên, chỉ thêm phần mới.

## Bối cảnh (đã kiểm tra)

- Receiver: [src/esp-01/receiver.cpp](../src/esp-01/receiver.cpp), board `esp01_1m`, ldscript `eagle.flash.1m.ld` (vùng app tối đa ≈ 998 KB, không có FS).
- Firmware receiver hiện ≈ 360 KB. OTA trên ESP8266 cần chỗ trống ≥ kích thước firmware mới, nên firmware phải ≤ ~490 KB. Bỏ WiFiManager (xem 2.1) giúp nhỏ đi thêm.
- `base_config.ini` đặt `lib_ldf_mode = off` cho ESP-01 ⇒ **mọi thư viện framework dùng tới đều phải ghi vào `lib_deps`** (ArduinoOTA, ESP8266mDNS), nếu không sẽ lỗi không tìm thấy header.
- Board `esp01_1m` có `resetmethod = ck` ⇒ PlatformIO gọi esptool với `--before no_reset --after soft_reset`: esptool **không** dùng DTR/RTS. Điều này đúng ý cho passthrough (DTR/RTS của cổng USB nối vào ESP32, không phải ESP-01).
- `_upload_method_uart` đang có `upload_speed = 921000`. Với passthrough phải dùng **115200** cố định, vì ESP32 bridge chạy baud cố định; nếu khác 115200 esptool sẽ gửi lệnh đổi baud và bridge không theo kịp.
- Relay ở GPIO0 (active LOW). GPIO2 sẽ dùng cho nút. GPIO0 và GPIO2 đều là chân strapping: lúc cấp nguồn phải HIGH để chạy app ⇒ **không được giữ nút lúc khởi động** (ghi chú này đưa vào comment code).

## Quyết định đã chốt

| Việc | Chốt |
|---|---|
| Phạm vi OTA | **Tạm thời**. Toàn bộ phần OTA + Wi-Fi cứng nằm trong `#ifdef OTA_ENABLE`. Nhánh `#else` là **code WiFiManager cũ giữ nguyên từng dòng** (softAP, portal 180 s, `wifi_set_channel`). Không xoá gì của bản gốc. |
| Bật OTA ở đâu | Chỉ env `esp01s_receiver_passthrough` và `esp01s_receiver_ota` có `-D OTA_ENABLE`. Env `esp01s_receiver_uart` build ra firmware như cũ (WiFiManager, không OTA) + nút IO2. |
| Wi-Fi khi `OTA_ENABLE` | Kết nối cứng SSID `Tuan Ly`, pass `***REMOVED***`, không dùng WiFiManager (cho firmware nhỏ, đủ chỗ OTA trên flash 1 MB). |
| Không vào được Wi-Fi (khi `OTA_ENABLE`) | Không chặn. ESP-NOW + nút + relay vẫn chạy. Thử lại mỗi 5 phút. |
| OTA | `ArduinoOTA`, hostname `bom-dien`, cổng 8266, mật khẩu OTA `***REMOVED***` (đổi được trong `config.h`). |
| Lần nạp đầu | Bắt buộc qua passthrough (firmware cũ chưa có OTA). Các lần sau dùng OTA. Khi có mạch nạp thật: nạp env `esp01s_receiver_uart` là về lại bản gốc. |
| Nút IO2 | **Vĩnh viễn**, có ở cả hai chế độ (không nằm trong `#ifdef`). |
| Passthrough | ESP32 DevKit làm cầu UART; tự đưa ESP-01 vào chế độ nạp khi thấy gói SYNC của esptool; nút BOOT của ESP32 làm dự phòng. |
| Nút IO2 | `INPUT_PULLUP`, nhấn nối GND, chống dội 50 ms, mỗi lần nhấn đảo trạng thái bơm, đồng bộ lên Blynk V0 + log V3. |

---

## Phần 1. Cấu hình (`include/config.h`)

Trong khối `#ifdef RECEIVER_MODE`, thêm (đặt sau các define relay, trước Blynk):

```c
#define BUTTON_PIN 2              // Nút nhấn nối GND, dùng pull-up nội. KHÔNG giữ nút khi cấp nguồn (GPIO2 là chân strapping)
#define BUTTON_DEBOUNCE_MS 50

// --- CHẾ ĐỘ NẠP TẠM QUA OTA (chỉ bật khi build với -D OTA_ENABLE) ---
#ifdef OTA_ENABLE
#define WIFI_SSID "Tuan Ly"
#define WIFI_PASS "***REMOVED***"
#define WIFI_CONNECT_TIMEOUT_MS 15000
#define WIFI_RETRY_INTERVAL_MS  (5UL * 60 * 1000)

#define OTA_HOSTNAME "bom-dien"
#define OTA_PASSWORD "***REMOVED***"
#endif
```

Lưu ý: `OTA_PASSWORD` phải khớp với `--auth` trong `platformio.ini` (Phần 3).

## Phần 2. Sửa `src/esp-01/receiver.cpp`

Giữ phong cách hiện có: comment tiếng Việt, hàm `static`, không thêm thư viện ngoài.

### 2.1 Include

```cpp
#ifdef OTA_ENABLE
#include <ArduinoOTA.h>
#else
#include <WiFiManager.h>
#endif
```

Nguyên tắc chung cho cả Phần 2: **không xoá dòng nào của luồng WiFiManager cũ**, chỉ bọc nó vào `#else`. Phần mới thêm (OTA, Wi-Fi cứng) nằm trong `#ifdef OTA_ENABLE`. Phần nút IO2 và `setPump()` nằm ngoài mọi `#ifdef`.

### 2.2 Gom điều khiển relay về một chỗ

Thêm biến và hàm, đặt ngay sau khối biến ESP-NOW:

```cpp
static bool pumpOn = false;

// Bật/tắt bơm và ghi nhận trạng thái hiện tại
static void setPump(const bool on)
{
    pumpOn = on;
    digitalWrite(RELAY_PIN, on ? RELAY_ON : RELAY_OFF);
}
```

Thay **mọi** `digitalWrite(RELAY_PIN, ...)` bằng `setPump(...)`:
- `handleEspNowData()`: `setPump(pendingMsg.status == 1);`
- `BLYNK_WRITE(V0)`: `setPump(manualState == 1);`
- `setup()`: thay `digitalWrite(RELAY_PIN, RELAY_OFF);` bằng `setPump(false);` (vẫn giữ thứ tự: ghi mức trước, `pinMode(RELAY_PIN, OUTPUT)` sau, để relay không giật lúc khởi động).

Tiện sửa lỗi chính tả trong `BLYNK_WRITE(V0)`: `"TẶT"` → `"TẮT"`.

### 2.3 Nút toggle IO2

```cpp
// Đọc nút IO2 (nhấn = LOW). Mỗi lần nhấn đảo trạng thái bơm
static void handleButton()
{
    static bool lastStable = HIGH;
    static bool lastRead = HIGH;
    static uint32_t lastChange = 0;

    const bool reading = digitalRead(BUTTON_PIN);
    if (reading != lastRead)
    {
        lastRead = reading;
        lastChange = millis();
    }

    if (millis() - lastChange < BUTTON_DEBOUNCE_MS || reading == lastStable) return;
    lastStable = reading;

    if (lastStable == LOW) // Cạnh nhấn xuống
    {
        setPump(!pumpOn);
        if (WiFi.status() == WL_CONNECTED)
        {
            Blynk.virtualWrite(V0, pumpOn ? 1 : 0);
            const String logMsg = pumpOn ? "🔘 Bạn vừa BẬT bơm bằng nút" : "🔘 Bạn vừa TẮT bơm bằng nút";
            Blynk.virtualWrite(V3, getTimeString() + logMsg + "\n");
        }
    }
}
```

Trong `setup()`, ngay sau phần relay: `pinMode(BUTTON_PIN, INPUT_PULLUP);`

Hành vi cần biết (ghi vào comment): gói ESP-NOW tiếp theo từ phao sẽ ghi đè trạng thái do nút đặt. Đó là chủ ý, không cần xử lý thêm.

### 2.4 Wi-Fi cứng + OTA (chỉ trong `#ifdef OTA_ENABLE`)

Khung `setup()` sau khi sửa (khởi tạo relay + nút ở trên, chung cho cả hai nhánh):

```cpp
#ifdef OTA_ENABLE
    // [NẠP TẠM QUA OTA] Wi-Fi cố định, không WiFiManager
    WiFi.mode(WIFI_STA);
    WiFi.persistent(false);
    WiFi.setAutoReconnect(true);
    initEspNow();
    if (connectWiFi()) startNetworkServices();
    if (WiFi.status() != WL_CONNECTED) wifi_set_channel(WIFI_CHANNEL);
#else
    // ... toàn bộ code cũ từ WiFi.mode(WIFI_AP_STA) tới wifi_set_channel(WIFI_CHANNEL), giữ nguyên,
    //     chỉ thay khối esp_now_init bằng initEspNow()
#endif
```

Các bước:

1. Tách khối khởi tạo ESP-NOW hiện có (`esp_now_init`, role, recv_cb, peer broadcast) thành `static void initEspNow()` dùng chung cho cả hai nhánh. Nội dung giữ nguyên.
2. Các hàm `connectWiFi()`, `startNetworkServices()` và biến `lastWiFiAttempt`, `servicesStarted` dưới đây đều bọc trong `#ifdef OTA_ENABLE`.
3. Hàm `connectWiFi()`:

```cpp
static uint32_t lastWiFiAttempt = 0;
static bool servicesStarted = false;

// Kết nối Wi-Fi cố định, chờ tối đa WIFI_CONNECT_TIMEOUT_MS (vẫn đọc nút trong lúc chờ)
static bool connectWiFi()
{
    lastWiFiAttempt = millis();
    WiFi.begin(WIFI_SSID, WIFI_PASS);

    while (WiFi.status() != WL_CONNECTED && millis() - lastWiFiAttempt < WIFI_CONNECT_TIMEOUT_MS)
    {
        handleButton();
        delay(10);
    }
    return WiFi.status() == WL_CONNECTED;
}
```

4. Hàm `startNetworkServices()` chạy **một lần** khi lần đầu có Wi-Fi (`servicesStarted` chặn chạy lại), chứa:
   - các dòng tối ưu đang có: `WiFi.setSleepMode(WIFI_NONE_SLEEP);` `WiFi.setOutputPower(17.5);`
   - `Blynk.config(BLYNK_AUTH_TOKEN);` `setSyncInterval(10 * 60);`
   - OTA:
     ```cpp
     ArduinoOTA.setHostname(OTA_HOSTNAME);
     ArduinoOTA.setPassword(OTA_PASSWORD);
     ArduinoOTA.onStart([]() { esp_now_unregister_recv_cb(); }); // Ngừng nhận ESP-NOW khi đang ghi flash
     ArduinoOTA.onError([](ota_error_t) { esp_now_register_recv_cb(OnDataRecv); });
     ArduinoOTA.begin();
     ```
     Không tắt relay khi OTA: giữ nguyên trạng thái bơm; sau khi OTA xong ESP tự khởi động lại và `setup()` đặt relay OFF như mọi lần khởi động.
5. Ở nhánh OTA, `wifi_set_channel(WIFI_CHANNEL)` chỉ gọi khi chưa kết nối (đã có trong khung trên): khi STA đã nối router thì kênh do router quyết định, ép kênh 6 sẽ làm rớt Wi-Fi. Sender vốn có quét kênh nên vẫn tìm thấy receiver. Nhánh `#else` giữ cách gọi cũ, không sửa.

### 2.5 `loop()`

```cpp
void loop()
{
    handleEspNowData();
    handleButton();

#ifdef OTA_ENABLE
    if (WiFi.status() == WL_CONNECTED)
    {
        if (!servicesStarted) startNetworkServices();
        ArduinoOTA.handle();
        Blynk.run();
    }
    else if (millis() - lastWiFiAttempt > WIFI_RETRY_INTERVAL_MS)
    {
        lastWiFiAttempt = millis();
        WiFi.begin(WIFI_SSID, WIFI_PASS); // Không chờ, setAutoReconnect lo phần còn lại
    }
#else
    // Run Blynk (code cũ giữ nguyên)
    if (WiFi.status() == WL_CONNECTED)
    {
        Blynk.run();
    }
#endif

    delay(1);
}
```

Lưu ý nhánh cũ: `wm.autoConnect()` chặn tới 180 s khi chưa có Wi-Fi, trong lúc đó nút IO2 không phản hồi. Đó là hành vi sẵn có, **không sửa**.

### 2.6 Comment đầu file

Thêm comment ngắn ngay dưới các `#include`:

```cpp
// OTA_ENABLE: chế độ nạp TẠM qua Wi-Fi (SSID cố định, ArduinoOTA, không WiFiManager).
// Không định nghĩa (env esp01s_receiver_uart): firmware gốc dùng WiFiManager như trước.
```

Thứ tự khai báo: `handleButton()` phải được định nghĩa trước `connectWiFi()`; `startNetworkServices()` dùng `OnDataRecv` nên đặt sau nó.

## Phần 3. PlatformIO

### 3.1 `base_config.ini` — thêm 2 khối

```ini
; Nạp qua ESP32 làm cầu UART (passthrough). Baud cố định 115200 để esptool không đổi baud
[_upload_method_passthrough]
upload_speed = 115200
monitor_speed = 115200
monitor_dtr = 0
monitor_rts = 0

; Nạp qua Wi-Fi (ArduinoOTA). Nếu máy không phân giải được .local thì thay bằng IP của receiver
[_upload_method_ota]
upload_protocol = espota
upload_port = bom-dien.local
upload_flags =
    --port=8266
    --auth=***REMOVED***
```

Không sửa `_upload_method_uart` (vẫn dùng cho mạch nạp USB-UART thật).

### 3.2 `platformio.ini` — tách phần chung của receiver

Thay `[env:esp01s_receiver_uart]` hiện tại bằng:

```ini
; Mạch Nhận - Đặt ở máy bơm (Nhận lệnh ESP-NOW để đóng cắt relay bơm)
; Firmware gốc: WiFiManager, không OTA. Nạp bằng mạch USB-UART
[env:esp01s_receiver_uart]
extends = _base_esp01_s, _upload_method_uart
build_src_filter = -<*> +<esp-01/receiver.cpp>
board_build.ldscript = eagle.flash.1m.ld
build_flags =
    -D RECEIVER_MODE
lib_deps =
    ESP8266WiFi
    ESP8266WebServer
    DNSServer
    tzapu/WiFiManager
    blynkkk/Blynk
    paulstoffregen/Time @ ^1.6.1

; ---- CHẾ ĐỘ NẠP TẠM QUA OTA (khi không mang mạch nạp) ----
; Wi-Fi cố định + ArduinoOTA, bỏ WiFiManager cho firmware nhỏ. Có mạch nạp thì quay về env ở trên
[_receiver_ota_common]
extends = env:esp01s_receiver_uart
build_flags =
    -D RECEIVER_MODE
    -D OTA_ENABLE
lib_deps =
    ESP8266WiFi
    ESP8266mDNS
    ArduinoOTA
    blynkkk/Blynk
    paulstoffregen/Time @ ^1.6.1

; Lần nạp đầu: qua ESP32 passthrough
[env:esp01s_receiver_passthrough]
extends = _receiver_ota_common, _upload_method_passthrough

; Các lần sau: qua Wi-Fi
[env:esp01s_receiver_ota]
extends = _receiver_ota_common, _upload_method_ota
```

Env `esp01s_receiver_uart` giữ nguyên như hiện tại (kể cả dòng `board_build.ldscript` đang dở trong working tree). Nếu `extends` lồng (`_receiver_ota_common` → `env:esp01s_receiver_uart`) gây lỗi hoặc `upload_speed` 921000 của uart lấn `_upload_method_passthrough`, thì chép thẳng `build_src_filter` và `board_build.ldscript` vào `_receiver_ota_common`, cho nó `extends = _base_esp01_s`. Kiểm tra bằng `pio project config` rằng env passthrough có `upload_speed = 115200`.

Thêm khối mới cho ESP32 bridge:

```ini
; ------------------------------------------
; 3. CÔNG CỤ: ESP32 LÀM CẦU NẠP CHO ESP-01 (PASSTHROUGH)
; ------------------------------------------
[env:esp32_passthrough]
platform = espressif32
board = esp32dev
framework = arduino
build_src_filter = -<*> +<esp32/passthrough.cpp>
monitor_speed = 115200
monitor_dtr = 0
monitor_rts = 0
```

## Phần 4. Firmware ESP32 passthrough (`src/esp32/passthrough.cpp`, file mới)

### 4.1 Đấu dây (ghi vào comment đầu file)

| ESP32 DevKit | ESP-01 / ESP-01S | Ghi chú |
|---|---|---|
| GPIO17 (TX2) | RX (GPIO3) | |
| GPIO16 (RX2) | TX (GPIO1) | |
| GPIO25 | GPIO0 | ESP32 kéo LOW để vào chế độ nạp |
| GPIO26 | RST | ESP32 kéo LOW để reset |
| 3V3 hoặc nguồn 3.3 V riêng ≥ 500 mA | VCC, EN (CH_PD) | ESP-01 sụt áp khi phát sóng; chân 3V3 của DevKit có thể yếu, nên dùng AMS1117 riêng nếu nạp lỗi giữa chừng |
| GND | GND | Bắt buộc chung mass |
| — | GPIO2 | Để trống hoặc nút đã có pull-up; không nối GND |

Nếu nạp ESP-01 ngay trên bo máy bơm: relay ở GPIO0 sẽ đóng trong lúc nạp (GPIO0 = LOW). Nên rút ESP-01 ra nạp riêng hoặc ngắt nguồn bơm.

### 4.2 Chức năng

Hằng số đầu file:

```cpp
#define BRIDGE_BAUD     115200
#define PIN_ESP_RX      16   // RX2 của ESP32, nối TX của ESP-01
#define PIN_ESP_TX      17   // TX2 của ESP32, nối RX của ESP-01
#define PIN_ESP_GPIO0   25
#define PIN_ESP_RST     26
#define PIN_BOOT_BTN    0    // Nút BOOT trên DevKit
#define PIN_LED         2    // LED trên DevKit: sáng = ESP-01 đang ở chế độ nạp
#define LONG_PRESS_MS   1000
#define FLASH_IDLE_MS   3000 // Hết lưu lượng nạp quá thời gian này thì coi như xong phiên nạp
```

Yêu cầu:

1. `setup()`:
   - `Serial.setRxBufferSize(1024); Serial.begin(BRIDGE_BAUD);`
   - `Serial2.setRxBufferSize(1024); Serial2.begin(BRIDGE_BAUD, SERIAL_8N1, PIN_ESP_RX, PIN_ESP_TX);`
   - `PIN_ESP_GPIO0`, `PIN_ESP_RST`: `OUTPUT`, mức HIGH. `PIN_BOOT_BTN`: `INPUT_PULLUP`. `PIN_LED`: `OUTPUT`.
   - Gọi `resetEsp01(false)` (khởi động ESP-01 ở chế độ chạy app, để monitor đọc được log).
2. `resetEsp01(bool flashMode)`:
   - `digitalWrite(PIN_ESP_GPIO0, flashMode ? LOW : HIGH);`
   - RST LOW 100 ms → HIGH → chờ 50 ms → `digitalWrite(PIN_ESP_GPIO0, HIGH)` (GPIO0 chỉ cần LOW lúc ESP-01 thoát reset; nhả ra sau đó ROM vẫn ở chế độ nạp).
   - Xoá rác: `while (Serial2.available()) Serial2.read();`
   - Cập nhật biến `inFlashMode` và LED.
3. Tự vào chế độ nạp khi thấy esptool: theo dõi luồng byte PC → ESP-01, nhận chuỗi SLIP mở đầu lệnh SYNC `0xC0 0x00 0x08` (máy trạng thái 3 bước, đúng byte thì tiến, sai thì về 0, gặp `0xC0` thì về bước 1). Khi khớp và `!inFlashMode` ⇒ gọi `resetEsp01(true)`. Gói SYNC đầu tiên bị mất là bình thường, esptool tự gửi lại.
4. Thoát phiên nạp: nếu `inFlashMode` và không có byte nào từ PC trong `FLASH_IDLE_MS` ⇒ chỉ đặt `inFlashMode = false` và tắt LED, **không reset** ESP-01 (esptool đã `soft_reset` cho app chạy).
5. Nút BOOT (chống dội 50 ms, xét lúc nhả): nhấn ngắn ⇒ `resetEsp01(true)`; nhấn ≥ `LONG_PRESS_MS` ⇒ `resetEsp01(false)`. Dùng khi tự động không ăn.
6. `loop()` chuyển tiếp hai chiều theo khối, không xử lý từng byte qua `Serial.write(c)`:
   ```cpp
   uint8_t buf[256];
   size_t n = Serial.available();
   if (n) { n = Serial.readBytes(buf, min(n, sizeof(buf))); feedSyncDetector(buf, n); Serial2.write(buf, n); lastHostByte = millis(); }
   n = Serial2.available();
   if (n) { n = Serial2.readBytes(buf, min(n, sizeof(buf))); Serial.write(buf, n); }
   ```
   Chú ý: gọi `feedSyncDetector` **trước** `Serial2.write`, để nếu phát hiện SYNC thì reset ESP-01 xong mới gửi (gói này có thể mất, chấp nhận).
7. Không in log debug nào ra `Serial` (sẽ làm hỏng giao thức esptool).

## Phần 5. Kiểm tra (tiêu chí xong)

Build (bắt buộc pass cả 4, không warning mới do mình thêm):

```bash
pio run -e esp01s_receiver_uart
pio run -e esp01s_receiver_passthrough
pio run -e esp01s_receiver_ota
pio run -e esp32_passthrough
pio run -e ch32v003_power_manager   # đảm bảo không làm vỡ env cũ
```

Kiểm tra kích thước: `firmware.bin` của env OTA phải ≤ 490 KB (in ra bằng `ls -l .pio/build/esp01s_receiver_ota/firmware.bin`). Lớn hơn thì dừng lại báo, không tự bỏ tính năng. Env uart không cần giới hạn này.

Kiểm tra giữ code cũ: `git diff src/esp-01/receiver.cpp` chỉ được thấy dòng thêm `#ifdef/#else/#endif`, `setPump`, `initEspNow`, nút IO2 và khối OTA; không có dòng nào của luồng WiFiManager bị xoá (ngoài phần ESP-NOW đã chuyển vào `initEspNow()`).

Thử trên phần cứng (người dùng làm, ghi các bước này vào báo cáo cuối):

1. Nạp bridge: cắm ESP32, `pio run -e esp32_passthrough -t upload`.
2. Đấu dây theo 4.1. Nạp receiver lần đầu: `pio run -e esp01s_receiver_passthrough -t upload --upload-port /dev/ttyUSB0`. Nếu báo "Failed to connect": nhấn ngắn nút BOOT trên ESP32 rồi chạy lại lệnh.
3. `pio device monitor -e esp01s_receiver_passthrough`: ESP-01 phải chạy app (nhấn giữ BOOT ≥ 1 s nếu không thấy gì).
4. Kiểm tra router "Tuan Ly" thấy thiết bị `bom-dien`. Đổi gì đó nhỏ trong code, nạp `pio run -e esp01s_receiver_ota -t upload` (hoặc `--upload-port <IP>`). Máy tính phải cùng mạng Wi-Fi "Tuan Ly"; tường lửa cho phép kết nối TCP vào máy tính (espota mở cổng ngẫu nhiên để ESP kéo firmware về).
5. Nút IO2: mỗi lần nhấn relay đảo trạng thái, Blynk V0 đổi theo, V3 có dòng log. Giữ nút rồi cấp nguồn ⇒ ESP không chạy (đúng như dự kiến, do GPIO2 strapping).
6. Tắt router: ESP-NOW từ phao vẫn bật/tắt được bơm, nút vẫn chạy.

Báo cáo cuối gồm: danh sách file đã sửa, kết quả 5 lệnh build, kích thước firmware receiver.
