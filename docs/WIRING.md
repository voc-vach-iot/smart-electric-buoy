# smart-electric-buoy: sơ đồ nối dây (suy ra từ code)

Nguồn: `/mnt/data/projects/hardware/smart-electric-buoy` (src/ch32v003/main.c, include/user_config.h, include/config.h, src/esp-01/sender.cpp, receiver.cpp, docs/plan-receiver-ota-passthrough-button.md).
Proteus: `/mnt/archive/projects/hardware/eda/proteus/projects/smart-electric-bouy/smart-electric-buoy.pdsprj` (zip, ROOT.DSN). Chỉ thấy linh kiện chung chung (LED_GREEN, điện trở, tụ điện phân, nút BUTTON), KHÔNG thấy ESP-01, CH32V003, SS8550, relay trong thư viện thiết bị. Coi như schematic thực tế chưa có; phần lớn suy từ code và xác nhận của chủ mạch. Bản `/mnt/data/projects/eda/proteus` chỉ có `esp01-shield`, không liên quan.

## 1. Danh sách linh kiện

Node Sender (chạy pin):
| Linh kiện | Giá trị / ghi chú | Căn cứ |
|---|---|---|
| CH32V003J4M6 (SOP8) | quản lý nguồn, ngủ Standby | platformio.ini `-D CH32V003_J4M6` |
| ESP-01S | ESP-NOW sender, đọc VCC qua ADC_VCC | sender.cpp:7 |
| LDO HT7333 | ổn áp từ pin, ra 3.3 V | xác nhận của chủ mạch |
| Tụ 1000 µF | lọc rail 3.3 V ngay sau HT7333, giữ đỉnh dòng khi TX | đã xác nhận |
| Công tắc nguồn cho ESP: PNP SS8550 | cao áp phía VCC, base kéo LOW để dẫn; collector cấp rail ESP (VCC, EN/CH_PD, RST nối chung) | chủ mạch xác nhận; user_config.h:24-27 |
| R base | 330 Ω từ PC2 (pin6) | chủ mạch xác nhận |
| R pull-up base-emitter | 100 kΩ, giữ PNP tắt khi CH32 ngủ | chủ mạch xác nhận |
| R pull-down phao FULL, EMPTY | 100k mỗi phao, xuống GND (bắt buộc vì là chân đánh thức) | chủ mạch xác nhận |
| 2 phao (tiếp điểm) FULL, EMPTY | active HIGH khi đóng | user_config.h:29-31 |
| Pin | điện áp 2600-3300 mV (BATTERY_*) | config.h |

Node Receiver:
| Linh kiện | Ghi chú | Căn cứ |
|---|---|---|
| ESP-01 | ESP-NOW + Blynk | receiver.cpp |
| Module ESP-01S relay (ESP-01S cắm lên board relay), active LOW | relay SRD-05VDC-SL-C, tiếp điểm 10A 250VAC; GPIO0 điều khiển relay | chủ mạch xác nhận; config.h RELAY_ACTIVE_LOW |
| Nguồn AC-DC 220VAC → 5V 1A | cấp 5 V cho module ESP-01S relay | chủ mạch xác nhận |
| Contactor (khởi động từ) + snubber | snubber RC đã có sẵn song song contactor, chống hồ quang | chủ mạch xác nhận |
| Nút nhấn bơm | bật/tắt bơm, GPIO2 nối GND | config.h:77 BUTTON_PIN |
| Nút nhấn reset | kéo RST xuống GND khi nhấn | chủ mạch xác nhận |
| AMS1117 3V3 (chỉ nhắc cho lúc nạp OTA/passthrough) | docs/plan...md:325 | |

Không có giá trị R, diode nào khác trong repo.

## 2. Nguồn

- Sender: pin → LDO HT7333 → rail 3.3 V. Rail này cấp CH32V003 (VDD) và qua khóa PNP SS8550 cấp ESP-01S. Điện áp pin đọc bằng ADC_VCC của ESP-01S được bù offset hiệu chuẩn 175 mV (`VCC_SS8550_OFFSET`, config.h:46): ESP đo ngay sau khi được cấp nguồn, lúc điện áp chưa ổn định, và ADC của ESP-01 có sai số, nên giá trị này lấy từ hiệu chuẩn thực tế.
- Receiver: 220VAC → AC-DC 5V 1A → module ESP-01S relay (5V). 220VAC cũng cấp cho contactor. ESP-01 lấy 3.3 V từ IC ổn áp AMS1117 có sẵn trên module relay.

## 3. Bảng kết nối (Sender)

| Từ | Đến | Net | Ghi chú |
|---|---|---|---|
| CH32 PA1 (pin1) | ESP-01S RX (GPIO3) | LEVEL | Trực tiếp, không R nối tiếp, không R kéo. Ghim mức: HIGH = phao đầy (tắt bơm), LOW = cạn (bật bơm). Khớp `PIN_LEVEL_INPUT 3` |
| CH32 PA2 (pin3) | ESP-01S GPIO0 | ACK | Trực tiếp, không R nối tiếp, không R kéo. ESP kéo LOW báo xong; CH32 dùng INPUT_PULLUP lúc chờ, sau đó OUTPUT LOW |
| CH32 PC2 (pin6) | Base PNP SS8550 | ESP_PWR | LOW = cấp nguồn ESP, HIGH = ngắt. R base 330 Ω; R pull-up base-emitter 100 kΩ |
| PNP SS8550 collector | ESP-01S VCC + CH_PD/EN + RST (nối chung) | ESP_VCC | Cả ba cùng rail đã chuyển mạch |
| Phao FULL | CH32 PC1 (pin5) | FLOAT_FULL | Active HIGH, đánh thức rising; 100k pull-down ngoài |
| Phao EMPTY | CH32 PC4 (pin7) | FLOAT_EMPTY | Active HIGH, đánh thức rising; 100k pull-down ngoài |
| Pin + | HT7333 VIN | VBAT | Pin cấp LDO |
| HT7333 VOUT (3.3 V) | CH32 VDD (pin4), nguồn cho khóa PNP | RAIL_3V3 | Cùng một rail 3.3 V cấp CH32V003 và phía trước SS8550 (ESP-01S). Tụ 1000 µF trên rail |
| Pin - | CH32 VSS (pin2), ESP GND | GND | |
| CH32 PD1 (pin8) | cổng nạp/printf debug (SWIO) | SWIO | printf dùng debug lib |

