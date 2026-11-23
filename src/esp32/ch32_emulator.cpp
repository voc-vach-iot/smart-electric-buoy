// ESP32 DevKit giả lập CH32V003 trên bo phao để test sender (ESP-01) trên bàn.
// Gõ 'E' (phao báo cạn, level LOW) hoặc 'F' (phao báo đầy, level HIGH) qua Serial 115200:
// cấp nguồn cho ESP-01 (nhả RST), chuyển tiếp log debug của sender ra Serial,
// chờ sender kéo GPIO0 xuống LOW (ACK) hoặc hết 10 s, rồi ngắt nguồn (kéo RST LOW).
// Hành vi mô phỏng theo src/ch32v003/main.c.
//
// Đấu dây:
//   ESP32 GPIO17 -> ESP-01 RX (GPIO3)   dùng như chân OUTPUT thường = tín hiệu mức phao
//   ESP32 GPIO16 <- ESP-01 TX (GPIO1)   UART RX 74880 để đọc log debug của sender
//   ESP32 GPIO25 <- ESP-01 GPIO0        INPUT_PULLUP, đọc ACK (LOW = sender xong việc)
//   ESP32 GPIO26 -> ESP-01 RST          giả lập nguồn: LOW = tắt (giữ reset), HIGH = bật
//   3V3 -> ESP-01 VCC, EN (CH_PD) cấp thường trực
//   GND -> ESP-01 GND (bắt buộc chung mass)
//
// Dùng Serial1 với TX trên GPIO27 (không dùng) để GPIO17 không bị UART chiếm làm TX.
#include <Arduino.h>

#define USB_BAUD        115200
#define ESP_LOG_BAUD    74880    // Baud log debug của sender (và log ROM ESP8266)
#define PIN_ESP_RX      16       // Nhận log từ TX của ESP-01
#define PIN_ESP_TX_DUMMY 27      // TX của Serial1, không nối đâu cả
#define PIN_LEVEL       17       // Tín hiệu mức phao -> RX (GPIO3) của ESP-01
#define PIN_ACK         25       // GPIO0 của ESP-01
#define PIN_ESP_RST     26       // RST của ESP-01

#define LEVEL_SETTLE_MS 2        // Chờ mức phao ổn định trước khi cấp nguồn
#define BOOT_IGNORE_MS  150      // Bỏ qua GPIO0 trong lúc ESP-01 đang boot
#define ACK_TIMEOUT_MS  10000
#define TAIL_MS         100      // Vẫn chuyển tiếp log thêm sau ACK/timeout

// Chuyển tiếp mọi byte đang có từ Serial1 sang Serial
static void forwardLog()
{
    uint8_t buf[128];
    size_t n;
    while ((n = Serial1.available()) > 0)
    {
        n = Serial1.readBytes(buf, min(n, sizeof(buf)));
        Serial.write(buf, n);
    }
}

// Một lần trigger: cấp nguồn ESP-01, chờ ACK, ngắt nguồn
static void runTrigger(const bool full)
{
    digitalWrite(PIN_LEVEL, full ? HIGH : LOW);
    delay(LEVEL_SETTLE_MS);
    pinMode(PIN_ACK, INPUT_PULLUP); // Phải HIGH lúc thoát reset để ESP-01 boot từ flash
    while (Serial1.available()) Serial1.read();

    digitalWrite(PIN_ESP_RST, HIGH);
    const uint32_t t0 = millis();
    Serial.printf("[EMU] Cap nguon ESP (%c)\n", full ? 'F' : 'E');

    bool acked = false;
    while (millis() - t0 < ACK_TIMEOUT_MS)
    {
        forwardLog();
        if (millis() - t0 >= BOOT_IGNORE_MS && digitalRead(PIN_ACK) == LOW)
        {
            acked = true;
            break;
        }
    }
    forwardLog();
    if (acked) Serial.printf("[EMU] ACK sau %lu ms\n", (unsigned long)(millis() - t0));
    else Serial.printf("[EMU] TIMEOUT %d ms\n", ACK_TIMEOUT_MS);

    const uint32_t tTail = millis();
    while (millis() - tTail < TAIL_MS) forwardLog();

    digitalWrite(PIN_ESP_RST, LOW);
    digitalWrite(PIN_LEVEL, LOW);
    Serial.println("[EMU] Ngat nguon ESP");

    while (Serial.available()) Serial.read(); // Bỏ lệnh gõ trong lúc đang chạy
}

void setup()
{
    Serial.begin(USB_BAUD);
    Serial1.setRxBufferSize(1024);
    Serial1.begin(ESP_LOG_BAUD, SERIAL_8N1, PIN_ESP_RX, PIN_ESP_TX_DUMMY);

    pinMode(PIN_ESP_RST, OUTPUT);
    digitalWrite(PIN_ESP_RST, LOW); // ESP-01 tắt
    pinMode(PIN_LEVEL, OUTPUT);
    digitalWrite(PIN_LEVEL, LOW);
    pinMode(PIN_ACK, INPUT_PULLUP);

    Serial.println();
    Serial.println("[EMU] San sang. Gui 'E' = phao can (LOW), 'F' = phao day (HIGH)");
}

void loop()
{
    while (Serial.available())
    {
        const int c = toupper(Serial.read());
        if (c == 'E') runTrigger(false);
        else if (c == 'F') runTrigger(true);
    }
    while (Serial1.available()) Serial1.read(); // Bỏ rác khi ESP-01 đang tắt
}
