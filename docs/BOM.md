# Danh sách linh kiện (BOM) — Smart Electric Buoy

Hệ thống báo mực nước: mạch gửi (phao) chạy pin gửi trạng thái qua ESP-NOW, mạch nhận (tủ điện) đóng cắt contactor của bơm.

Ghi chú: Link mua là link affiliate; mua qua link giúp ủng hộ Vọc Vạch IoT.

## Mạch gửi (phao)

| # | Linh kiện | Thông số / mã | SL | Ghi chú | Link mua |
|---|---|---|---|---|---|
| 1 | Vi điều khiển CH32V003J4M6 | SOP8 | 1 | Quản lý nguồn, ngủ Standby, đánh thức bằng phao | [Shopee](https://s.shopee.vn/9074q5D68Z) |
| 2 | Đế chuyển SOP8 sang DIP8 | SOP8 → DIP8 | 1 | Gắn CH32V003J4M6 lên breadboard | [Shopee](https://s.shopee.vn/6fjA436mqt) |
| 3 | Module ESP-01S | ESP8266, ESP-NOW | 1 | Gửi trạng thái phao, đọc điện áp pin bằng ADC_VCC | [Shopee](https://s.shopee.vn/3qOyhMqDd2) |
| 4 | Đế ESP-01 Breakout | Đổi 2x4 chân thành 1 hàng chân cắm breadboard | 1 | Có sẵn 1 tụ lọc nguồn, không có ổn áp | [Shopee](https://s.shopee.vn/BVgKRwWRu) |
| 5 | LDO HT7333-A | TO-92, ra 3.3 V | 1 | Ổn áp từ pin xuống rail 3.3 V | [Shopee](https://s.shopee.vn/40iOthtvsf) |
| 6 | Transistor PNP SS8550 | TO-92 | 1 | Khóa nguồn phía cao cấp cho ESP-01S | [Shopee](https://s.shopee.vn/AAJ2F5mqMo) |
| 7 | Điện trở 330 Ω | 1/4 W, ±5% | 1 | R1: base SS8550 từ PC2 | [Shopee](https://s.shopee.vn/9zzc2q95c6) |
| 8 | Điện trở 100 kΩ | 1/4 W, ±5% | 3 | R2: kéo base-emitter SS8550 (1 cái); kéo chân phao FULL và EMPTY (2 cái) | [Shopee](https://s.shopee.vn/9V3LS0hU2w) |
| 9 | Tụ hóa 1000 µF | 16 V | 1 | C1: lọc rail 3.3 V sau HT7333, giữ đỉnh dòng khi ESP phát | [Shopee](https://s.shopee.vn/4B1p6JuhJJ) |
| 10 | Pin LiPo 3.7 V | 1000 mAh | 1 | Nguồn cho mạch phao | [Shopee](https://s.shopee.vn/70M0TYQgC9) |
| 11 | Bộ phao điện (gồm 2 quả phao) | Phao mực nước 2 cặp tiếp điểm | 1 | Cả 2 phao chìm (cạn) thì cặp EMPTY thông, cả 2 phao nổi (đầy) thì cặp FULL thông; CH32 nối 2 cặp này để thức dậy và đọc trạng thái. Hãng khác nhau nối khác nhau, đo thông mạch trước khi nối | [Shopee](https://s.shopee.vn/9V3LSGbhwx) |
| 12 | Breadboard mini | | 1 | Lắp mạch gửi | [Shopee](https://s.shopee.vn/70M0TdGPUB) |

## Mạch nhận (tủ điện)

| # | Linh kiện | Thông số / mã | SL | Ghi chú | Link mua |
|---|---|---|---|---|---|
| 13 | Module relay ESP-01S | ESP-01S cắm lên board relay, relay SRD-05VDC-SL-C 10 A 250 VAC, kích mức thấp | 1 | Relay đóng cắt cuộn contactor; board có sẵn AMS1117 3V3 | [Shopee](https://s.shopee.vn/Lp6WXafh5) |
| 14 | Module ESP-01 | ESP8266, ESP-NOW + Blynk | 1 | Cắm lên module relay, GPIO0 điều khiển relay | [Shopee](https://s.shopee.vn/8AXxrHoxSi) |
| 15 | Nguồn AC-DC 220 VAC sang 5 V | 5 V / 1 A | 1 | Cấp 5 V cho module relay | [Shopee](https://s.shopee.vn/gRwwCmoRU) |
| 16 | Contactor (khởi động từ) | GLEX LNC1-25, cuộn 220/230 V~, 25 A (AC-7a) | 1 | Đóng cắt bơm; có 2 tiếp điểm động lực (1-2, 3-4) | [Shopee](https://s.shopee.vn/6fjA5TtafD) |
| 17 | Module RC snubber | Mắc song song contactor | 1 | Chống hồ quang | [Shopee](https://s.shopee.vn/1gKU8KIxze) |
| 18 | Nút nhấn | 2 chân | 2 | BOM: bật/tắt bơm (GPIO2 xuống GND); RESET: kéo RST xuống GND | [Shopee](https://s.shopee.vn/1W13wPoXET) |

## Vật tư phụ

| # | Vật tư | Ghi chú |
|---|---|---|
| A | Mạch nạp WCH-LinkE | Nạp CH32V003 qua chân SWIO (PD1) — [Shopee](https://s.shopee.vn/6fjA615u0w) |
| B | Dây cắm breadboard | Nối mạch gửi — [Shopee](https://s.shopee.vn/4fy5iQsfWj) |
| C | Dây điện, đầu cos / cầu đấu | Nối 220 VAC, contactor và bơm dây 1 lõi 2.5 mm² (contactor 25 A, dùng ở mức khoảng 16 A) — [Shopee](https://s.shopee.vn/7pv7UKtNFG) |
| D | Mạch nạp USB cho ESP-01 / ESP-01S (loại 1 đầu USB, 1 header 2x4) | Nạp firmware ESP — [Shopee](https://s.shopee.vn/40iOtVYRz6) |
