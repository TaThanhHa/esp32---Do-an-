#ifndef HOG_CLASSIFIER_H
#define HOG_CLASSIFIER_H

// Định nghĩa kích thước ảnh đầu vào bắt buộc
#define IMG_W 28
#define IMG_H 44

// Hàm API chính: Truyền vào mảng ảnh 2D, trả về nhãn dự đoán (0-9)
// Trả về -1 nếu ESP32 bị hết bộ nhớ RAM (lỗi cấp phát)
int predict_image(const float img[IMG_H][IMG_W]);

#endif // HOG_CLASSIFIER_H
