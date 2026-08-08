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

// Định nghĩa chân cắm cho Sender
#ifdef SENDER_MODE
#define PIN_MCU_ACK 2
#define PIN_LEVEL_INPUT 3
#define PIN_MODE INPUT
#define STATUS_FULL HIGH
#define STATUS_EMPTY LOW
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

#define BATTERY_MAX      3300 // Điện áp sau ổn áp (hoặc pin đã vơi)
#define BATTERY_WARNING  2800 // Mức nên báo về điện thoại để đi sạc
#define BATTERY_CRITICAL 2600 // Mức cực kỳ nguy hiểm, nên ngắt hệ thống

#define BLYNK_TEMPLATE_ID "TMPL6fHo4wONF"
#define BLYNK_TEMPLATE_NAME "Bơm điện thông minh"
#define BLYNK_AUTH_TOKEN "FgKNLL0eg-YfPt7_6EMmGB27idL8eZ6w"
#endif

#ifdef DEBUG_MODE
#define DBG_PRINT(...) Serial.print(__VA_ARGS__)
#define DBG_PRINTLN(...) Serial.println(__VA_ARGS__)
#define DBG_PRINTF(...) Serial.printf(__VA_ARGS__)
#define DBG_BEGIN(...) Serial.begin(__VA_ARGS__)
#else
#define DBG_PRINT(...)
#define DBG_PRINTLN(...)
#define DBG_PRINTF(...)
#define DBG_BEGIN(...)
#endif

#endif // CONFIG_H
