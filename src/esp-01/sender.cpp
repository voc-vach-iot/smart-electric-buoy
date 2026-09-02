#include "config.h"
#include <ESP8266WiFi.h>
#include <espnow.h>
#include <EEPROM.h>
#include <user_interface.h>

ADC_MODE(ADC_VCC);

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
static uint8_t broadcastMac[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// Lưu config vào EEPROM (Tránh ghi lặp dữ liệu cũ)
static void saveConfigToEEPROM(const uint8_t* mac, const uint8_t channel)
{
    if (sysConfig.magic == EEPROM_MAGIC &&
        sysConfig.channel == channel &&
        memcmp(sysConfig.mac, mac, 6) == 0)
    {
        DBG_PRINTLN("Dbg: Config unchanged, skip writing to Flash.");
        return;
    }

    sysConfig.magic = EEPROM_MAGIC;
    memcpy(sysConfig.mac, mac, 6);
    sysConfig.channel = channel;

    EEPROM.begin(EEPROM_SIZE);
    EEPROM.put(EEPROM_ADDR, sysConfig);
    const bool ok = EEPROM.commit();
    EEPROM.end();

    DBG_PRINTF("Dbg: saveConfigToEEPROM() - MAC: %02X:%02X:%02X:%02X:%02X:%02X | CH: %d\n",
               sysConfig.mac[0], sysConfig.mac[1], sysConfig.mac[2],
               sysConfig.mac[3], sysConfig.mac[4], sysConfig.mac[5],
               sysConfig.channel);
    if (!ok)
    {
        DBG_PRINTLN("Dbg: EEPROM commit FAILED!");
    }
    else
    {
        DBG_PRINTLN("Dbg: EEPROM commit SUCCESS!");
    }
}

// Đọc cấu hình từ EEPROM
static bool loadConfigFromEEPROM()
{
    EEPROM.begin(EEPROM_SIZE);
    EEPROM.get(EEPROM_ADDR, sysConfig);
    EEPROM.end();

    if (sysConfig.magic == EEPROM_MAGIC && sysConfig.channel >= 1 && sysConfig.channel <= 13)
    {
        DBG_PRINTF("Dbg: Loaded EEPROM - MAC: %02X:%02X:%02X:%02X:%02X:%02X | CH: %d\n",
                   sysConfig.mac[0], sysConfig.mac[1], sysConfig.mac[2],
                   sysConfig.mac[3], sysConfig.mac[4], sysConfig.mac[5],
                   sysConfig.channel);
        return true;
    }

    DBG_PRINTLN("Dbg: EEPROM empty or invalid!");
    return false;
}

// Callback xử lý gói tin nhận về (ACK)
// ReSharper disable CppParameterMayBeConstPtrOrRef => Vì phải khớp với type callback của esp_now
// ReSharper disable once CppParameterNeverUsed => Dù có thể không dùng hết các tham số, nhưng phải giữ nguyên để đăng ký callback
static void OnDataRecv(uint8_t* mac, uint8_t* incomingData, const uint8_t len)
{
    if (len != sizeof(struct_message)) return;

    struct_message inMsg;
    memcpy(&inMsg, incomingData, sizeof(inMsg));
    if (inMsg.key != SECRET_KEY) return;

    if (inMsg.msg_type == DISCOVERY_ACK || inMsg.msg_type == DATA_ACK)
    {
        memcpy(sysConfig.mac, mac, 6);
        deliverySuccess = true;
        DBG_PRINTF("Dbg: ACK received from %02X:%02X:%02X:%02X:%02X:%02X\n",
                   mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    }
}

// Đăng ký Peer ESP-NOW
static bool addPeerWithRetry(uint8_t* mac, const uint8_t channel)
{
    if (const int res = esp_now_add_peer(mac, ESP_NOW_ROLE_COMBO, channel, nullptr, 0); res != 0)
    {
        DBG_PRINTF("Dbg: Add peer failed, code %d\n", res);
        return false;
    }
    DBG_PRINTF("Dbg: Add peer OK (MAC: %02X:%02X... CH:%d)\n", mac[0], mac[1], channel);
    return true;
}

// Quét toàn bộ 13 channel Wi-Fi để tìm Receiver
static bool scanAndDiscover()
{
    DBG_PRINTLN("Dbg: --- STARTING SCAN (13->1) ---");
    myData.msg_type = DISCOVERY_REQ;

    for (int c = 13; c >= 1; c--)
    {
        DBG_PRINTF("Dbg: Scan CH %d...\n", c);
        wifi_set_channel(c);
        delay(CHANNEL_SETTLE_DELAY_MS);

        if (!addPeerWithRetry(broadcastMac, c))
            continue;

        deliverySuccess = false;
        esp_now_send(broadcastMac, reinterpret_cast<uint8_t*>(&myData), sizeof(myData));
        DBG_PRINTLN("Dbg: esp_now_send called");

        const unsigned long wait = millis();
        while (millis() - wait < SCAN_ACK_TIMEOUT_MS)
        {
            if (deliverySuccess)
            {
                DBG_PRINTF("Dbg: Found receiver on CH %d!\n", c);
                saveConfigToEEPROM(sysConfig.mac, c);
                esp_now_del_peer(broadcastMac);
                return true;
            }
            yield();
        }
        DBG_PRINTF("Dbg: No ACK on CH %d\n", c);
        esp_now_del_peer(broadcastMac);
    }
    DBG_PRINTLN("Dbg: Scan complete - no response.");
    return false;
}

// Đọc trạng thái phao
static BuoyStatus getStatus()
{
    const bool rawVal = digitalRead(PIN_LEVEL_INPUT);
    DBG_PRINTF("Dbg: PIN_LEVEL_INPUT Raw = %d\n", rawVal);
    return rawVal == STATUS_FULL ? PUMP_OFF : PUMP_ON;
}

// Tắt vi điều khiển an toàn (Tránh đơ Watchdog)
[[noreturn]] static void finishAndShutdown()
{
    EspClass::wdtFeed();

    DBG_PRINTLN("Dbg: Finishing, pull ACK low...");
    pinMode(PIN_MCU_ACK, OUTPUT);
    digitalWrite(PIN_MCU_ACK, LOW);
    delay(SHUTDOWN_DELAY_MS);

    EspClass::wdtFeed();
    WiFi.disconnect(true);
    EspClass::wdtFeed();
    WiFi.mode(WIFI_OFF);
    EspClass::wdtFeed();

    DBG_FLUSH();
    EspClass::wdtFeed();

    while (true)
    {
        yield();
        EspClass::wdtFeed();
    }
}

void setup()
{
    // Giải phóng RX
    Serial.end();
    pinMode(PIN_MCU_ACK, INPUT);
    pinMode(PIN_LEVEL_INPUT, INPUT);
    delay(2);

    // Giảm công suất RF để tiết kiệm pin
    WiFi.setOutputPower(17);

    DBG_BEGIN(74880, SERIAL_8N1, SERIAL_TX_ONLY);
    DBG_PRINTLN("\n\n--- SENDER (NO RTC) ---");

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
    for (int i = 0; i < 4; i++) vcc_acc += EspClass::getVcc();
    uint32_t avg = vcc_acc / 4 + VCC_SS8550_OFFSET;
    if (avg > BATTERY_MAX) avg = BATTERY_MAX;
    if (avg < BATTERY_CRITICAL) avg = BATTERY_CRITICAL;
    myData.battery_voltage = avg;
    DBG_PRINTF("Dbg: VCC = %d mV\n", myData.battery_voltage);

    // Đọc trạng thái phao
    myData.status = getStatus();
    DBG_PRINTF("Dbg: Status to send: %s\n", myData.status == PUMP_ON ? "ON" : "OFF");

    // ---------- 1. QUICK SEND ----------
    if (loadConfigFromEEPROM())
    {
        DBG_PRINTF("Dbg: Quick send on CH %d...\n", sysConfig.channel);
        wifi_set_channel(sysConfig.channel);
        delay(CHANNEL_SETTLE_DELAY_MS);

        if (addPeerWithRetry(sysConfig.mac, sysConfig.channel))
        {
            for (uint8_t retry = 0; retry < QUICK_SEND_RETRIES; retry++)
            {
                DBG_PRINTF("Dbg: Quick retry %d/%d\n", retry + 1, QUICK_SEND_RETRIES);
                deliverySuccess = false;
                esp_now_send(sysConfig.mac, reinterpret_cast<uint8_t*>(&myData), sizeof(myData));
                DBG_PRINTLN("Dbg: esp_now_send called");

                const unsigned long start = millis();
                while (millis() - start < ACK_TIMEOUT_MS)
                {
                    if (deliverySuccess)
                    {
                        DBG_PRINTLN("Dbg: Quick send SUCCESS! ACK received.");
                        finishAndShutdown();
                    }
                    yield();
                    EspClass::wdtFeed();
                }
                DBG_PRINTF("Dbg: Quick retry %d - no ACK\n", retry + 1);
                delay(RETRY_DELAY_MS);
            }
        }
        DBG_PRINTLN("Dbg: Quick send FAILED (no ACK or peer error).");
    }

    // ---------- 2. SCAN & DATA SEND ----------
    if (!deliverySuccess)
    {
        if (scanAndDiscover())
        {
            DBG_PRINTLN("Dbg: Found receiver, sending DATA_SEND...");
            myData.msg_type = DATA_SEND;
            wifi_set_channel(sysConfig.channel);
            delay(CHANNEL_SETTLE_DELAY_MS);

            if (addPeerWithRetry(sysConfig.mac, sysConfig.channel))
            {
                for (uint8_t retry = 0; retry < DATA_SEND_RETRIES; retry++)
                {
                    DBG_PRINTF("Dbg: Data retry %d/%d\n", retry + 1, DATA_SEND_RETRIES);
                    deliverySuccess = false;
                    esp_now_send(sysConfig.mac, reinterpret_cast<uint8_t*>(&myData), sizeof(myData));
                    DBG_PRINTLN("Dbg: esp_now_send called");

                    const unsigned long start = millis();
                    while (millis() - start < ACK_TIMEOUT_MS)
                    {
                        if (deliverySuccess)
                        {
                            DBG_PRINTLN("Dbg: DATA_SEND after scan SUCCESS!");
                            finishAndShutdown();
                        }
                        yield();
                    }
                    DBG_PRINTF("Dbg: Data retry %d - no ACK\n", retry + 1);
                    delay(RETRY_DELAY_MS);
                }
                DBG_PRINTLN("Dbg: DATA_SEND after scan FAILED (no ACK).");
            }
        }
        else
        {
            DBG_PRINTLN("Dbg: Scan failed, receiver likely OFF.");
        }
    }

    DBG_PRINTLN("Dbg: All attempts failed. Shutting down.");
    finishAndShutdown();
}

void loop()
{
}
