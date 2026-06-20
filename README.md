# 📦 AIoT Smart Card Recovery Box

> Hệ thống hòm thông minh tự động thu nhận và hỗ trợ hoàn trả thẻ sinh viên bị thất lạc, ứng dụng công nghệ AIoT và thuật toán học máy xử lý tại biên (Edge Computing).

---

## 🎯 Giới thiệu

Thẻ sinh viên tích hợp nhiều chức năng quan trọng (vào cổng trường, điểm danh, thư viện, gửi xe, ví điện tử nội bộ), và tình trạng làm rơi thẻ xảy ra rất thường xuyên. Quy trình xử lý thủ công hiện tại — đăng lên mạng xã hội để tìm chủ — vừa kém hiệu quả vừa có nguy cơ lộ thông tin cá nhân.

Dự án này xây dựng một **trạm Kiosk thông minh** hoạt động 24/7 để giải quyết bài toán trên:

- Người nhặt được thẻ chỉ cần **thả vào hòm** — hệ thống tự xử lý toàn bộ.
- Hệ thống **nhận diện MSSV** bằng camera nhúng và thuật toán học máy ngay tại thiết bị.
- **Gửi email thông báo tự động** kèm mã OTP 4 chữ số về Gmail sinh viên.
- Sinh viên đến nhập mã OTP → hòm **tự động mở** và trả thẻ.

---

## 🏗️ Kiến trúc hệ thống

```
┌─────────────────────────────────────────────────────────┐
│              CLOUD SERVICES (Google)                    │
│   Google Sheets (DB)  ←→  Apps Script  ←→  Gmail API   │
└──────────────────────┬──────────────────────────────────┘
                       │ HTTPS
┌──────────────────────┴──────────────────────────────────┐
│            MCU TRUNG TÂM (ESP32)                        │
│  State Machine · PWM Servo · I2C OLED · Web Server      │
└────────┬──────────────────────────────┬─────────────────┘
         │ UART / Local WiFi            │ I2C / PWM
┌────────┴──────────┐       ┌───────────┴───────────────┐
│  ESP32-CAM        │       │  OUTPUT ACTUATORS          │
│  · Chụp ảnh       │       │  · OLED 1.3"               │
│  · DFS Barcode    │       │  · Servo giữ thẻ (SG90)    │
│  · HOG + Softmax  │       │  · Servo gạt thẻ (SG90)    │
│  · Web Setup UI   │       │  · Servo khóa cửa          │
└───────────────────┘       └───────────────────────────┘
         ↑
┌────────┴──────────┐
│  INPUT            │
│  · Keypad 4×4     │
│    (PCF8574 I2C)  │
└───────────────────┘
```

---

## ✨ Tính năng

### Kịch bản 1 — Thu nhận thẻ tự động
1. Người dùng đặt thẻ vào khay → servo kẹp giữ thẻ tại tiêu cự camera.
2. ESP32-CAM chụp ảnh, thực hiện toàn bộ pipeline nhận diện ngay tại biên:
   - Nhị phân hóa ảnh (Thresholding)
   - DFS định vị Barcode → tính góc nghiêng
   - Biến đổi Affine căn chỉnh dải số
   - Trích xuất đặc trưng HOG
   - Phân loại Softmax → ra chuỗi MSSV 8 chữ số
3. Servo gạt thẻ vào ngăn chứa.
4. Hệ thống gửi **email tự động** đến `MSSV@vnu.edu.vn` kèm mã OTP ngẫu nhiên.
5. Dữ liệu `(MSSV, OTP)` được lưu vào Google Sheets.

### Kịch bản 2 — Hoàn trả thẻ chủ động
1. Sinh viên đến hòm, nhập mã OTP 4 chữ số nhận được qua email.
2. Hệ thống gọi Apps Script đối chiếu OTP với Google Sheets.
3. Khớp → servo rút chốt khóa, cửa hộp mở → sinh viên lấy thẻ.
4. Hàng dữ liệu OTP bị xóa khỏi Sheets (tránh tái sử dụng).

### Tính năng bổ sung
- **Web Setup UI** nhúng trên ESP32-CAM: tinh chỉnh tọa độ cắt ảnh (ROI) theo thời gian thực, lưu vào Flash.
- **Fallback nhập tay**: nếu nhận diện thất bại >N lần, sinh viên có thể nhập MSSV thủ công qua bàn phím.
- **Fail-safe 2 tầng**: Watchdog phần cứng + State Timeout phần mềm (30s), tự phục hồi về menu chính.

---

## 🧠 Thuật toán học máy (Edge AI)

Toàn bộ nhận diện chạy **offline trên ESP32-CAM**, không cần gửi ảnh lên cloud:

| Bước | Phương pháp | Mô tả |
|------|-------------|-------|
| 1 | Grayscale + Threshold | Nhị phân hóa ảnh |
| 2 | DFS (Depth-First Search) | Tìm vùng Barcode làm mốc định vị |
| 3 | Affine Transform | Căn chỉnh góc nghiêng bằng Inverse Mapping |
| 4 | Sliding Window + Moving Average | Phân đoạn và tinh chỉnh trọng tâm 8 ký tự |
| 5 | HOG Features | Trích xuất đặc trưng hướng Gradient, bất biến với ánh sáng |
| 6 | Softmax Regression | Phân loại chữ số 0–9, ổn định số học trên phần cứng nhúng |

