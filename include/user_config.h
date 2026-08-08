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
#define PIN_ESP_ACK_WAIT MCU_PIN3
#define PIN_ESP_EN MCU_PIN6
#define PIN_PUMP_FULL MCU_PIN5
#define PIN_PUMP_EMPTY MCU_PIN7

#endif
