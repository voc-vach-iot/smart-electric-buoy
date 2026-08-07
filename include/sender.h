#ifndef SENDER_H
#define SENDER_H

#include <Arduino.h>
#include "config.h"

/**
 * @brief Đưa thiết bị ESP-01S vào chế độ Deep Sleep để tối ưu hóa điện năng tiêu thụ.
 * 
 * Hàm này tiến hành ngắt kết nối WiFi, xả bộ đệm Serial và gọi lệnh deepSleep 
 * của chip ESP8266. Hàm sẽ chặn luồng thực thi vĩnh viễn cho tới khi chip được reset.
 */
[[noreturn]] void enterDeepSleep();

/**
 * @brief Đọc và đánh giá trạng thái hiện tại của phao cảm biến.
 * 
 * @return BuoyStatus Giá trị trạng thái bơm tương ứng (PUMP_ON hoặc PUMP_OFF).
 */
BuoyStatus getStatus();

/**
 * @brief Thực hiện quét tất cả các kênh WiFi để tìm kiếm thiết bị Receiver.
 * 
 * @return true Quét thành công và tìm thấy Receiver.
 * @return false Không tìm thấy Receiver trên toàn bộ các kênh (1-13).
 */
bool scanAndDiscover();

/**
 * @brief Lưu cấu hình gồm địa chỉ MAC và channel WiFi của Receiver vào bộ nhớ RTC.
 * 
 * @param mac Mảng chứa 6 byte địa chỉ MAC của thiết bị Receiver.
 * @param channel Kênh WiFi hiện tại thu thuộc thiết bị Receiver.
 */
void saveConfigToRTC(const uint8_t* mac, uint8_t channel);

/**
 * @brief Tải thông tin địa chỉ MAC và channel WiFi của Receiver từ bộ nhớ RTC.
 */
void loadConfigFromRTC();

/**
 * @brief Callback xử lý sự kiện nhận dữ liệu phản hồi từ ESP-NOW.
 * 
 * @param mac Địa chỉ MAC của thiết bị vừa gửi dữ liệu tới.
 * @param incomingData Con trỏ chứa mảng dữ liệu nhận được.
 * @param len Độ dài của gói dữ liệu nhận được.
 */
void OnDataRecv(uint8_t* mac, uint8_t* incomingData, uint8_t len);

#endif // SENDER_H
