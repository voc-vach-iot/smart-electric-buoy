#include "config.h"
#include <ESP8266WiFi.h>
#include <espnow.h>
#include <EEPROM.h>
#include <user_interface.h>

ADC_MODE(ADC_VCC);

#define EEPROM_SIZE 32
#define EEPROM_ADDR 0
#define EEPROM_MAGIC 0xAB // Byte kiểm tra xem EEPROM đã lưu dữ liệu hợp lệ chưa

namespace
{
    struct ConfigData
    {
        uint8_t magic;
        uint8_t mac[6];
        uint8_t channel;
    };
}

static ConfigData sysConfig;
static struct_message myData;
static volatile bool deliverySuccess = false;

// 🛠️ ĐỌC / GHI CẤU HÌNH BẰNG EEPROM (FLASH) THAY CHO RTC RAM
static void saveConfigToEEPROM(const uint8_t* mac, const uint8_t channel)
{
    // 1. Nếu dữ liệu mới GIỐNG HỆT dữ liệu cũ đang lưu thì BỎ QUA, không ghi Flash
    if (sysConfig.magic == EEPROM_MAGIC &&
        sysConfig.channel == channel &&
        memcmp(sysConfig.mac, mac, 6) == 0)
    {
        DBG_PRINTLN("Dbg: Config unchanged, skip writing to Flash.");
        return;
    }

    // 2. Chỉ ghi khi Kênh hoặc MAC thực sự thay đổi
    sysConfig.magic = EEPROM_MAGIC;
    memcpy(sysConfig.mac, mac, 6);
    sysConfig.channel = channel;

    EEPROM.begin(EEPROM_SIZE);
    EEPROM.put(EEPROM_ADDR, sysConfig);
    EEPROM.commit(); // Thao tác này mới thực sự tốn 1 lần Write Cycle
    EEPROM.end();
    DBG_PRINTLN("Dbg: Saved NEW config to Flash/EEPROM!");
}

static bool loadConfigFromEEPROM()
{
    EEPROM.begin(EEPROM_SIZE);
    EEPROM.get(EEPROM_ADDR, sysConfig);
    EEPROM.end();

    if (sysConfig.magic == EEPROM_MAGIC && sysConfig.channel >= 1 && sysConfig.channel <= 13)
    {
        DBG_PRINTF("Dbg: Loaded EEPROM - CH: %d\n", sysConfig.channel);
        return true;
    }

    DBG_PRINTLN("Dbg: EEPROM empty or invalid!");
    return false;
}

// ReSharper disable CppParameterMayBeConstPtrOrRef => Vì phải khớp với type callback của esp_now
// ReSharper disable once CppParameterNeverUsed => Dù có thể không dùng hết các tham số, nhưng phải giữ nguyên để đăng ký callback
static void OnDataRecv(uint8_t* mac, uint8_t* incomingData, const uint8_t len)
{
    struct_message inMsg;
    if (len < sizeof(inMsg)) return;
    memcpy(&inMsg, incomingData, sizeof(inMsg));
    if (inMsg.key != SECRET_KEY) return;

    if (inMsg.msg_type == DISCOVERY_ACK || inMsg.msg_type == DATA_ACK)
    {
        memcpy(sysConfig.mac, mac, 6);
        deliverySuccess = true;
        DBG_PRINTLN("Dbg: ACK Received!");
    }
}

static bool scanAndDiscover()
{
    DBG_PRINTLN("Dbg: Starting Scan...");
    uint8_t broadcastMac[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    myData.msg_type = DISCOVERY_REQ;

    for (int c = 1; c <= 13; c++)
    {
        wifi_set_channel(c);
        esp_now_add_peer(broadcastMac, ESP_NOW_ROLE_COMBO, c, nullptr, 0);
        esp_now_send(broadcastMac, reinterpret_cast<uint8_t*>(&myData), sizeof(myData));

        const unsigned long wait = millis();
        while (millis() - wait < 40)
        {
            if (deliverySuccess)
            {
                saveConfigToEEPROM(sysConfig.mac, c);
                DBG_PRINTF("Dbg: Found on CH %d\n", c);
                return true;
            }
            yield();
        }
        esp_now_del_peer(broadcastMac);
    }
    return false;
}

// Đọc trực tiếp mức logic ghim từ CH32 qua chân RX (GPIO3)
static BuoyStatus getStatus()
{
    const bool rawVal = digitalRead(PIN_LEVEL_INPUT);
    DBG_PRINTF("Dbg: PIN_LEVEL_INPUT Raw = %d\n", rawVal);

    if (rawVal == STATUS_FULL)
    {
        return PUMP_OFF; // Mức HIGH từ CH32 -> Bồn đầy -> Báo TẮT BƠM
    }
    return PUMP_ON; // Mức LOW từ CH32 -> Bồn cạn -> Báo BẬT BƠM
}

// 🛠️ HÀM BÁO HOÀN THÀNH VÀ CHỜ CH32 TẮT NGUỒN
[[noreturn]] static void finishAndShutdown()
{
    DBG_PRINTLN("\nDbg: Gui tin hieu FINISH cho CH32 de ngat nguon EN...");

    // 1. Dập chân ACK (GPIO2) xuống LOW để báo CH32 biết việc đã xong
    pinMode(PIN_MCU_ACK, OUTPUT);
    digitalWrite(PIN_MCU_ACK, LOW);

    // 2. Tắt các tiến trình WiFi
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);

    // 3. Đứng chờ CH32 dập chân EN ngắt nguồn hoàn toàn
    while (true)
    {
        yield();
    }
}