## 4. Bảng kết nối (Receiver)

Nguồn và tải:
| Từ | Đến | Net | Ghi chú |
|---|---|---|---|
| 220VAC (lưới) | Đầu vào AC-DC 5V 1A | MAINS | |
| AC-DC 5V 1A, ra +5V | VCC của module ESP-01S relay | 5V | Cấp cuộn relay SRD-05VDC |
| 220VAC, dây lửa | COM của tiếp điểm relay (module ESP-01S relay) | LIVE | Dây lửa đi qua tiếp điểm relay |
| Tiếp điểm NO của module relay | Cuộn contactor | PUMP | Relay đóng/ngắt cuộn contactor, contactor đóng bơm |
| Hai đầu contactor | Snubber RC (đã có sẵn) | | Chống hồ quang tại tiếp điểm |

Điều khiển và nút nhấn:
| Từ | Đến | Net | Ghi chú |
|---|---|---|---|
| ESP-01 GPIO0 | IN của module ESP-01S relay (cắm trực tiếp) | RELAY | Theo mạch có sẵn của module (transistor + điện trở trên board). Active LOW (RELAY_ON=LOW). GPIO0 là strapping: relay đóng khi nạp (docs:329) |
| ESP-01 GPIO2 | Nút nhấn bơm, nút còn lại nối GND | BTN | INPUT_PULLUP nội; config.h:77 BUTTON_PIN; không giữ nút khi cấp nguồn |
| ESP-01 RST | Nút nhấn reset, nút còn lại nối GND | RST | Nhấn kéo RST xuống GND để reset MCU. Không dùng chân GPIO |
| AMS1117 trên module relay (3V3) | ESP-01 VCC, CH_PD | 3V3 | Ổn áp có sẵn trên module |

## 5. Bảng chân MCU

CH32V003J4M6 (pin ↔ cổng theo ch32v003_gpio_pins.h, J4M6: pin1=PA1, pin3=PA2, pin5=PC1, pin6=PC2, pin7=PC4, pin8=PD1):
| Chân | Chức năng | Định nghĩa |
|---|---|---|
| 1 (PA1) | LEVEL_OUTPUT sang RX ESP | include/user_config.h:16; dùng main.c:29,88,93,134 |
| 3 (PA2) | ESP_ACK_WAIT | user_config.h:20; main.c:101,117,135 |
| 5 (PC1) | PUMP_FULL (phao đầy) | user_config.h:29; main.c:25,55 |
| 6 (PC2) | ESP_POWER | user_config.h:24; main.c:33,102,133 |
| 7 (PC4) | PUMP_EMPTY (phao cạn) | user_config.h:30; main.c:26,54 |
| 8 (PD1) | SWIO / printf | không khai báo trong repo (suy từ lib) |
| 2, 4 | VSS, VDD | VDD nối rail 3.3 V (xem mục 3) |

ESP-01S sender: GPIO0 = PIN_MCU_ACK (config.h:45), GPIO3 = PIN_LEVEL_INPUT (config.h:46); ADC_VCC (sender.cpp:7).
ESP-01 receiver: GPIO0 = RELAY_PIN (config.h:66), GPIO2 = BUTTON_PIN (config.h:77).

## 6. Lưu ý thiết kế

HT7333 ra tối đa khoảng 250 mA, nhưng đỉnh phát Wi-Fi của ESP-01S có thể vượt 300 mA. Tụ 1000 µF giữ rail khi có đỉnh ngắn. Cần đo độ sụt rail 3.3 V lúc phát (TX).

## 7. Chưa rõ / mâu thuẫn

1. Schematic Proteus không chứa ESP/CH32/transistor/relay: không có nguồn đối chiếu.
2. Đường LEVEL và ACK: không có R. Thứ tự GPIO0 (đã kiểm trong src/ch32v003/main.c): CH32 chuyển chân ACK sang INPUT_PULLUP **trước** khi cấp nguồn ESP (dòng 101–102), nên GPIO0 ở mức HIGH lúc ESP khởi động và ESP chạy bình thường. CH32 bỏ qua ESP_BOOT_IGNORE_MS đầu (GPIO0 tự nháy LOW lúc boot), rồi nhận ACK khi chân LOW liên tục đủ ESP_ACK_STABLE_MS (tối đa 10 s). Chỉ **sau khi đã ngắt nguồn ESP** (dòng 133) chân mới chuyển OUTPUT LOW (dòng 135–136) để không thả nổi, tiết kiệm điện. Không có lúc nào GPIO0 bị kéo LOW khi ESP đang khởi động.
