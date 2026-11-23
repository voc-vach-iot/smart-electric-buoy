#include "ch32fun.h"
#include "ch32v003_delay.h"
#include "ch32v003_gpio.h"
#include "ch32v003_power.h"
#include "ch32v003_debug.h"
#include "user_config.h"

// ------------------------------------------------------------------------
// [SỬA ĐỔI]: Tạo cấu hình đánh thức (Wakeup) theo chuẩn thư viện ch32v003_power
// ------------------------------------------------------------------------
static const POWER_WakeupPin_t wakeupPins[] = {
    {PIN_PUMP_EMPTY, POWER_WAKEUP_RISING}, // Thay thế WAKEUP_RISING bằng hằng số POWER_WAKEUP_RISING
    {PIN_PUMP_FULL, POWER_WAKEUP_RISING}
};

static const POWER_WakeupConfiguration_t wakeupConfig = {
    .pins = wakeupPins,
    .pinCount = 2,
    .timeoutMs = 0 // Bằng 0: Chỉ thức khi phao kích hoạt, không dùng hẹn giờ
};

static void setup()
{
    // 1. Chân Phao FULL và EMPTY cấu hình INPUT
    pinMode(PIN_PUMP_FULL, INPUT);
    pinMode(PIN_PUMP_EMPTY, INPUT);

    // 2. Chân LEVEL_OUTPUT (Pin 1) làm Output ghim áp sang RX của ESP
    pinMode(PIN_LEVEL_OUTPUT, OUTPUT);
    digitalWrite(PIN_LEVEL_OUTPUT, PUMP_EMPTY_STATUS); // Mặc định ghim LOW

    // 3. Chân Điều khiển nguồn cho ESP (Pin 6) làm Output
    pinMode(PIN_ESP_POWER, OUTPUT);
    digitalWrite(PIN_ESP_POWER, ESP_POWER_OFF); // Mặc định ngắt nguồn ESP

    // 4. [SỬA ĐỔI]: Gọi hàm đăng ký Wakeup Pin của thư viện
    powerConfigureWakeup(&wakeupConfig);

    printf("\r\n--- CH32V003 POWER MANAGER INIT SUCCESS ---\r\n");
}

typedef enum
{
    FLOAT_STATE_NONE = 0, // Chưa ổn định hoặc cả 2 phao đều không đóng
    FLOAT_STATE_EMPTY,    // Phao cạn đóng -> BẬT BƠM
    FLOAT_STATE_FULL      // Phao đầy đóng -> TẮT BƠM
} FloatState_t;

// Trạng thái đã gửi gần nhất (RAM được giữ trong Standby), NONE = chưa gửi lần nào
static FloatState_t lastSentState = FLOAT_STATE_NONE;

static FloatState_t readFloatRaw()
{
    if (digitalRead(PIN_PUMP_EMPTY) == FLOAT_ACTIVE) return FLOAT_STATE_EMPTY;
    if (digitalRead(PIN_PUMP_FULL) == FLOAT_ACTIVE) return FLOAT_STATE_FULL;
    return FLOAT_STATE_NONE;
}

// Chỉ chốt khi trạng thái giữ nguyên FLOAT_STABLE_MS liên tục, tránh đọc lúc phao còn đang di chuyển/dội
static FloatState_t readFloatStable()
{
    FloatState_t state = readFloatRaw();
    uint16_t stable_ms = 0;

    for (uint16_t elapsed_ms = 0; elapsed_ms < FLOAT_SETTLE_TIMEOUT_MS; elapsed_ms++)
    {
        delayMs(1);
        const FloatState_t now = readFloatRaw();
        if (now != state)
        {
            state = now;
            stable_ms = 0;
        }
        else if (++stable_ms >= FLOAT_STABLE_MS)
        {
            return state;
        }
    }
    return FLOAT_STATE_NONE;
}

