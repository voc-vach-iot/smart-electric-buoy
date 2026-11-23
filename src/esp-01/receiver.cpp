#include "config.h"
#ifdef OTA_ENABLE
#include <ArduinoOTA.h>
#else
#include <WiFiManager.h>
#endif
#include <ESP8266WiFi.h>
#include <espnow.h>
#include <BlynkSimpleEsp8266.h>
#include <WidgetRTC.h>
#include <TimeLib.h>

// OTA_ENABLE: chế độ nạp TẠM qua Wi-Fi (SSID cố định, ArduinoOTA, không WiFiManager).
// Không định nghĩa (env esp01s_receiver_uart): firmware gốc dùng WiFiManager như trước.

static WidgetRTC rtc;

// --- CÁC BIẾN LƯU TRẠNG THÁI VÀ BỘ ĐỆM ESP-NOW ---
static struct_message pendingMsg;
static uint8_t pendingMac[6];
static volatile bool hasPendingData = false;

static bool pumpOn = false;

// Bật/tắt bơm và ghi nhận trạng thái hiện tại
static void setPump(const bool on)
{
    pumpOn = on;
    digitalWrite(RELAY_PIN, on ? RELAY_ON : RELAY_OFF);
}

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
        setPump(pendingMsg.status == 1);

        // Cập nhật trạng thái lên Blynk
        if (WiFi.status() == WL_CONNECTED)
        {
            Blynk.virtualWrite(V0, pendingMsg.status);
            Blynk.virtualWrite(V1, pendingMsg.battery_voltage * 1.0 / 1000.0);

            // Tính bằng long rồi mới giới hạn, kẻo giá trị âm (dưới BATTERY_EMPTY_MV) tràn khi ép sang uint8_t
            const long percentage = map(pendingMsg.battery_voltage, BATTERY_EMPTY_MV, BATTERY_MAX, 0, 100);
            const uint8_t battery_percentage = constrain(percentage, 0, 100);
            Blynk.virtualWrite(V2, battery_percentage);

            const String statusStr = pendingMsg.status == 1 ? "💧 Bồn cạn -> ĐANG BƠM" : "✅ Bồn đầy -> DỪNG BƠM";
            Blynk.virtualWrite(V3, getTimeString() + statusStr + " (Pin: " + String(battery_percentage) + "%)\n");
        }
    }
}

// Đọc nút IO2 (nhấn = LOW). Mỗi lần nhấn đảo trạng thái bơm
// Lưu ý: gói ESP-NOW tiếp theo từ phao sẽ ghi đè trạng thái do nút đặt (chủ ý)
static void handleButton()
{
    static bool lastStable = HIGH;
    static bool lastRead = HIGH;
    static uint32_t lastChange = 0;

    const bool reading = digitalRead(BUTTON_PIN);
    if (reading != lastRead)
    {
        lastRead = reading;
        lastChange = millis();
    }

    if (millis() - lastChange < BUTTON_DEBOUNCE_MS || reading == lastStable) return;
    lastStable = reading;

    if (lastStable == LOW) // Cạnh nhấn xuống
    {
        setPump(!pumpOn);
        if (WiFi.status() == WL_CONNECTED)
        {
            Blynk.virtualWrite(V0, pumpOn ? 1 : 0);
            const String logMsg = pumpOn ? "🔘 Bạn vừa BẬT bơm bằng nút" : "🔘 Bạn vừa TẮT bơm bằng nút";
            Blynk.virtualWrite(V3, getTimeString() + logMsg + "\n");
        }
    }
}

#ifdef OTA_ENABLE
static uint32_t lastWiFiAttempt = 0;
static bool servicesStarted = false;

// Kết nối Wi-Fi cố định, chờ tối đa WIFI_CONNECT_TIMEOUT_MS (vẫn đọc nút trong lúc chờ)
static bool connectWiFi()
{
    lastWiFiAttempt = millis();
    WiFi.begin(WIFI_SSID, WIFI_PASS);

    while (WiFi.status() != WL_CONNECTED && millis() - lastWiFiAttempt < WIFI_CONNECT_TIMEOUT_MS)
    {
        handleButton();
        delay(10);
    }
    return WiFi.status() == WL_CONNECTED;
}

