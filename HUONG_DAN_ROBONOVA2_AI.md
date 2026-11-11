# Hướng Dẫn Đấu Nối, Biên Dịch & Nạp Firmware DB-Robonova2 ESP32-S3 AI

Bộ mã nguồn này được thiết kế chuyên biệt cho **MINIROBOT Robonova2 / Metal Fighter MF-17ST** tích hợp **ESP32-S3 N16R8** theo đúng **Sơ đồ DB-ROBONOVA2 ESP32-S3 XIAOZHI AI**, đồng thời mở rộng hỗ trợ **Cảm biến siêu âm trên mắt** và **Cảm biến khoảng cách hồng ngoại `10-NK` dưới bàn chân trái** trên bản độ của anh.

---

## 1. Bảng Đấu Nối Phần Cứng Chuẩn Theo Sơ Đồ

### 1.1. Mạch chính ESP32-S3 DevKitC-1 (`N16R8` - 16MB Flash, 8MB Octal PSRAM) ↔ Mạch gốc `MR-C3024FX`
| Chân trên ESP32-S3 | Nối tới chân trên `MR-C3024FX` | Chức năng |
| :--- | :--- | :--- |
| **`GPIO 42`** (`TX`) | Chân **`a`** (`ERX` - cụm cổng Serial/Zigbee góc trên trái) | Gửi mã lệnh ASCII (`9600 bps`) gọi 63 động tác gốc (`K1`–`K68`) |
| **`GPIO 41`** (`RX`) | Chân **`A`** (`ETX` - cạnh chân `a`) | Nhận phản hồi Pin + Mắt Siêu Âm + Cảm biến chân `10-NK` + ACK hoàn thành động tác |
| **`5V`** (`5Vin`) | Chân **`V`** (`+5.0V` ổn áp trên dãy nguồn cạnh chân `A`) | Cấp nguồn 5V từ robot sang ESP32-S3 & mạch loa |
| **`GND`** | Chân **`G`** / **`c`** (`GND` chung) | Nối mass chung bắt buộc giữa 2 mạch |

> **Lưu ý an toàn khi nạp code:** Khi cắm cáp USB từ máy tính vào ESP32-S3 để nạp lần đầu, nên **tắt công tắc nguồn của Robot** (hoặc rút dây `5V` nối từ `MR-C3024FX`) để tránh xung nguồn 5V USB và 5V BEC của robot. Sau khi nạp xong có thể cập nhật không dây qua OTA (`http://<IP>/ota`).

### 1.2. Micro I2S `INMP441`
- `VDD` → `3.3V` | `GND` → `GND` | `L/R` → `GND`
- `WS` → **`GPIO 4`**
- `SCK` → **`GPIO 5`**
- `SD` → **`GPIO 6`**

### 1.3. Mạch Khuếch Đại Loa I2S `MAX98357A`
- `VIN` → `5V` | `GND` → `GND`
- `DIN` → **`GPIO 7`**
- `BCLK` → **`GPIO 15`**
- `LRC` → **`GPIO 16`**

### 1.4. Cảm Biến Chạm Ngực `TTP223` (Màu đỏ)
- **Hàn nối điểm `A`**, để hở điểm `B` trên module `TTP223` (xuất mức `LOW` khi chạm, trùng logic nút `BOOT`).
- `VCC` → `3.3V` | `GND` → `GND` | `I/O` → **`GPIO 0`**
- **Thao tác chạm:**
  - **Chạm 1 lần:** Đánh thức / Bắt đầu hoặc dừng trò chuyện giọng nói AI.
  - **Chạm đúp 2 lần:** Dừng khẩn cấp chuỗi động tác Robot & hiện địa chỉ `http://<IP>:8080` lên màn hình.
  - **Chạm giữ > 1.5 giây:** Mở lại điểm phát Wi-Fi để đổi mạng Wi-Fi mới.

### 1.5. Màn Hình `1.54" IPS TFT ST7789 240x240px` (Tùy chọn — không gắn màn vẫn chạy bình thường)
- `1. GND` → `GND` | `2. VCC` → `3.3V`
- `3. SCL (SPI CLOCK)` → **`GPIO 9`**
- `4. SDA (SPI DATA)` → **`GPIO 10`**
- `5. RES (RESET)` → **`GPIO 18`**
- `6. DC (DATA/CMD)` → **`GPIO 8`**
- `7. CS (CHIP SELECT)` → **`GPIO 14`**
- `8. BLK (BACKLIGHT)` → **`GPIO 13`**
- *Mẹo:* Màn 8 chân có `CS` mặc định chạy `SPI Mode 0`. Nếu anh dùng loại màn ST7789 7 chân (không có `CS`), chỉ cần bấm nút **"Đổi SPI Mode ST7789 (0/3)"** ở cuối trang Web `:8080` là mạch tự chuyển sang `SPI Mode 3`.

---

## 2. Hai Cách Biên Dịch & Nạp Firmware Ra File `.bin`

### Cách 1: Build Tự Động Trên GitHub Actions (Nhanh nhất — Không cần cài 10GB ESP-IDF vào máy)
Đã tích hợp sẵn workflow `.github/workflows/build_db_robonova2.yml`:
1. Tạo 1 repository mới (Public hoặc Private) trên tài khoản GitHub của anh.
2. Mở PowerShell tại thư mục `E:\MTC\APK\KoreaRobot\robonova2_esp32s3_ai` và đẩy code lên:
   ```powershell
   git add .
   git commit -m "Add DB-Robonova2 ESP32-S3 AI custom firmware"
   git remote set-url origin https://github.com/<TEN_GITHUB_CUA_ANH>/robonova2-esp32s3-ai.git
   git push -u origin robonova2-vn
   ```