// Ghim mức sang RX của ESP, cấp nguồn, chờ ACK rồi ngắt nguồn ESP
static void sendStateToEsp(const FloatState_t state)
{
    // [BƯỚC 1]: GHIM MỨC LOGIC SANG CHÂN LEVEL_OUTPUT (RX CỦA ESP)
    if (state == FLOAT_STATE_EMPTY)
    {
        digitalWrite(PIN_LEVEL_OUTPUT, PUMP_EMPTY_STATUS);
        printf("-> [LOG]: KET LUAN -> PHAO CAN! Ghim LEVEL_OUTPUT = LOW\r\n");
    }
    else
    {
        digitalWrite(PIN_LEVEL_OUTPUT, PUMP_FULL_STAUS);
        printf("-> [LOG]: KET LUAN -> PHAO DAY! Ghim LEVEL_OUTPUT = HIGH\r\n");
    }

    // Đệm 2ms cho mức áp trên chân RX ổn định
    delayMs(2);

    // [BƯỚC 2]: ĐÁNH THỨC ESP-01S (Cấp nguồn EN = HIGH)
    pinMode(PIN_ESP_ACK_WAIT, INPUT_PULLUP); // Mở chân ACK_WAIT làm INPUT_PULLUP để chờ xung FINISH từ ESP
    digitalWrite(PIN_ESP_POWER, ESP_POWER_ON);
    printf("-> [LOG]: Da cap nguon ESP (EN = HIGH). ESP-01S dang BOOT...\r\n");
    // GPIO0 của ESP8266 tự nhấp nháy LOW trong lúc boot (~36-95 ms), phải bỏ qua kẻo tưởng nhầm là ACK
    delayMs(ESP_BOOT_IGNORE_MS);

    // [BƯỚC 3]: ĐỢI TÍN HIỆU ACK/FINISH TỪ ESP BÁO VỀ CHÂN (MAX 10 GIÂY)
    printf("-> [LOG]: Cho tin hieu FINISH tu ESP dap PIN3 ve LOW (Max 10000ms)...\r\n");

    uint16_t handshake_timeout_ms = 10000;
    uint16_t time_elapsed_ms = ESP_BOOT_IGNORE_MS;
    uint8_t ack_low_ms = 0;

    // Chỉ nhận ACK khi chân LOW liên tục đủ ESP_ACK_STABLE_MS (lọc xung nhiễu ngắn)
    while (ack_low_ms < ESP_ACK_STABLE_MS && handshake_timeout_ms > 0)
    {
        ack_low_ms = digitalRead(PIN_ESP_ACK_WAIT) == LOW ? ack_low_ms + 1 : 0;
        delayUs(1000);
        handshake_timeout_ms--;
        time_elapsed_ms++;
    }

    if (ack_low_ms >= ESP_ACK_STABLE_MS)
    {
        printf("-> [SUCCESS]: ESP-01S hoan thanh va phan hoi sau: %d ms.\r\n", time_elapsed_ms);
    }
    else
    {
        printf("-> [TIMEOUT ERROR]: DA CHO QUA 10 GIAY! ESP khong chiu ACK. Cuong che ngat nguon.\r\n");
    }

    // [BƯỚC 4]: TẮT NGUỒN ESP
    digitalWrite(PIN_ESP_POWER, ESP_POWER_OFF);
    digitalWrite(PIN_LEVEL_OUTPUT, PUMP_EMPTY_STATUS); // Reset chân LEVEL_OUTPUT về LOW
    pinMode(PIN_ESP_ACK_WAIT, OUTPUT); // Khóa chân ACK_WAIT ở OUTPUT để bảo vệ Bootrom
    digitalWrite(PIN_ESP_ACK_WAIT, LOW);
    printf("-> [LOG]: Da ngat nguon ESP (EN = LOW).\r\n");
}

static void loop()
{
    // Đảm bảo ngắt nguồn ESP trước khi đi ngủ
    digitalWrite(PIN_ESP_POWER, ESP_POWER_OFF);

    printf("-> [LOG]: Chuan bi chim vao Sleep...\r\n");
    delayMs(5);

    // Standby (~8-10uA), thức dậy chạy tiếp dòng dưới (RAM được giữ nguyên)
    powerStandby();

    printf("\r\n===============================================\r\n");

    // Gửi tới khi trạng thái phao thật khớp với lần gửi gần nhất: phao đổi trạng thái trong lúc đang gửi
    // sẽ không còn cạnh lên nào đánh thức nữa (powerStandby xóa cờ EXTI), nên phải đọc lại trước khi ngủ
    for (uint8_t sends = 0; sends < FLOAT_MAX_SENDS; sends++)
    {
        const FloatState_t state = readFloatStable();
        printf("-> [LOG]: Trang thai phao on dinh: %d (da gui: %d)\r\n", state, lastSentState);

        if (state == FLOAT_STATE_NONE)
        {
            printf("-> [LOG]: CANH BAO -> Nhieu hoac phao chua on dinh, bo qua.\r\n");
            break;
        }
        if (state == lastSentState) break;

        sendStateToEsp(state);
        lastSentState = state;
    }

    printf("===============================================\r\n\r\n");
    delayMs(5);
}

int main()
{
    SystemInit();
    delayMs(1000); // Chờ nạp code & ổn định
    setup();
    while (1)
    {
        loop();
    }
}