// Chạy một lần khi lần đầu có Wi-Fi: tối ưu RF, Blynk, OTA
static void startNetworkServices()
{
    if (servicesStarted) return;
    servicesStarted = true;

    WiFi.setSleepMode(WIFI_NONE_SLEEP);
    WiFi.setOutputPower(17.5);

    Blynk.config(BLYNK_AUTH_TOKEN);
    setSyncInterval(10 * 60);

    ArduinoOTA.setHostname(OTA_HOSTNAME);
    ArduinoOTA.setPassword(OTA_PASSWORD);
    ArduinoOTA.onStart([]() { esp_now_unregister_recv_cb(); }); // Ngừng nhận ESP-NOW khi đang ghi flash
    ArduinoOTA.onError([](ota_error_t) { esp_now_register_recv_cb(OnDataRecv); });
    ArduinoOTA.begin();
}
#endif

// Khởi tạo ESP-NOW (dùng chung cho cả hai chế độ)
static void initEspNow()
{
    if (esp_now_init() == 0)
    {
        esp_now_set_self_role(ESP_NOW_ROLE_COMBO);
        esp_now_register_recv_cb(OnDataRecv);

        uint8_t broadcastMac[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
        esp_now_add_peer(broadcastMac, ESP_NOW_ROLE_COMBO, WIFI_CHANNEL, nullptr, 0);
    }
}

BLYNK_WRITE(V0)
{
    const int manualState = param.asInt();
    setPump(manualState == 1);
    const String logMsg = manualState == 1 ? "👉 Bạn vừa BẬT bơm từ điện thoại" : "👉 Bạn vừa TẮT bơm từ điện thoại";
    Blynk.virtualWrite(V3, getTimeString() + logMsg + "\n");
}

BLYNK_CONNECTED()
{
    rtc.begin();
}

void setup()
{
    Serial.begin(115200);
    setPump(false);
    pinMode(RELAY_PIN, OUTPUT);
    pinMode(BUTTON_PIN, INPUT_PULLUP);

#ifdef OTA_ENABLE
    // [NẠP TẠM QUA OTA] Wi-Fi cố định, không WiFiManager
    WiFi.mode(WIFI_STA);
    WiFi.persistent(false);
    WiFi.setAutoReconnect(true);
    initEspNow();
    if (connectWiFi()) startNetworkServices();
    // Khi STA đã nối router thì kênh do router quyết định, ép kênh sẽ làm rớt Wi-Fi
    if (WiFi.status() != WL_CONNECTED) wifi_set_channel(WIFI_CHANNEL);
#else
    WiFi.mode(WIFI_AP_STA);

    const String apName = "Voc-Vach-IoT-Phao-" + String(EspClass::getChipId(), HEX);
    WiFi.softAP(apName.c_str());

    initEspNow();

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
#endif
}

void loop()
{
    // Ưu tiên xử lý các gói tin ESP-NOW trong bộ đệm
    handleEspNowData();
    handleButton();

#ifdef OTA_ENABLE
    if (WiFi.status() == WL_CONNECTED)
    {
        if (!servicesStarted) startNetworkServices();
        ArduinoOTA.handle();
        Blynk.run();
    }
    else if (millis() - lastWiFiAttempt > WIFI_RETRY_INTERVAL_MS)
    {
        lastWiFiAttempt = millis();
        WiFi.begin(WIFI_SSID, WIFI_PASS); // Không chờ, setAutoReconnect lo phần còn lại
    }
#else
    // Run Blynk
    if (WiFi.status() == WL_CONNECTED)
    {
        Blynk.run();
    }
#endif

    // GIẢM NHIỆT CPU: Cho CPU nghỉ 1ms mỗi lượt lặp (RF vẫn nhận sóng bình thường)
    delay(1);
}
