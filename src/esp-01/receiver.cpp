#include "config.h"
#include <WiFiManager.h>
#include <ESP8266WiFi.h>
#include <espnow.h>
#include <BlynkSimpleEsp8266.h>
#include <WidgetRTC.h>
#include <TimeLib.h>

static WidgetRTC rtc;

// --- CÁC BIẾN LƯU TRẠNG THÁI VÀ BỘ ĐỆM ESP-NOW ---
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

// Callback ngắt Wi-Fi: Đẩy nhanh dữ liệu vào bộ đệm Rx
// ReSharper disable CppParameterMayBeConstPtrOrRef => Vì phải khớp với type callback của esp_now
// ReSharper disable once CppParameterNeverUsed => Dù có thể không dùng hết các tham số, nhưng phải giữ nguyên để đăng ký callback
static void OnDataRecv(uint8_t* mac, uint8_t* incomingData, const uint8_t len)
{
    if (len != sizeof(struct_message)) return;

    memcpy(&pendingMsg, incomingData, sizeof(pendingMsg));
    memcpy(pendingMac, mac, 6);
    hasPendingData = true;
}

// Xử lý gói tin ESP-NOW
static void handleEspNowData()
{
    if (!hasPendingData) return;
    hasPendingData = false;

    if (pendingMsg.key != SECRET_KEY) return;

    // ----- 1. DISCOVERY_REQ -----
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

    // ----- 2. DATA_SEND -----
    if (pendingMsg.msg_type == DATA_SEND)
    {
        if (!esp_now_is_peer_exist(pendingMac))
        {
            uint8_t ch = WiFi.channel();
            if (ch == 0) ch = WIFI_CHANNEL;
            esp_now_add_peer(pendingMac, ESP_NOW_ROLE_COMBO, ch, nullptr, 0);
        }

        // PHẢN HỒI ACK TỨC THÌ
        struct_message res;
        res.key = SECRET_KEY;
        res.msg_type = DATA_ACK;
        esp_now_send(pendingMac, reinterpret_cast<uint8_t*>(&res), sizeof(res));

        // Điều khiển Relay
        if (pendingMsg.status == 1)
        {
            digitalWrite(RELAY_PIN, RELAY_ON);
        }
        else
        {
            digitalWrite(RELAY_PIN, RELAY_OFF);
        }

        // Cập nhật trạng thái lên Blynk
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

BLYNK_WRITE(V0)
{
    const int manualState = param.asInt();
    digitalWrite(RELAY_PIN, (manualState == 1) ? RELAY_ON : RELAY_OFF);
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
        // 1. GIẢM NHIỆT: Tắt Access Point, chuyển hẳn sang Client
        WiFi.softAPdisconnect(true);
        WiFi.mode(WIFI_STA);

        // 2. TỐI ƯU SÓNG: Giữ phần cứng RF mở 24/7 (Không miss gói tin nào từ Sender)
        WiFi.setSleepMode(WIFI_NONE_SLEEP);

        // 3. GIẢM NHIỆT: Hạ công suất phát Wi-Fi xuống 17.5 dBm vừa đủ mát
        WiFi.setOutputPower(17.5);

        Blynk.config(BLYNK_AUTH_TOKEN);
        setSyncInterval(10 * 60);
    }

    // Cố định lại channel Wi-Fi
    wifi_set_channel(WIFI_CHANNEL);
}

void loop()
{
    // Ưu tiên xử lý các gói tin ESP-NOW trong bộ đệm
    handleEspNowData();

    // Run Blynk
    if (WiFi.status() == WL_CONNECTED)
    {
        Blynk.run();
    }

    // GIẢM NHIỆT CPU: Cho CPU nghỉ 1ms mỗi lượt lặp (RF vẫn nhận sóng bình thường)
    delay(1);
}
