/**
* @file user_config.h
 * @author Vọc Vạch IoT
 * @brief Tệp cấu hình người dùng, định nghĩa các chân chức năng cụ thể cho ứng dụng.
 * @version 1.0
 * @date 2026-05-26
 */

#ifndef USER_CONFIG_H
#define USER_CONFIG_H

#if defined(CH32V003J4M6)
#include <ch32v003_gpio.h>
#endif

#define PIN_LEVEL_OUTPUT MCU_PIN1
#define PUMP_FULL_STAUS HIGH
#define PUMP_EMPTY_STATUS LOW

#define PIN_ESP_ACK_WAIT MCU_PIN3
#define ESP_BOOT_IGNORE_MS 200 // Bỏ qua chân ACK trong lúc ESP boot (GPIO0 nhấp nháy tới ~95 ms)
#define ESP_ACK_STABLE_MS  20  // ACK hợp lệ khi LOW liên tục ít nhất chừng này ms

#define PIN_ESP_POWER MCU_PIN6
// Đối với PNP Transistor hoặc P-MOSFET ngắt VCC:
#define ESP_POWER_ON   LOW   // Kéo Base/Gate xuống LOW để dẫn VCC
#define ESP_POWER_OFF  HIGH  // Kéo Base/Gate lên HIGH để ngắt VCC

#define PIN_PUMP_FULL MCU_PIN5
#define PIN_PUMP_EMPTY MCU_PIN7
#define FLOAT_ACTIVE HIGH
#define FLOAT_STABLE_MS         50  // Trạng thái phao phải giữ nguyên liên tục chừng này ms mới chốt (lọc dội tiếp điểm)
#define FLOAT_SETTLE_TIMEOUT_MS 500 // Quá thời gian này mà phao chưa ổn định thì coi là nhiễu
#define FLOAT_MAX_SENDS         3   // Số lần gửi tối đa mỗi lần thức (phao đổi trạng thái trong lúc đang gửi)

#endif
