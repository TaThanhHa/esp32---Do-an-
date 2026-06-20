#include "HogClassifier.h"
#include <Arduino.h>
#include <cmath>
#include <cstring>
#include "model.h" // Nhúng bộ trọng số W và b của mô hình

#define PI 3.14159265358979323846

// Cấu hình kích thước nội bộ
#define CELL_SIZE 4
#define BLOCK_SIZE 2
#define BINS 9

#define CELLS_X (IMG_W / CELL_SIZE)
#define CELLS_Y (IMG_H / CELL_SIZE)
#define BLOCKS_X (CELLS_X - BLOCK_SIZE + 1)
#define BLOCKS_Y (CELLS_Y - BLOCK_SIZE + 1)
#define FEATURES_PER_BLOCK (BLOCK_SIZE * BLOCK_SIZE * BINS)
#define TOTAL_FEATURES (BLOCKS_X * BLOCKS_Y * FEATURES_PER_BLOCK) // 2160

// Hàm tính toán HOG (Chỉ dùng nội bộ trong file này)
static void compute_hog(const float img[IMG_H][IMG_W], float *features) {
    float cell_hist[CELLS_Y][CELLS_X][BINS];
    memset(cell_hist, 0, sizeof(cell_hist));

    for (int y = 0; y < IMG_H; y++) {
        for (int x = 0; x < IMG_W; x++) {
            int x_left = (x == 0) ? 0 : x - 1;
            int x_right = (x == IMG_W - 1) ? IMG_W - 1 : x + 1;
            int y_top = (y == 0) ? 0 : y - 1;
            int y_bottom = (y == IMG_H - 1) ? IMG_H - 1 : y + 1;

            float dx = img[y][x_right] - img[y][x_left];
            float dy = img[y_bottom][x] - img[y_top][x];

            float mag = sqrt(dx * dx + dy * dy);
            if (mag == 0) continue;

            float angle = atan2(dy, dx) * (180.0 / PI);
            if (angle < 0) angle += 180.0;
            if (angle >= 180.0) angle -= 180.0;

            float bin_width = 180.0 / BINS;
            float bin_pos = angle / bin_width;
            
            int bin1 = (int)(bin_pos - 0.5);
            float weight2 = bin_pos - 0.5 - bin1;
            float weight1 = 1.0 - weight2;

            if (bin1 < 0) bin1 += BINS;
            int bin2 = (bin1 + 1) % BINS;

            int cx = x / CELL_SIZE;
            int cy = y / CELL_SIZE;

            cell_hist[cy][cx][bin1] += mag * weight1;
            cell_hist[cy][cx][bin2] += mag * weight2;
        }
    }

    int feature_idx = 0;
    for (int by = 0; by < BLOCKS_Y; by++) {
        for (int bx = 0; bx < BLOCKS_X; bx++) {
            float block_vector[FEATURES_PER_BLOCK];
            int v_idx = 0;
            float sum_sq = 0.0;

            for (int cy = by; cy < by + BLOCK_SIZE; cy++) {
                for (int cx = bx; cx < bx + BLOCK_SIZE; cx++) {
                    for (int b = 0; b < BINS; b++) {
                        float val = cell_hist[cy][cx][b];
                        block_vector[v_idx++] = val;
                        sum_sq += val * val;
                    }
                }
            }

            float l2_norm = sqrt(sum_sq + 1e-5);
            sum_sq = 0.0;
            
            for (int i = 0; i < FEATURES_PER_BLOCK; i++) {
                block_vector[i] /= l2_norm;
                if (block_vector[i] > 0.2f) block_vector[i] = 0.2f;
                sum_sq += block_vector[i] * block_vector[i];
            }

            l2_norm = sqrt(sum_sq + 1e-5);
            for (int i = 0; i < FEATURES_PER_BLOCK; i++) {
                features[feature_idx++] = block_vector[i] / l2_norm;
            }
        }
    }
}

// Hàm Softmax (Chỉ dùng nội bộ)
static void softmax(float *input, float *output, int n) {
    float max_val = input[0];
    for (int i = 1; i < n; i++)
        if (input[i] > max_val) max_val = input[i];

    float sum = 0.0;
    for (int i = 0; i < n; i++) {
        output[i] = exp(input[i] - max_val);
        sum += output[i];
    }

    for (int i = 0; i < n; i++)
        output[i] /= sum;
}

// Hàm phân loại từ đặc trưng (Chỉ dùng nội bộ)
static int predict_from_features(float *x) {
    float logits[NUM_CLASSES];
    float probs[NUM_CLASSES];

    for (int i = 0; i < NUM_CLASSES; i++) {
        logits[i] = b[i]; 
        for (int j = 0; j < HOG_DIM; j++) {
            logits[i] += W[i][j] * x[j]; 
        }
    }

    softmax(logits, probs, NUM_CLASSES);

    int best = 0;
    for (int i = 1; i < NUM_CLASSES; i++) {
        if (probs[i] > probs[best])
            best = i;
    }

    return best;
}

// Thực thi API chính
int predict_image(const float img[IMG_H][IMG_W]) {
    float* hog_feat = (float*)malloc(TOTAL_FEATURES * sizeof(float));
    
    if (hog_feat == NULL) {
        Serial.println("Lỗi RAM: Không thể cấp phát bộ nhớ cho HOG features!");
        return -1; 
    }

    compute_hog(img, hog_feat);
    int predicted_label = predict_from_features(hog_feat);
    
    free(hog_feat); 
    return predicted_label;
}