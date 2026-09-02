#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// #define DEBUG_MODE 

#define WIFI_CHANNEL 6
#define SECRET_KEY 0x00000000
#define RTC_ADDR 64

enum MessageType
{
    DISCOVERY_REQ,
    DISCOVERY_ACK,
    DATA_SEND, // Sender gửi trạng thái phao
    DATA_ACK // Receiver báo: "Đã nhận và thực thi"
};

enum BuoyStatus
{
    PUMP_OFF,
    PUMP_ON
};

typedef struct struct_message
{
    uint32_t key;
    uint8_t msg_type;
    uint8_t status;
    uint16_t battery_voltage; // Giá trị pin tính theo mV
} struct_message;

#define BATTERY_MAX      3300 // Điện áp sau ổn áp (hoặc pin đã vơi)
#define BATTERY_WARNING  2800 // Mức nên báo về điện thoại để đi sạc
#define BATTERY_CRITICAL 2600 // Mức cực kỳ nguy hiểm, nên ngắt hệ thống

// Định nghĩa chân cắm cho Sender
#ifdef SENDER_MODE
#define PIN_MCU_ACK 0
#define PIN_LEVEL_INPUT 3
#define PIN_MODE INPUT
#define STATUS_FULL HIGH
#define STATUS_EMPTY LOW
#define VCC_SS8550_OFFSET 175

// ============================================================================
// CẤU HÌNH THỜI GIAN VÀ RETRY (DỄ DÀNG ĐIỀU CHỈNH)
// ============================================================================
#define EEPROM_SIZE             32
#define EEPROM_ADDR             0
#define EEPROM_MAGIC            0xAB

#define QUICK_SEND_RETRIES      5     // Số lần thử gửi nhanh (Quick Send)
#define DATA_SEND_RETRIES       2     // Số lần thử gửi dữ liệu sau khi Scan
#define ACK_TIMEOUT_MS          15    // Thời gian chờ ACK cho mỗi lượt gửi (ms)
#define SCAN_ACK_TIMEOUT_MS     20    // Thời gian chờ ACK khi đang Quét Kênh (ms)
#define RETRY_DELAY_MS          5     // Khoảng nghỉ giữa các lần thử lại (ms)
#define CHANNEL_SETTLE_DELAY_MS 1     // Thời gian chờ sau khi đổi Channel Wi-Fi (ms)
#define SHUTDOWN_DELAY_MS       50    // Thời gian giữ chân ACK trước khi tắt hoàn toàn (ms)
#endif

// Định nghĩa chân cắm cho Receiver
#ifdef RECEIVER_MODE
#define RELAY_PIN 0
#define RELAY_ACTIVE_LOW

#ifdef RELAY_ACTIVE_LOW
#define RELAY_ON  LOW
#define RELAY_OFF HIGH
#else
#define RELAY_ON  HIGH
#define RELAY_OFF LOW
#endif

#define BLYNK_TEMPLATE_ID "***REMOVED***"
#define BLYNK_TEMPLATE_NAME "Bơm điện thông minh"
#define BLYNK_AUTH_TOKEN "***REMOVED***"
#endif

#ifdef DEBUG_MODE
#define DBG_PRINT(...) Serial.print(__VA_ARGS__)
#define DBG_PRINTLN(...) Serial.println(__VA_ARGS__)
#define DBG_PRINTF(...) Serial.printf(__VA_ARGS__)
#define DBG_BEGIN(...) Serial.begin(__VA_ARGS__)
#define DBG_FLUSH(...) Serial.flush(__VA_ARGS__)
#else
#define DBG_PRINT(...)
#define DBG_PRINTLN(...)
#define DBG_PRINTF(...)
#define DBG_BEGIN(...)
#define DBG_FLUSH(...)
#endif

#endif // CONFIG_H
