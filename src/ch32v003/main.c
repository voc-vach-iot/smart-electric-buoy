#include "ch32fun.h"
#include "ch32v003_delay.h"
#include "ch32v003_gpio.h"
#include "ch32v003_power.h"
#include "ch32v003_debug.h"
#include "user_config.h"

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

    // 4. Cấu hình Wakeup Pin khi phao kéo xuống GND
    enableWakeupPin(PIN_PUMP_EMPTY, WAKEUP_RISING);
    enableWakeupPin(PIN_PUMP_FULL, WAKEUP_RISING);

    printf("\r\n--- CH32V003 POWER MANAGER INIT SUCCESS ---\r\n");
}

static void loop()
{
    // Đảm bảo ngắt nguồn ESP trước khi đi ngủ
    digitalWrite(PIN_ESP_POWER, ESP_POWER_OFF);

    printf("-> [LOG]: Chuan bi chim vao Sleep...\r\n");
    delayMs(5);

    sleepUltraLowPower();

    // ------------------------------------------------------------------------
    // TỈNH GIẤC: Xử lý ghim áp và gọi ESP dậy
    // ------------------------------------------------------------------------
    printf("\r\n===============================================\r\n");
    printf("-> [LOG]: Tinh giac! Dang loc nhiieu do phao 20ms...\r\n");
    delayMs(20);

    const uint8_t status_full = digitalRead(PIN_PUMP_FULL);
    const uint8_t status_empty = digitalRead(PIN_PUMP_EMPTY);

    printf("-> [LOG]: Trang thai doc duoc: FULL(Pin5)=%d | EMPTY(Pin7)=%d\r\n", status_full, status_empty);

    uint8_t isTrueInterrupt = 0;

    // [BƯỚC 1]: GHIM MỨC LOGIC SANG CHÂN LEVEL_OUTPUT (RX CỦA ESP)
    if (status_empty == FLOAT_ACTIVE)
    {
        // Phao cạn đóng -> Ghim LOW sang RX của ESP (Lệnh BẬT BƠM)
        digitalWrite(PIN_LEVEL_OUTPUT, PUMP_EMPTY_STATUS);
        printf("-> [LOG]: KET LUAN -> PHAO CAN CHẬP! Ghim LEVEL_OUTPUT (Pin1) = LOW\r\n");
        isTrueInterrupt = 1;
    }
    else if (status_full == FLOAT_ACTIVE)
    {
        // Phao đầy đóng -> Ghim HIGH sang RX của ESP (Lệnh TẮT BƠM)
        digitalWrite(PIN_LEVEL_OUTPUT, PUMP_FULL_STAUS);
        printf("-> [LOG]: KET LUAN -> PHAO ĐẦY CHẬP! Ghim LEVEL_OUTPUT (Pin1) = HIGH\r\n");
        isTrueInterrupt = 1;
    }
    else
    {
        printf("-> [LOG]: CANH BAO -> Ngat ao do nhieu! Ca 2 phao deu dang HIGH.\r\n");
    }

    if (isTrueInterrupt == 1)
    {
        // Đệm 2ms cho mức áp trên chân RX ổn định
        delayMs(2);

        // [BƯỚC 2]: ĐÁNH THỨC ESP-01S (Cấp nguồn EN = HIGH)
        pinMode(PIN_ESP_ACK_WAIT, INPUT_PULLUP); // Mở chân ACK_WAIT (Pin3) làm INPUT_PULLUP để chờ xung FINISH từ ESP
        digitalWrite(PIN_ESP_POWER, ESP_POWER_ON);
        printf("-> [LOG]: Da cap nguon ESP (EN = HIGH). ESP-01S dang BOOT...\r\n");
        delayMs(100);

        // [BƯỚC 3]: ĐỜI TÍN HIỆU ACK/FINISH TỪ ESP BÁO VỀ CHÂN PIN3 (MAX 10 GIÂY)
        printf("-> [LOG]: Cho tin hieu FINISH tu ESP dập PIN3 ve LOW (Max 10000ms)...\r\n");

        uint16_t handshake_timeout_ms = 10000;
        uint16_t time_elapsed_ms = 0;

        while (digitalRead(PIN_ESP_ACK_WAIT) == HIGH && handshake_timeout_ms > 0)
        {
            delayUs(1000);
            handshake_timeout_ms--;
            time_elapsed_ms++;
        }

        // Kiểm tra kết quả
        if (digitalRead(PIN_ESP_ACK_WAIT) == LOW)
        {
            printf("-> [SUCCESS]: !!! DA NHAN DUOC ACK/FINISH THÂT !!!\r\n");
            printf("-> [SUCCESS]: ESP-01S hoan thanh va phan hoi sau: %d ms.\r\n", time_elapsed_ms);
        }
        else
        {
            printf("-> [TIMEOUT ERROR]: DA CHO QUA 10 GIAY! ESP khong chiu ACK. Cuong che ngat nguon.\r\n");
        }

        // [BƯỚC 4]: TẮT NGUỒN ESP ĐỂ TIẾP TỤC ĐI NGỦ
        digitalWrite(PIN_ESP_POWER, ESP_POWER_OFF);
        digitalWrite(PIN_LEVEL_OUTPUT, PUMP_EMPTY_STATUS); // Reset chân LEVEL_OUTPUT về LOW
        pinMode(PIN_ESP_ACK_WAIT, OUTPUT); // Khóa chân ACK_WAIT (Pin3) ở OUTPUT để bảo vệ Bootrom
        digitalWrite(PIN_ESP_ACK_WAIT, LOW); // Khóa chân ACK_WAIT (Pin3) ở OUTPUT để bảo vệ Bootrom
        printf("-> [LOG]: Da ngat nguon ESP (EN = LOW).\r\n");
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
