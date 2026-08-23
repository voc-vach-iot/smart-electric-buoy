/**
* @file user_config.h
 * @author Vọc Vạch IoT
 * @brief Tệp cấu hình người dùng, định nghĩa các chân chức năng cụ thể cho ứng dụng.
 * @version 1.0
 * @date 2026-05-26
 */

#ifndef USER_CONFIG_H
#define USER_CONFIG_H

#include <ch32v003_gpio.h>

#define PIN_LEVEL_OUTPUT MCU_PIN1
#define PUMP_FULL_STAUS HIGH
#define PUMP_EMPTY_STATUS LOW

#define PIN_ESP_ACK_WAIT MCU_PIN3

#define PIN_ESP_POWER MCU_PIN6
// Đối với PNP Transistor hoặc P-MOSFET ngắt VCC:
#define ESP_POWER_ON   LOW   // Kéo Base/Gate xuống LOW để dẫn VCC
#define ESP_POWER_OFF  HIGH  // Kéo Base/Gate lên HIGH để ngắt VCC

#define PIN_PUMP_FULL MCU_PIN5
#define PIN_PUMP_EMPTY MCU_PIN7
#define FLOAT_ACTIVE HIGH

#endif
