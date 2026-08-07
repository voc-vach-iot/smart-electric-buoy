#include "sender.h"
#include <ESP8266WiFi.h>
#include <espnow.h>
#include <user_interface.h>

ADC_MODE(ADC_VCC);

#define DEEP_SLEEP_TIME_US 0

static uint8_t targetMac[6];
static uint8_t currentChannel = WIFI_CHANNEL;
static struct_message myData;
static volatile bool deliverySuccess = false;

void saveConfigToRTC(const uint8_t* mac, const uint8_t channel)
{
    uint32_t part1, part2;
    memcpy(&part1, mac, 4);
    memcpy(&part2, mac + 4, 2);
    part2 |= static_cast<uint32_t>(channel) << 16;
    system_rtc_mem_write(RTC_ADDR, &part1, 4);
    system_rtc_mem_write(RTC_ADDR + 1, &part2, 4);
}

void loadConfigFromRTC()
{
    uint32_t part1, part2;
    system_rtc_mem_read(RTC_ADDR, &part1, 4);
    system_rtc_mem_read(RTC_ADDR + 1, &part2, 4);
    memcpy(targetMac, &part1, 4);
    memcpy(targetMac + 4, &part2, 2);
    currentChannel = part2 >> 16 & 0xFF;
}

// ReSharper disable CppParameterMayBeConstPtrOrRef => Vì phải khớp với type callback của esp_now
// ReSharper disable once CppParameterNeverUsed => Dù có thể không dùng hết các tham số, nhưng phải giữ nguyên để đăng ký callback
void OnDataRecv(uint8_t* mac, uint8_t* incomingData, const uint8_t len)
{
    struct_message inMsg;
    if (len < sizeof(inMsg)) return;
    memcpy(&inMsg, incomingData, sizeof(inMsg));
    if (inMsg.key != SECRET_KEY) return;

    if (inMsg.msg_type == DISCOVERY_ACK || inMsg.msg_type == DATA_ACK)
    {
        memcpy(targetMac, mac, 6);
        deliverySuccess = true;
        DBG_PRINTLN("Dbg: ACK Received!");
    }
}

bool scanAndDiscover()
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
        while (millis() - wait < 250)
        {
            if (deliverySuccess)
            {
                currentChannel = c;
                saveConfigToRTC(targetMac, currentChannel);
                DBG_PRINTF("Dbg: Found on CH %d\n", c);
                return true;
            }
            yield();
        }
        esp_now_del_peer(broadcastMac);
    }
    return false;
}

BuoyStatus getStatus()
{
    const bool rawVal = digitalRead(PIN_SENSOR);
    DBG_PRINTF("Dbg: PIN_SENSOR Raw = %d (Expect EMPTY=%d)\n", rawVal, STATUS_EMPTY);

    if (rawVal == STATUS_EMPTY)
    {
        return PUMP_ON;
    }
    return PUMP_OFF;
}

[[noreturn]] void enterDeepSleep()
{
    DBG_PRINTLN("\nDbg: Entering Deep Sleep mode...");
    Serial1.flush();

    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);

    EspClass::deepSleep(DEEP_SLEEP_TIME_US, WAKE_RF_DEFAULT);

    while (true)
    {
        yield();
    }
}

void setup()
{
    Serial.end();

    DBG_BEGIN(74880);
    DBG_PRINTLN("\n\n--- SENDER DEBUG (DeepSleep Mode) ---");

    pinMode(PIN_SENSOR, INPUT);

    uint8_t count = 0;
    while (digitalRead(PIN_SENSOR) == LOW)
    {
        if (count++ > 20) break;
        delay(100);
    }

    pinMode(PIN_SENSOR, PIN_MODE);
    DBG_PRINTF("Dbg: PIN_SENSOR configured on GPIO %d\n", PIN_SENSOR);

    WiFi.mode(WIFI_STA);
    WiFi.disconnect();

    if (esp_now_init() != 0)
    {
        DBG_PRINTLN("Dbg: ESP-NOW Init Fail");
        enterDeepSleep();
    }

    esp_now_set_self_role(ESP_NOW_ROLE_COMBO);
    esp_now_register_recv_cb(OnDataRecv);

    loadConfigFromRTC();

    myData.key = SECRET_KEY;
    myData.msg_type = DATA_SEND;

    DBG_PRINTLN("Dbg: Waiting for voltage stability...");
    for (int i = 0; i < 5; i++)
    {
        DBG_PRINTF("Dbg: Current RX: %d\n", digitalRead(PIN_SENSOR));
        delay(100);
    }

    uint32_t vcc_acc = 0;
    for (int i = 0; i < 4; i++)
    {
        vcc_acc += EspClass::getVcc();
        delay(10);
    }
    myData.battery_voltage = vcc_acc / 4;
    DBG_PRINTF("Dbg: Calibrated VCC = %d mV\n", myData.battery_voltage);

    myData.status = getStatus();
    DBG_PRINTF("Dbg: Status to send: %s\n", myData.status == PUMP_ON ? "ON" : "OFF");

    if (currentChannel >= 1 && currentChannel <= 13)
    {
        DBG_PRINTF("Dbg: Quick send CH %d\n", currentChannel);
        wifi_set_channel(currentChannel);
        esp_now_add_peer(targetMac, ESP_NOW_ROLE_COMBO, currentChannel, nullptr, 0);
        esp_now_send(targetMac, reinterpret_cast<uint8_t*>(&myData), sizeof(myData));

        const unsigned long startWait = millis();
        while (millis() - startWait < 400)
        {
            if (deliverySuccess)
            {
                DBG_PRINTLN("Dbg: Quick Success. Going to Deep Sleep.");
                delay(50);
                enterDeepSleep();
            }
            yield();
        }
    }

    if (!deliverySuccess)
    {
        if (scanAndDiscover())
        {
            esp_now_add_peer(targetMac, ESP_NOW_ROLE_COMBO, currentChannel, nullptr, 0);
            esp_now_send(targetMac, reinterpret_cast<uint8_t*>(&myData), sizeof(myData));
            delay(100);
        }
        else
        {
            DBG_PRINTLN("Dbg: Scan Failed.");
        }
    }

    DBG_PRINTLN("Dbg: Final Deep Sleep.");
    delay(50);
    enterDeepSleep();
}

void loop()
{
}