void setup()
{
    // 🛠️ BƯỚC 1: Khóa chân ACK (GPIO2) ở INPUT để bảo vệ Bootrom
    pinMode(PIN_MCU_ACK, INPUT);

    // 🛠️ BƯỚC 2: Giải phóng chân RX (GPIO3) làm chân Digital Input
    Serial.end();
    pinMode(PIN_LEVEL_INPUT, INPUT);
    delay(2);

    DBG_BEGIN(74880);
    DBG_PRINTLN("\n\n--- SENDER ĐÃ TỐI ƯU CÙNG CH32V003 (NO RTC) ---");

    // Khởi tạo ESP-NOW
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();

    if (esp_now_init() != 0)
    {
        DBG_PRINTLN("Dbg: ESP-NOW Init Fail");
        finishAndShutdown();
    }

    esp_now_set_self_role(ESP_NOW_ROLE_COMBO);
    esp_now_register_recv_cb(OnDataRecv);

    myData.key = SECRET_KEY;
    myData.msg_type = DATA_SEND;

    // Đo điện áp VCC
    uint32_t vcc_acc = 0;
    for (int i = 0; i < 4; i++)
    {
        vcc_acc += EspClass::getVcc();
    }
    myData.battery_voltage = vcc_acc / 4;
    DBG_PRINTF("Dbg: VCC = %d mV\n", myData.battery_voltage);

    // Đọc trạng thái phao ghim sẵn từ CH32
    myData.status = getStatus();
    DBG_PRINTF("Dbg: Status to send: %s\n", myData.status == PUMP_ON ? "ON" : "OFF");

    // Lấy kênh WiFi & MAC đã lưu từ EEPROM

    // Nếu đã có cấu hình trong EEPROM -> Gửi nhanh
    if (loadConfigFromEEPROM())
    {
        DBG_PRINTF("Dbg: Quick send CH %d\n", sysConfig.channel);
        wifi_set_channel(sysConfig.channel);
        esp_now_add_peer(sysConfig.mac, ESP_NOW_ROLE_COMBO, sysConfig.channel, nullptr, 0);

        for (uint8_t retry = 0; retry < 3; retry++)
        {
            esp_now_send(sysConfig.mac, reinterpret_cast<uint8_t*>(&myData), sizeof(myData));

            const unsigned long startWait = millis();
            while (millis() - startWait < 80)
            {
                if (deliverySuccess)
                {
                    DBG_PRINTLN("Dbg: Quick Success.");
                    finishAndShutdown(); // Dập ACK LOW & chờ CH32 cắt nguồn EN
                }
                yield();
            }
            if (deliverySuccess) break;
        }
    }

    // Nếu gửi nhanh thất bại hoặc chưa có EEPROM -> Quét 13 kênh
    if (!deliverySuccess)
    {
        if (scanAndDiscover())
        {
            myData.msg_type = DATA_SEND;
            esp_now_add_peer(sysConfig.mac, ESP_NOW_ROLE_COMBO, sysConfig.channel, nullptr, 0);
            esp_now_send(sysConfig.mac, reinterpret_cast<uint8_t*>(&myData), sizeof(myData));
            delay(40);
        }
        else
        {
            DBG_PRINTLN("Dbg: Scan Failed.");
        }
    }

    // Hoàn thành nhiệm vụ -> Báo CH32 ngắt nguồn
    finishAndShutdown();
}

void loop()
{
}
