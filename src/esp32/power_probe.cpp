// ESP32 DevKit làm máy ghi áp VCC + chân ACK của ESP-01 trên bo phao (độ phân giải 1 ms).
// Trên bo phao, CH32V003 cấp nguồn cho ESP-01 qua SS8550 (PNP), chờ 100 ms rồi cắt nguồn
// ngay khi GPIO0 (ACK) của ESP-01 xuống LOW. Công cụ này ghi lại VCC và ACK quanh mỗi lần
// bật nguồn, kèm log UART của ESP-01, để xem ESP-01 sụt áp / reset sớm / bị cắt nguồn sớm.
//
// Đấu dây (chỉ là ngõ vào, ESP32 không bao giờ được kéo các chân này):
//   ESP32 GPIO34 (ADC1) <- ESP-01 VCC     analogReadMilliVolts, 11 dB (bão hòa ~3.1-3.3 V, chỉ quan tâm sụt áp)
//   ESP32 GPIO35        <- ESP-01 GPIO0   đường ACK, INPUT (GPIO35 không có pull)
//   ESP32 GPIO32 (ADC1) <- VDD của CH32 (ngõ ra HT7333)
//   ESP32 GPIO33 (ADC1) <- Pin+ qua cầu phân áp 10k/10k (áp thật = đọc x BAT_DIVIDER)
//   ESP32 GPIO16        <- ESP-01 TX      UART 74880 để đọc log ROM/app của ESP-01
//   GND                 -> GND bo phao (bắt buộc chung mass)
//
// Cách chạy:
//   Chờ VCC < 500 mV (nguồn tắt) thì "lên cò"; VCC vượt 1500 mV khi đã lên cò thì ghi 2000 ms.
//   Trong lúc ghi không in gì (giữ đúng thời gian), byte Serial1 cứ dồn vào bộ đệm RX.
//   Ghi xong mới in CSV rút gọn, dòng tóm tắt và log UART, rồi lên cò lại.
//
// Dùng Serial1 với TX trên GPIO27 (không dùng), không dùng chân mặc định của Serial2.
#include <Arduino.h>

#define USB_BAUD        115200
#define ESP_LOG_BAUD    74880    // Baud log ROM của ESP8266 và log debug của sender
#define PIN_VCC         34       // ADC1, nối VCC của ESP-01
#define PIN_ACK         35       // Nối GPIO0 của ESP-01
#define PIN_RAIL        32       // ADC1, nối VDD của CH32 (ngõ ra HT7333)
#define PIN_BAT         33       // ADC1, nối Pin+ qua cầu phân áp
#define BAT_DIVIDER     2        // Cầu phân áp 10k/10k
#define PIN_ESP_RX      16       // Nhận log từ TX của ESP-01
#define PIN_ESP_TX_DUMMY 27      // TX của Serial1, không nối đâu cả
#define UART_RX_BUF     8192

#define VCC_OFF_MV      500      // Dưới mức này = nguồn tắt, lên cò
#define VCC_ON_MV       1500     // Vượt mức này khi đã lên cò = bắt đầu ghi
#define CAPTURE_MS      2000
#define PRINT_DELTA_MV  50       // Chỉ in khi vmin/vmax lệch quá mức này so với dòng đã in trước
#define PRINT_EVERY_MS  50       // Vẫn in mỗi 50 ms dù không đổi

struct Bucket
{
    uint16_t vmin;
    uint16_t vmax;
    uint16_t railMin; // VDD CH32 thấp nhất trong ms này
    uint16_t batMin;  // Áp pin thấp nhất trong ms này (đã nhân BAT_DIVIDER)
    bool ackLow;    // Từng thấy ACK LOW trong ms này
    bool ackHigh;   // Từng thấy ACK HIGH trong ms này
};

static Bucket buckets[CAPTURE_MS];
static uint32_t captureCount = 0;
static bool armed = false;

static inline uint32_t readVcc()
{
    return analogReadMilliVolts(PIN_VCC);
}

// Ký tự ACK của một bucket: H, L hoặc X (thấy cả hai)
static char ackChar(const Bucket& b)
{
    if (b.ackLow && b.ackHigh) return 'X';
    return b.ackLow ? 'L' : 'H';
}

