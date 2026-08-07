#ifndef RECEIVER_H
#define RECEIVER_H

#include <Arduino.h>
#include "config.h"

/**
 * @brief Lấy chuỗi ký tự thể hiện thời gian hiện tại chuẩn định dạng [HH:MM:SS].
 * 
 * @return String Chuỗi thời gian đã định dạng dùng để ghi Log.
 */
String getTimeString();

/**
 * @brief Callback xử lý dữ liệu nhận từ ESP-NOW (Xử lý Discovery Request & Data Status).
 * 
 * @param mac Địa chỉ MAC của thiết bị gửi đến.
 * @param incomingData Con trỏ đến dữ liệu nhận được.
 * @param len Độ dài dữ liệu.
 */
void OnDataRecv(uint8_t* mac, uint8_t* incomingData, uint8_t len);

#endif // RECEIVER_H
