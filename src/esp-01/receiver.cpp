#include "config.h"
#include <WiFiManager.h>
#include <ESP8266WiFi.h>
#include <espnow.h>
#include <BlynkSimpleEsp8266.h>
#include <WidgetRTC.h>
#include <TimeLib.h>

static WidgetRTC rtc;

// --- CÁC BIẾN ĐỆM XỬ LÝ BẤT ĐỒNG BỘ ESP-NOW ---
static struct_message pendingMsg;
static uint8_t pendingMac[6];
static volatile bool hasPendingData = false;

static String getTimeString()
{
    const String currentTime = String(hour() < 10 ? "0" : "") + hour() + ":" +
        (minute() < 10 ? "0" : "") + minute() + ":" +
        (second() < 10 ? "0" : "") + second();
    return "[" + currentTime + "] ";
}

// Callback ngắt Wi-Fi: Chỉ lưu dữ liệu và bật cờ, KHÔNG xử lý gửi/nhận hay logic chậm ở đây
// ReSharper disable CppParameterMayBeConstPtrOrRef
// ReSharper disable once CppParameterNeverUsed
static void OnDataRecv(uint8_t* mac, uint8_t* incomingData, uint8_t len)
{
    if (len != sizeof(struct_message)) return;

    // Lưu tạm thông tin vào bộ đệm
    memcpy(&pendingMsg, incomingData, sizeof(pendingMsg));
    memcpy(pendingMac, mac, 6);
    hasPendingData = true; // Bật cờ thông báo cho loop()
}

// Hàm xử lý gói tin ESP-NOW được gọi từ loop()
static void handleEspNowData()
{
    if (!hasPendingData) return;
    hasPendingData = false; // Xóa cờ ngay lập tức

    if (pendingMsg.key != SECRET_KEY) return;

    // ----- 1. Xử lý DISCOVERY_REQ -----
    if (pendingMsg.msg_type == DISCOVERY_REQ)
    {
        if (!esp_now_is_peer_exist(pendingMac))
        {
            uint8_t ch = WiFi.channel();
            if (ch == 0) ch = WIFI_CHANNEL;
            esp_now_add_peer(pendingMac, ESP_NOW_ROLE_COMBO, ch, nullptr, 0);
        }

        struct_message res;
        res.key = SECRET_KEY;
        res.msg_type = DISCOVERY_ACK;
        esp_now_send(pendingMac, reinterpret_cast<uint8_t*>(&res), sizeof(res));
        return;
    }

    // ----- 2. Xử lý DATA_SEND -----
    if (pendingMsg.msg_type == DATA_SEND)
    {
        // Đảm bảo peer tồn tại
        if (!esp_now_is_peer_exist(pendingMac))
        {
            uint8_t ch = WiFi.channel();
            if (ch == 0) ch = WIFI_CHANNEL;
            esp_now_add_peer(pendingMac, ESP_NOW_ROLE_COMBO, ch, nullptr, 0);
        }

        // Gửi ACK phản hồi ngay khi ra khỏi ngắt Wi-Fi
        struct_message res;
        res.key = SECRET_KEY;
        res.msg_type = DATA_ACK;
        esp_now_send(pendingMac, reinterpret_cast<uint8_t*>(&res), sizeof(res));

        // Thực hiện điều khiển Relay
        if (pendingMsg.status == 1)
        {
            digitalWrite(RELAY_PIN, RELAY_ON);
        }
        else
        {
            digitalWrite(RELAY_PIN, RELAY_OFF);
        }

        // Cập nhật trạng thái lên Blynk (nếu có kết nối Wi-Fi)
        if (WiFi.status() == WL_CONNECTED)
        {
            Blynk.virtualWrite(V0, pendingMsg.status);
            Blynk.virtualWrite(V1, pendingMsg.battery_voltage * 1.0 / 1000.0);

            uint8_t battery_percentage = map(pendingMsg.battery_voltage, BATTERY_CRITICAL, BATTERY_MAX, 0, 100);
            battery_percentage = constrain(battery_percentage, 0, 100);
            Blynk.virtualWrite(V2, battery_percentage);

            const String statusStr = pendingMsg.status == 1 ? "💧 Bồn cạn -> ĐANG BƠM" : "✅ Bồn đầy -> DỪNG BƠM";
            Blynk.virtualWrite(V3, getTimeString() + statusStr + " (Pin: " + String(battery_percentage) + "%)\n");
        }
    }
}

BLYNK_WRITE (V0)
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
        // Cố định channel Wi-Fi sau khi kết nối
        wifi_set_channel(WIFI_CHANNEL);
    }
    else
    {
        // Không kết nối được WiFi, vẫn dùng channel cố định
        wifi_set_channel(WIFI_CHANNEL);
    }
}

void loop()
{
    // Ưu tiên xử lý các gói tin ESP-NOW chờ gửi ACK trước
    handleEspNowData();

    if (WiFi.status() == WL_CONNECTED)
    {
        Blynk.run();
    }
}