3. Vào tab **Actions** trên GitHub → chọn **Build DB-Robonova2 ESP32-S3 AI Firmware** → đợi khoảng 4–5 phút → tải gói **`firmware_db_robonova2_s3_0x0`** về giải nén lấy file **`firmware_db_robonova2_s3_0x0.bin`**.
4. Mở trình duyệt Chrome/Edge truy cập: **`https://tienhuyiot.github.io/esp_web_flasher/`**
   - Cắm cáp USB-C vào cổng **USB** (hoặc **UART/COM**) của ESP32-S3 N16R8 → bấm **Connect**.
   - Chọn địa chỉ nạp **`0x0`** và chọn file **`firmware_db_robonova2_s3_0x0.bin`** → bấm **Program**!

### Cách 2: Build Trực Tiếp Trên Máy Tính (VS Code + ESP-IDF v5.4 / v5.5)
1. Cài **ESP-IDF v5.5** (hoặc ESP-IDF Extension trong VS Code).
2. Mở **ESP-IDF 5.5 PowerShell** tại `E:\MTC\APK\KoreaRobot\robonova2_esp32s3_ai` và chạy 1 lệnh duy nhất:
   ```powershell
   python scripts/release.py db-robonova2-s3 --name db-robonova2-s3
   ```
3. File gộp nạp thẳng ở địa chỉ `0x0` sẽ nằm tại: `build\merged-binary.bin` (và bản nén trong `releases\`).
   Hoặc nạp trực tiếp qua cổng COM:
   ```powershell
   idf.py -p COMx flash monitor
   ```

---

## 3. Kết Nối Wi-Fi, Giọng Nói AI & Web Control Cổng `8080`

1. **Cấu hình Wi-Fi lần đầu:**
   - Khi mới nạp xong, ESP32-S3 phát Wi-Fi tên **`TienHuyIoT-xxxx`**.
   - Dùng điện thoại kết nối vào Wi-Fi đó, trang chọn mạng Wi-Fi nhà sẽ tự bật lên (`http://192.168.4.1`). Chọn Wi-Fi 2.4GHz và nhập mật khẩu.
2. **Kích hoạt Trợ lý Giọng nói AI XiaoZhi:**
   - Lần đầu kết nối mạng, robot đọc và hiện **mã 6 chữ số** lên màn hình.
   - Đăng nhập `https://xiaozhi.me` → thêm thiết bị bằng mã 6 số → chọn ngôn ngữ **Tiếng Việt** và thiết lập nhân vật trợ lý cho Robot.
   - Sau khi kích hoạt, anh có thể ra lệnh tự nhiên bằng tiếng Việt:
     - *"Chào mọi người đi"*, *"Bắt tay nào"*, *"Đi tiến lên 3 bước"*, *"Múa bài võ Kungfu số 1"*, *"Chống đẩy 10 cái"*, *"Né đòn Matrix"*, *"Leo bậc cầu thang"*, *"Mở đài VOV1"*, *"Bật bài Bắc Bling rồi nhảy theo nhạc đi"*, *"IP điều khiển web của em là gì?"*...
3. **Điều khiển qua Web Control ở cổng `8080`:**
   - Mở trình duyệt trên điện thoại hoặc máy tính cùng mạng Wi-Fi, vào **`http://<IP_CUA_ROBOT>:8080`** (ví dụ `http://192.168.1.68:8080` — địa chỉ này cũng hiện ngay trên màn hình 1.54" lúc khởi động hoặc khi chạm đúp vào ngực robot).
   - Giao diện Web có sẵn: D-Pad điều hướng nhanh, bảng đầy đủ **63 động tác gốc (`K1`–`K68`)** chia theo nhóm, chạy chuỗi Combo liên hoàn, mở Nhạc trực tuyến / Đài VOV, chỉnh Âm lượng & Độ sáng màn hình, và theo dõi trực tiếp thông số cảm biến thời gian thực qua WebSocket!

---

## 4. Tích Hợp 2 Cảm Biến Độ Thêm Trên Mạch `MR-C3024FX` (Tùy chọn)

- **Nếu giữ nguyên code hãng đang có sẵn trong `MR-C3024FX`:** ESP32-S3 vẫn điều khiển trơn tru **100% toàn bộ 63 động tác (`K1`–`K68`)** ngay lập tức qua dây `GPIO 42 -> ERX` mà không cần nạp lại mạch lưng!
- **Nếu muốn khai thác luôn Mắt Siêu Âm + Cảm biến chân `10-NK` cho AI & Web `:8080`:**
  1. Mở phần mềm **RoboBASIC v2.5 / v2.8**, mở file `mr_c3024_robobasic\code_robonova2_ai_sensors.bas`.
  2. Kiểm tra 2 hằng số ở đầu file cho khớp đúng chân cắm cảm biến trên mạch lưng `MR-C3024FX` của anh:
     - `CONST FOOT_AD_PORT = 0`: Cổng `AD(0..4)` đang cắm dây tín hiệu cảm biến `10-NK` dưới chân trái.
     - `CONST HEAD_SONAR_PORT = 5`: Cổng `SONAR(0..7)` của mắt siêu âm (hoặc đổi `CONST USE_SONAR_DIGITAL = 0` nếu mắt là cảm biến hồng ngoại/siêu âm xuất áp Analog vào `AD(1)`).
  3. Nạp xuống `MR-C3024FX` và nối thêm dây từ chân **`A` (`ETX`)** trên `MR-C3024FX` sang **`GPIO 41`** của `ESP32-S3`. Ngay lập tức thanh trạng thái trên Web `:8080` và chế độ **"Tự tránh vật cản / Tự leo bậc"** sẽ hoạt động thời gian thực!
