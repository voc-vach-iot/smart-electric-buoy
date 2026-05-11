#include <Arduino.h>
#include <config.h>
#include <ESP8266WiFi.h>
#include <espnow.h>
#include <user_interface.h>

ADC_MODE(ADC_VCC);

uint8_t targetMac[6];
uint8_t currentChannel = WIFI_CHANNEL;
struct_message myData;
volatile bool deliverySuccess = false;

// --- QUẢN LÝ BỘ NHỚ RTC ---
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
    // Debug giá trị thực tế chân RX đang đọc được
    const bool rawVal = digitalRead(PIN_SENSOR);
    DBG_PRINTF("Dbg: PIN_SENSOR Raw = %d (Expect EMPTY=%d)\n", rawVal, STATUS_EMPTY);

    if (rawVal == STATUS_EMPTY)
    {
        return PUMP_ON;
    }
    return PUMP_OFF;
}

/**
 * Hàm ép chip vào chế độ Light Sleep cực hạn.
 * Tiêu thụ ~1mA, giúp chip không bị "treo thây ma" (Zombie) 
 * và sẵn sàng Reset sạch sẽ khi phao kích chân RST.
 */
[[noreturn]] void enterForcedLightSleep()
{
    DBG_PRINTLN("\nDbg: Preparation for Light Sleep...");

    // 1. Dọn dẹp Wifi: Tắt hoàn toàn modem để giảm dòng sụt áp
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    WiFi.forceSleepBegin(); // Ép modem vào giấc ngủ sâu nhất
    delay(10);

    // 2. Cấu hình chế độ Light Sleep cho hệ thống
    // FPM: Force Power Management
    wifi_fpm_open();
    wifi_fpm_set_sleep_type(LIGHT_SLEEP_T);

    DBG_PRINTLN("Dbg: Entering Forced Light Sleep. See you at Reset!");
    delay(100); // Cho Serial kịp in hết

    // 3. Thực hiện ngủ vô thời hạn (0xFFFFFFF)
    // Chip sẽ chỉ thức dậy khi có tác động vào chân RST (Reset vật lý)
    wifi_fpm_do_sleep(0xFFFFFFF);

    // 4. Vòng lặp khóa (Safety Loop)
    // Khi chip đang trong quá trình ngủ, nó không được chạy tiếp bất cứ dòng code nào bên dưới
    while (true)
    {
        // Delay này rất quan trọng để CPU thực sự chuyển sang trạng thái Idle
        delay(1000);
    }
}

void setup()
{
    // Khởi tạo Serial1 trên chân GPIO2 (TXD1)
    DBG_BEGIN(74880);
    DBG_PRINTLN("\n\n--- SENDER DEBUG (Serial1/GPIO2) ---");

    // 2. Đợi tụ nạp xong (Khoảng 500ms - 1s)
    // Lúc này chip tiêu thụ rất ít điện vì chưa bật Wifi
    pinMode(PIN_SENSOR, INPUT_PULLUP);
    while (digitalRead(PIN_SENSOR) == LOW)
    {
        static uint8_t count = 0;
        if (count++ > 20) break; // Đợi tối đa 2s
        delay(100);
    }

    pinMode(PIN_SENSOR, PIN_MODE);
    DBG_PRINTF("Dbg: PIN_SENSOR configured on GPIO %d\n", PIN_SENSOR);

    WiFi.mode(WIFI_STA);
    WiFi.disconnect();

    if (esp_now_init() != 0)
    {
        DBG_PRINTLN("Dbg: ESP-NOW Init Fail");
        enterForcedLightSleep();
    }

    esp_now_set_self_role(ESP_NOW_ROLE_COMBO);
    esp_now_register_recv_cb(OnDataRecv);

    loadConfigFromRTC();

    myData.key = SECRET_KEY;
    myData.msg_type = DATA_SEND;
    myData.battery_voltage = ESP.getVcc();

    // Theo dõi quá trình áp sạc lại qua Serial1
    DBG_PRINTLN("Dbg: Waiting for voltage stability...");
    for (int i = 0; i < 5; i++)
    {
        DBG_PRINTF("Dbg: Current RX: %d\n", digitalRead(PIN_SENSOR));
        delay(100);
    }

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
                DBG_PRINTLN("Dbg: Quick Success. Sleep.");
                delay(50);
                enterForcedLightSleep();
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

    DBG_PRINTLN("Dbg: Final Sleep.");
    delay(50);
    enterForcedLightSleep();
}

void loop()
{
}