// Ghi CAPTURE_MS ms, mỗi ms một bucket; v0 là mẫu VCC đã kích hoạt cò (tính vào bucket 0)
static void capture(const uint32_t v0)
{
    const uint32_t t0 = micros();
    for (uint16_t i = 0; i < CAPTURE_MS; i++)
    {
        Bucket& b = buckets[i];
        b.vmin = 0xFFFF;
        b.vmax = 0;
        b.ackLow = false;
        b.ackHigh = false;
        b.railMin = 0xFFFF;
        b.batMin = 0xFFFF;
        if (i == 0)
        {
            b.vmin = b.vmax = (uint16_t)v0;
        }
        const uint32_t end = t0 + (uint32_t)(i + 1) * 1000UL;
        do
        {
            const uint16_t v = (uint16_t)readVcc();
            if (v < b.vmin) b.vmin = v;
            if (v > b.vmax) b.vmax = v;
            const uint16_t rail = (uint16_t)analogReadMilliVolts(PIN_RAIL);
            const uint16_t bat = (uint16_t)(analogReadMilliVolts(PIN_BAT) * BAT_DIVIDER);
            if (rail < b.railMin) b.railMin = rail;
            if (bat < b.batMin) b.batMin = bat;
            if (digitalRead(PIN_ACK)) b.ackHigh = true;
            else b.ackLow = true;
        } while ((int32_t)(micros() - end) < 0);
    }
}

static void printCapture()
{
    Serial.printf("### CAPTURE %u start\n", (unsigned)captureCount);
    Serial.println("t_ms,vmin,vmax,ack,rail_min,bat_min");

    int lastPrinted = -1;
    uint16_t pMin = 0, pMax = 0, pRail = 0, pBat = 0;
    uint16_t railMin = 0xFFFF, batMin = 0xFFFF;
    int railMinAt = 0, batMinAt = 0;
    char pAck = 0;
    uint16_t overallMin = 0xFFFF;
    int overallMinAt = 0;
    int firstAckLow = -1;
    int vccOffAt = -1;

    for (int i = 0; i < CAPTURE_MS; i++)
    {
        const Bucket& b = buckets[i];
        const char a = ackChar(b);

        if (b.vmin < overallMin)
        {
            overallMin = b.vmin;
            overallMinAt = i;
        }
        if (b.railMin < railMin)
        {
            railMin = b.railMin;
            railMinAt = i;
        }
        if (b.batMin < batMin)
        {
            batMin = b.batMin;
            batMinAt = i;
        }
        if (firstAckLow < 0 && b.ackLow) firstAckLow = i;
        if (vccOffAt < 0 && b.vmax < VCC_OFF_MV) vccOffAt = i;

        const bool first = (lastPrinted < 0);
        const bool last = (i == CAPTURE_MS - 1);
        const bool changed = !first &&
            (abs((int)b.vmin - (int)pMin) > PRINT_DELTA_MV ||
             abs((int)b.vmax - (int)pMax) > PRINT_DELTA_MV ||
             abs((int)b.railMin - (int)pRail) > PRINT_DELTA_MV ||
             abs((int)b.batMin - (int)pBat) > PRINT_DELTA_MV ||
             a != pAck);
        const bool periodic = !first && (i - lastPrinted >= PRINT_EVERY_MS);

        if (first || last || changed || periodic)
        {
            Serial.printf("%d,%u,%u,%c,%u,%u\n", i, b.vmin, b.vmax, a, b.railMin, b.batMin);
            lastPrinted = i;
            pMin = b.vmin;
            pMax = b.vmax;
            pAck = a;
            pRail = b.railMin;
            pBat = b.batMin;
        }
    }

    Serial.printf("### SUMMARY vmin_overall=%u@%d first_ack_low=%d vcc_off_at=%d rail_min=%u@%d bat_start=%u bat_min=%u@%d\n",
                  overallMin, overallMinAt, firstAckLow, vccOffAt, railMin, railMinAt, buckets[0].batMin, batMin, batMinAt);

    Serial.println("### UART");
    while (Serial1.available())
    {
        Serial.write((uint8_t)Serial1.read());
    }
    Serial.println();
    Serial.println("### END");
}

static void drainUart()
{
    while (Serial1.available()) Serial1.read();
}

void setup()
{
    Serial.begin(USB_BAUD);
    Serial1.setRxBufferSize(UART_RX_BUF);
    Serial1.begin(ESP_LOG_BAUD, SERIAL_8N1, PIN_ESP_RX, PIN_ESP_TX_DUMMY);

    analogSetPinAttenuation(PIN_VCC, ADC_11db);
    analogSetPinAttenuation(PIN_RAIL, ADC_11db);
    analogSetPinAttenuation(PIN_BAT, ADC_11db);
    pinMode(PIN_ACK, INPUT);

    Serial.println("### power_probe ready: cho VCC < 500 mV de len co");
}

void loop()
{
    const uint32_t v = readVcc();

    if (!armed)
    {
        drainUart(); // Bỏ rác trước lần bật nguồn để chỉ giữ log trong cửa sổ ghi
        if (v < VCC_OFF_MV) armed = true;
        return;
    }

    if (v > VCC_ON_MV)
    {
        captureCount++;
        capture(v);
        printCapture();
        armed = false; // Chỉ lên cò lại khi VCC xuống < 500 mV
    }
}
