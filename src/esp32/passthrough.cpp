// ESP32 DevKit làm cầu UART để nạp ESP-01 (passthrough) cho esptool trên PC.
// Tự đưa ESP-01 vào chế độ nạp khi thấy gói SYNC của esptool; nút BOOT làm dự phòng
// (nhấn ngắn = vào chế độ nạp, nhấn giữ >= 1 s = reset chạy app).
//
// Đấu dây:
//   ESP32 GPIO17 (TX2) -> ESP-01 RX (GPIO3)
//   ESP32 GPIO16 (RX2) <- ESP-01 TX (GPIO1)
//   ESP32 GPIO25       -> ESP-01 GPIO0   (kéo LOW để vào chế độ nạp)
//   ESP32 GPIO26       -> ESP-01 RST     (kéo LOW để reset)
//   3V3 (hoặc nguồn 3.3 V riêng >= 500 mA) -> ESP-01 VCC, EN (CH_PD)
//        ESP-01 sụt áp khi phát sóng; nếu nạp lỗi giữa chừng thì dùng AMS1117 riêng
//   GND                -> ESP-01 GND (bắt buộc chung mass)
//   ESP-01 GPIO2: để trống hoặc nút đã có pull-up, không nối GND
// Nạp ESP-01 ngay trên bo máy bơm: relay ở GPIO0 sẽ đóng lúc nạp, nên rút ESP-01 ra hoặc ngắt nguồn bơm.
//
// Không in log debug ra Serial (làm hỏng giao thức esptool).
#include <Arduino.h>

#define BRIDGE_BAUD     115200
#ifndef ESP_UART_BAUD
#define ESP_UART_BAUD   BRIDGE_BAUD // Ghi đè bằng -D ESP_UART_BAUD=74880 để nghe log ROM của ESP8266
#endif
#define PIN_ESP_RX      16   // RX2 của ESP32, nối TX của ESP-01
#define PIN_ESP_TX      17   // TX2 của ESP32, nối RX của ESP-01
#define PIN_ESP_GPIO0   25
#define PIN_ESP_RST     26
#define PIN_BOOT_BTN    0    // Nút BOOT trên DevKit
#define PIN_LED         2    // LED trên DevKit: sáng = ESP-01 đang ở chế độ nạp
#define LONG_PRESS_MS   1000
#define BUTTON_DEBOUNCE_MS 50
#define FLASH_IDLE_MS   3000 // Hết lưu lượng nạp quá thời gian này thì coi như xong phiên nạp

static bool inFlashMode = false;
static uint32_t lastHostByte = 0;

// Reset ESP-01; flashMode = true: giữ GPIO0 LOW lúc thoát reset để vào bootloader
static void resetEsp01(const bool flashMode)
{
    digitalWrite(PIN_ESP_GPIO0, flashMode ? LOW : HIGH);
    digitalWrite(PIN_ESP_RST, LOW);
    delay(100);
    digitalWrite(PIN_ESP_RST, HIGH);
    delay(50);
    // GPIO0 chỉ cần LOW lúc thoát reset, nhả ra sau đó ROM vẫn ở chế độ nạp
    digitalWrite(PIN_ESP_GPIO0, HIGH);

    while (Serial2.available()) Serial2.read();

    inFlashMode = flashMode;
    lastHostByte = millis();
    digitalWrite(PIN_LED, flashMode ? HIGH : LOW);
}

// Dò chuỗi SLIP mở đầu lệnh SYNC của esptool: 0xC0 0x00 0x08
static void feedSyncDetector(const uint8_t* data, const size_t len)
{
    static uint8_t step = 0;
    for (size_t i = 0; i < len; i++)
    {
        const uint8_t c = data[i];
        if (c == 0xC0) step = 1;
        else if (step == 1 && c == 0x00) step = 2;
        else if (step == 2 && c == 0x08)
        {
            step = 0;
            if (!inFlashMode) resetEsp01(true);
        }
        else step = 0;
    }
}

// Nút BOOT (xét lúc nhả): nhấn ngắn = vào chế độ nạp, nhấn giữ = chạy app
static void handleBootButton()
{
    static bool lastStable = HIGH;
    static bool lastRead = HIGH;
    static uint32_t lastChange = 0;
    static uint32_t pressedAt = 0;

    const bool reading = digitalRead(PIN_BOOT_BTN);
    if (reading != lastRead)
    {
        lastRead = reading;
        lastChange = millis();
    }
    if (millis() - lastChange < BUTTON_DEBOUNCE_MS || reading == lastStable) return;
    lastStable = reading;

    if (lastStable == LOW)
    {
        pressedAt = millis();
    }
    else
    {
        resetEsp01(millis() - pressedAt < LONG_PRESS_MS);
    }
}

void setup()
{
    Serial.setRxBufferSize(1024);
    Serial.begin(BRIDGE_BAUD);
    Serial2.setRxBufferSize(1024);
    Serial2.begin(ESP_UART_BAUD, SERIAL_8N1, PIN_ESP_RX, PIN_ESP_TX);

    pinMode(PIN_ESP_GPIO0, OUTPUT);
    pinMode(PIN_ESP_RST, OUTPUT);
    digitalWrite(PIN_ESP_GPIO0, HIGH);
    digitalWrite(PIN_ESP_RST, HIGH);
    pinMode(PIN_BOOT_BTN, INPUT_PULLUP);
    pinMode(PIN_LED, OUTPUT);

    // Khởi động ESP-01 ở chế độ chạy app để monitor đọc được log
    resetEsp01(false);
}

void loop()
{
    uint8_t buf[256];

    size_t n = Serial.available();
    if (n)
    {
        n = Serial.readBytes(buf, min(n, sizeof(buf)));
        feedSyncDetector(buf, n); // Trước khi ghi: nếu thấy SYNC thì reset xong mới gửi (gói này có thể mất)
        Serial2.write(buf, n);
        lastHostByte = millis();
    }

    n = Serial2.available();
    if (n)
    {
        n = Serial2.readBytes(buf, min(n, sizeof(buf)));
        Serial.write(buf, n);
    }

    // Hết phiên nạp: không reset, esptool đã soft_reset cho app chạy
    if (inFlashMode && millis() - lastHostByte > FLASH_IDLE_MS)
    {
        inFlashMode = false;
        digitalWrite(PIN_LED, LOW);
    }

    handleBootButton();
}