**Kết quả thực nghiệm** (200 lượt, góc nghiêng ±15°):
- Định vị Barcode (DFS): **96.5%**
- Nhận diện chữ số tổng quát: **95.88%**
- Độ trễ gửi email (Cloud): trung bình **2.45s**
- Độ trễ xác thực OTP: trung bình **1.30s**

---

## 🔧 Phần cứng

| Linh kiện | Vai trò |
|-----------|---------|
| ESP32 | Vi điều khiển trung tâm, WiFi, State Machine |
| ESP32-CAM | Camera nhúng + toàn bộ pipeline xử lý ảnh |
| OLED 1.3" (I2C) | Màn hình hiển thị hướng dẫn và trạng thái |
| Keypad 4×4 + PCF8574 | Nhập mã OTP qua I2C, tiết kiệm chân IO |
| Servo SG90 × 2 | Giữ thẻ tại tiêu cự, gạt thả thẻ (tầng trên) |
| Servo kim loại × 1 | Chốt khóa cửa ngăn chứa (tầng dưới) |
| LED + buồng tối | Chiếu sáng chuẩn hóa cho camera |

---

## ☁️ Hạ tầng Cloud

- **Google Apps Script**: nhận HTTPS POST từ ESP32, định tuyến logic ghi/đọc.
- **Google Sheets**: cơ sở dữ liệu động lưu cặp `(MSSV, OTP)`.
- **Gmail API**: gửi email thông báo tự động đến sinh viên.

> Kiến trúc này không yêu cầu server riêng — hoàn toàn miễn phí với tài khoản Google thông thường.

---

## 🚀 Cài đặt & Triển khai

### Yêu cầu
- Arduino IDE với board package **esp32 by Espressif**
- Tài khoản Google (Google Sheets + Apps Script)

### Các bước

**1. Clone repo**
```bash
git clone https://github.com/<your-username>/<repo-name>.git
cd <repo-name>
```

**2. Cấu hình Cloud**
- Tạo Google Sheet mới, lấy Spreadsheet ID.
- Deploy Apps Script từ thư mục `cloud/`, lấy Web App URL.
- Điền vào `config.h`:
```cpp
#define APPS_SCRIPT_URL  "https://script.google.com/macros/s/..."
#define SENDER_EMAIL     "your-gmail@gmail.com"
#define SENDER_PASSWORD  "your-app-password"
```

**3. Nạp firmware ESP32 (trung tâm)**
- Mở `firmware/esp32_main/esp32_main.ino`
- Chọn board: **ESP32 Dev Module**
- Upload

**4. Nạp firmware ESP32-CAM**
- Mở `firmware/esp32cam/esp32cam.ino`
- Chọn board: **AI Thinker ESP32-CAM**
- Upload (cần IO0 → GND khi nạp)

**5. Hiệu chuẩn camera (Web Setup)**
- Kích hoạt chế độ nhà sản xuất trên bàn phím.
- Truy cập `http://<IP-ESP32-CAM>/setup` trên trình duyệt.
- Điều chỉnh `ΔX`, `ΔY` để ROI khớp với vùng MSSV trên thẻ.
- Nhấn **Lưu & Reset**.

---

## 📁 Cấu trúc thư mục

```
├── firmware/
│   ├── esp32_main/        # Firmware vi điều khiển trung tâm
│   └── esp32cam/          # Firmware camera + AI pipeline
├── cloud/
│   └── apps_script.gs     # Google Apps Script (backend)
├── hardware/
│   ├── schematic.pdf      # Sơ đồ mạch
│   └── bom.csv            # Danh sách linh kiện
├── docs/
│   └── report.pdf         # Báo cáo tiểu luận đầy đủ
└── README.md
```

---

## 📊 Kết quả thực nghiệm

Thử nghiệm với **50 phôi thẻ sinh viên**, **200 lượt thả thẻ**:

```
Chữ số    Mẫu    Đúng    Chính xác
────────────────────────────────────
0         160    156     97.50%
1         160    158     98.75%  ← tốt nhất
2         160    152     95.00%
3         160    151     94.38%
4         160    154     96.25%
5         160    150     93.75%  ← thấp nhất (dễ nhầm với 6)
6         160    153     95.63%
7         160    157     98.13%
8         160    151     94.38%
9         160    152     95.00%
────────────────────────────────────
Tổng      1600   1534    95.88%
```

---

## 🔮 Hướng phát triển

- [ ] Tích hợp mô hình **TensorFlow Lite for Microcontrollers** thay HOG+Softmax để tăng độ chính xác trên thẻ bị mờ.
- [ ] Thêm **Timestamp & auto-expire OTP** sau 24 giờ trên Apps Script.
- [ ] Thiết kế **PCB công nghiệp** thay thế module thử nghiệm.
- [ ] Tích hợp **cảm biến trọng lượng** để cảnh báo khi hộp đầy.
- [ ] Mở rộng hỗ trợ đa mẫu thẻ (các trường đại học khác).

---

## 👨‍💻 Tác giả

**Tạ Thanh Hải**  
Khoa Vật lý — Trường Đại học Khoa học Tự nhiên, ĐHQGHN  
Ngành Kỹ thuật Điện tử và Tin học  

Giảng viên hướng dẫn: **ThS. Nguyễn Cảnh Việt** & **CN. Vi Anh Quân**

---

## 📄 Giấy phép

Dự án thuộc phạm vi học thuật. Mọi trích dẫn hoặc tái sử dụng vui lòng ghi rõ nguồn.
