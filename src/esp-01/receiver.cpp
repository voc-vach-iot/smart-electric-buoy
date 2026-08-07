#include "receiver.h"
#include <ESP8266WiFi.h>
#include <espnow.h>
#include <WiFiManager.h>
#include <BlynkSimpleEsp8266.h>
#include <WidgetRTC.h>
#include <TimeLib.h>

static WidgetRTC rtc;

String getTimeString()
{
    const String currentTime = String(hour() < 10 ? "0" : "") + hour() + ":" +
        (minute() < 10 ? "0" : "") + minute() + ":" +
        (second() < 10 ? "0" : "") + second();
    return "[" + currentTime + "] ";
}

// ReSharper disable CppParameterMayBeConstPtrOrRef => Vì phải khớp với type callback của esp_now
// ReSharper disable once CppParameterNeverUsed => Dù có thể không dùng hết các tham số, nhưng phải giữ nguyên để đăng ký callback
void OnDataRecv(uint8_t* mac, uint8_t* incomingData, uint8_t len)
{
    struct_message inMsg;
    memcpy(&inMsg, incomingData, sizeof(inMsg));

    if (inMsg.key != SECRET_KEY) return;

    if (inMsg.msg_type == DISCOVERY_REQ)
    {
        struct_message res;
        res.key = SECRET_KEY;
        res.msg_type = DISCOVERY_ACK;

        if (!esp_now_is_peer_exist(mac))
        {
            uint8_t ch = WiFi.channel();
            if (ch == 0) ch = WIFI_CHANNEL;
            esp_now_add_peer(mac, ESP_NOW_ROLE_COMBO, ch, nullptr, 0);
        }
        esp_now_send(mac, reinterpret_cast<uint8_t*>(&res), sizeof(res));
        return;
    }

    if (inMsg.msg_type == DATA_SEND)
    {
        if (!esp_now_is_peer_exist(mac))
        {
            uint8_t ch = WiFi.channel();
            if (ch == 0) ch = WIFI_CHANNEL;
            esp_now_add_peer(mac, ESP_NOW_ROLE_COMBO, ch, nullptr, 0);
        }

        if (inMsg.status == 1)
        {
            digitalWrite(RELAY_PIN, RELAY_ON);
        }
        else
        {
            digitalWrite(RELAY_PIN, RELAY_OFF);
        }

        struct_message res;
        res.key = SECRET_KEY;
        res.msg_type = DATA_ACK;
        esp_now_send(mac, reinterpret_cast<uint8_t*>(&res), sizeof(res));

        if (WiFi.status() == WL_CONNECTED)
        {
            Blynk.virtualWrite(V0, inMsg.status);
            Blynk.virtualWrite(V1, inMsg.battery_voltage * 1.0 / 1000.0);

            uint8_t battery_percentage = map(inMsg.battery_voltage, BATTERY_CRITICAL, BATTERY_MAX, 0, 100);
            battery_percentage = constrain(battery_percentage, 0, 100);
            Blynk.virtualWrite(V2, battery_percentage);

            const String statusStr = inMsg.status == 1 ? "💧 Bồn cạn -> ĐANG BƠM" : "✅ Bồn đầy -> DỪNG BƠM";
            Blynk.virtualWrite(V3, getTimeString() + statusStr + " (Pin: " + String(battery_percentage) + "%)\n");
        }
    }
}

BLYNK_WRITE(V0)
{
    const int manualState = param.asInt();
    if (manualState == 1)
    {
        digitalWrite(RELAY_PIN, RELAY_ON);
    }
    else
    {
        digitalWrite(RELAY_PIN, RELAY_OFF);
    }
    const String logMsg = manualState == 1 ? "👉 Bạn vừa BẬT bơm từ điện thoại" : "👉 Bạn vừa TẶT bơm từ điện thoại";
    Blynk.virtualWrite(V3, getTimeString() + logMsg + "\n");
}

BLYNK_CONNECTED()
{
    rtc.begin();
}

void setup()
{
    Serial.begin(115200);
    digitalWrite(RELAY_PIN, RELAY_OFF);
    pinMode(RELAY_PIN, OUTPUT);

    WiFi.mode(WIFI_AP_STA);

    const String apName = "Voc-Vach-IoT-Phao-" + String(EspClass::getChipId(), HEX);
    WiFi.softAP(apName.c_str());

    if (esp_now_init() == 0)
    {
        esp_now_set_self_role(ESP_NOW_ROLE_COMBO);
        esp_now_register_recv_cb(OnDataRecv);

        uint8_t broadcastMac[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
        esp_now_add_peer(broadcastMac, ESP_NOW_ROLE_COMBO, WIFI_CHANNEL, nullptr, 0);
    }

    WiFiManager wm;
    wm.setConfigPortalTimeout(180);

    if (wm.autoConnect(apName.c_str()))
    {
        Blynk.config(BLYNK_AUTH_TOKEN);
        setSyncInterval(10 * 60);
    }
    else
    {
        wifi_set_channel(WIFI_CHANNEL);
    }
}

void loop()
{
    if (WiFi.status() == WL_CONNECTED)
    {
        Blynk.run();
    }
}
