// ==========================================================
// VAI TRÒ: ESP32 DEV - Hệ thống nhặt thẻ sinh viên
// Chức năng: AI Camera, Gmail, Google Sheet, Servo Cơ khí
// Version: 3.0 - Tích hợp Servo & Kịch bản cửa/gạt thẻ
// ==========================================================

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <WiFiClientSecure.h>
#include <Wire.h>
#include <U8g2lib.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include <HTTPClient.h>
#include "HogClassifier.h"
#include <PCF8574.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <esp_task_wdt.h>

// ----------------------------------------------------------
// CẤU HÌNH WATCHDOG & TIMEOUT
// ----------------------------------------------------------
#define WDT_TIMEOUT          30      
#define WIFI_CONNECT_TIMEOUT 15      
#define HTTP_TIMEOUT_MS      20000   
#define CAM_READY_TIMEOUT_MS 30000   
#define CAM_CAPTURE_TIMEOUT_MS 60000 
#define AI_PROCESS_TIMEOUT_MS  30000 
#define OLED_MSG_DURATION_MS   2500  

// ----------------------------------------------------------
// PHẦN CỨNG GIAO TIẾP & MẠNG
// ----------------------------------------------------------
#define SDA_PIN     21
#define SCL_PIN     22
#define CAM_RX_PIN  17
#define CAM_TX_PIN  16

const char* AP_SSID = "ESP32_INTERNAL_NET";
const char* AP_PASS = "12345678";
const char* CAM_IP  = "192.168.4.2";
// >>> THAY LINK APP SCRIPT CỦA BẠN VÀO ĐÂY <<<
const char* GAS_URL = "https://script.google.com/macros/s/AKfycbyyPehvIxeS1miQr2EcqwXg8hEvHdEndVEmVxw6ONNEPiqaXgmW58UJduYCt7UbYTM/exec";

// ----------------------------------------------------------
// CẤU HÌNH SERVO CƠ KHÍ
// ----------------------------------------------------------
#define SERVO1_PIN 25 // Servo cửa
#define SERVO2_PIN 26 // Servo gạt thẻ 1
#define SERVO3_PIN 27 // Servo gạt thẻ 2

#define SERVO_FREQ 50
#define SERVO_RES 16  // độ phân giải cao cho mượt

#define CH1 0
#define CH2 1
#define CH3 2

// HÀM CHUYỂN GÓC -> DUTY CHO SERVO
uint32_t angleToDuty(int angle) {
    int us = map(angle, 0, 180, 500, 2500);
    uint32_t duty = (uint32_t)((us / 20000.0) * ((1 << SERVO_RES) - 1));
    return duty;
}

void setServo(int ch, int angle) {
    angle = constrain(angle, 0, 180);
    uint32_t duty = angleToDuty(angle);
    ledcWrite(ch, duty);
}

// ----------------------------------------------------------
// OLED & KEYPAD
// ----------------------------------------------------------
U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE, SCL_PIN, SDA_PIN);

#define I2C_ADDR 0x20
PCF8574 pcf8574(I2C_ADDR);
#define ROWS 4
#define COLS 4
char keys[ROWS][COLS] = {
    {'1','2','3','A'},
    {'4','5','6','B'},
    {'7','8','9','C'},
    {'*','0','#','D'}
};
uint8_t rowPins[ROWS] = {0, 1, 2, 3};
uint8_t colPins[COLS] = {4, 5, 6, 7};

// ----------------------------------------------------------
// AI - ẢNH & BUFFER
// ----------------------------------------------------------
#define NUM_DIGITS  8
#define DIGIT_W     28
#define DIGIT_H     44
#define DIGIT_BYTES (DIGIT_W * DIGIT_H)

static uint8_t rawBufs[NUM_DIGITS][DIGIT_BYTES];
static int     results[NUM_DIGITS];
static int     predictedCount = 0;

TaskHandle_t  TaskAI_Handle;
QueueHandle_t imageQueue;
QueueHandle_t resultQueue;

AsyncWebServer server(80);

// ----------------------------------------------------------
// STATE MACHINE
// ----------------------------------------------------------
enum AppState {
    STATE_INIT,
    STATE_WAIT_WIFI_SETUP,   
    STATE_WAIT_CAM,          
    STATE_MAIN_MENU,         
    STATE_SETUP_CAM,         
    STATE_PROCESSING,        
    STATE_RESULT,            
    STATE_MANUAL_INPUT,      
    STATE_RETRIEVE_INPUT,    
    STATE_WAIT_LOCK_DOOR,    // TRẠNG THÁI MỚI: Chờ bấm khóa cửa
    STATE_ERROR              
};
AppState appState = STATE_INIT;

// ----------------------------------------------------------
// BIẾN TRẠNG THÁI
// ----------------------------------------------------------
char manualBuffer[9]   = "";  
int  manualIdx         = 0;
char currentMSV[9]     = "";  
char generatedCode[5]  = "";  

char retrieveBuffer[5] = "";  
int  retrieveIdx       = 0;

bool screenNeedsUpdate = true;

unsigned long stateEnteredAt  = 0;  
unsigned long lastProgressAt  = 0;  

// ----------------------------------------------------------
// FORWARD DECLARATIONS
// ----------------------------------------------------------
static void sendEmailThroughAppScript();
static void verifyCodeWithSheet(const char* code);
static bool sendCaptureCommand(); 
static void handleUart();
char        scanKeypadNonBlocking();
void        oledText(const char* l1, const char* l2 = NULL,
                     const char* l3 = NULL, const char* l4 = NULL);
void        enterState(AppState newState);

// ==========================================================
// OLED UTILITIES
// ==========================================================
void oledText(const char* l1, const char* l2, const char* l3, const char* l4) {
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_6x10_tr);
    if (l1) u8g2.drawStr(0, 15, l1);
    if (l2) u8g2.drawStr(0, 30, l2);
    if (l3) u8g2.drawStr(0, 45, l3);
    if (l4) u8g2.drawStr(0, 60, l4);
    u8g2.sendBuffer();
}

void oledError(const char* l1, const char* l2 = NULL) {
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_6x10_tr);
    u8g2.drawStr(0, 15, "!! LOI !!");
    if (l1) u8g2.drawStr(0, 30, l1);
    if (l2) u8g2.drawStr(0, 45, l2);
    u8g2.drawStr(0, 60, "Vui long thu lai...");
    u8g2.sendBuffer();
}

void enterState(AppState newState) {
    appState         = newState;
    stateEnteredAt   = millis();
    lastProgressAt   = millis();
    screenNeedsUpdate = true;
}

// ==========================================================
// KEYPAD SCAN (NON-BLOCKING + DEBOUNCE HOÀN CHỈNH)
// ==========================================================
char scanKeypadNonBlocking() {
    static unsigned long lastDebounceTime = 0;
    static bool keyReleased = true;

    for (int col = 0; col < COLS; col++) {
        for (int i = 0; i < 8; i++) pcf8574.write(i, HIGH);
        pcf8574.write(colPins[col], LOW);

        for (int row = 0; row < ROWS; row++) {
            if (pcf8574.read(rowPins[row]) == LOW) {
                if (keyReleased) {
                    if (millis() - lastDebounceTime > 50) {
                        keyReleased = false;
                        lastDebounceTime = millis();
                        return keys[row][col]; 
                    }
                } else {
                    lastDebounceTime = millis(); 
                }
                return 0; 
            }
        }
    }
    
    if (!keyReleased && (millis() - lastDebounceTime > 50)) {
        keyReleased = true; 
    }
    return 0;
}

// ==========================================================
// AI TASK (Chạy trên CORE 0)
// ==========================================================
static void decodeAndPredict(int idx) {
    if (ESP.getFreeHeap() < DIGIT_BYTES * sizeof(float) + 4096) {
        Serial.printf("[AI] Khong du RAM cho digit %d\n", idx);
        results[idx] = -1;
        return;
    }

    float* imgFlat = (float*) malloc(DIGIT_H * DIGIT_W * sizeof(float));
    if (!imgFlat) {
        Serial.printf("[AI] malloc that bai cho digit %d\n", idx);
        results[idx] = -1;
        return;
    }

    float (*imgBuf)[DIGIT_W] = (float(*)[DIGIT_W]) imgFlat;
    for (int y = 0; y < DIGIT_H; y++)
        for (int x = 0; x < DIGIT_W; x++)
            imgBuf[y][x] = rawBufs[idx][y * DIGIT_W + x] / 255.0f;

    results[idx] = predict_image(imgBuf);
    free(imgFlat);
    Serial.printf("[AI] Digit %d → %d\n", idx, results[idx]);
}

void TaskAI_Code(void* pvParameters) {
    int incoming_idx;
    for (;;) {
        if (xQueueReceive(imageQueue, &incoming_idx, portMAX_DELAY) == pdPASS) {
            decodeAndPredict(incoming_idx);
            xQueueSend(resultQueue, &incoming_idx, portMAX_DELAY);
        }
    }
}

// ==========================================================
// SINH MÃ NGẪU NHIÊN 4 SỐ
// ==========================================================
void generateCode() {
    randomSeed(esp_random());
    int code = random(1000, 10000); 
    sprintf(generatedCode, "%04d", code);
    Serial.printf("[CODE] Ma sinh ra: %s\n", generatedCode);
}

// ==========================================================
// HTTP: GỬI LỆNH CHỤP ẢNH CHO CAM
// ==========================================================
static bool sendCaptureCommand() {
    char url[48];
    sprintf(url, "http://%s/capture", CAM_IP);

    HTTPClient http;
    http.begin(url);
    http.setTimeout(HTTP_TIMEOUT_MS);

    Serial.println("[CAM] Gui lenh chup...");
    int code = http.GET();

    if (code == 200) {
        Serial.println("[CAM] Lenh chup OK");
        http.end();
        return true; 
    } 
    
    String errorMsg = http.getString(); 
    Serial.printf("[CAM ERR] Code: %d, Chi tiet: %s\n", code, errorMsg.c_str());
    
    if (code == 500) {
        oledError("CAM bao loi:", errorMsg.c_str());
    } else if (code < 0) {
        oledError("Loi mang CAM:", http.errorToString(code).c_str());
    } else {
        char header[24];
        sprintf(header, "Loi HTTP %d", code);
        oledError(header, "Xem Serial de biet");
    }
    
    delay(3000); 
    http.end();
    return false; 
}

// ==========================================================
// HTTP: GỬI MAIL + GẠT THẺ VÀO HỘP
// ==========================================================
static void sendEmailThroughAppScript() {
    generateCode();

    oledText("DANG GUI MAIL...", currentMSV, "Ma xac nhan:", generatedCode);
    esp_task_wdt_reset();

    WiFiClientSecure client;
    client.setInsecure();

    HTTPClient http;
    http.begin(client, GAS_URL);
    http.setTimeout(HTTP_TIMEOUT_MS);
    http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
    http.addHeader("Content-Type", "application/json");

    char payload[200];
    sprintf(payload, "{\"action\":\"send_mail\",\"msv\":\"%s\",\"code\":\"%s\"}", currentMSV, generatedCode);

    Serial.printf("[MAIL] Payload: %s\n", payload);
    int code = http.POST(payload);
    Serial.printf("[MAIL] HTTP code: %d\n", code);

    if (code == 200 || code == 400 || code == 302 || code == 303) {
        // --- 1. HIỆN THÔNG BÁO THÀNH CÔNG ---
        oledText("THANH CONG!", "Da luu vao Sheet", "Dang gat the...", currentMSV);
        
        // --- 2. SERVO GẠT THẺ ---
        setServo(CH2, 100);
        setServo(CH3, 50);
        
        // --- 3. ĐỢI 3 GIÂY ---
        delay(3000);
        
        // --- 4. TRẢ SERVO GẠT THẺ VỀ NHƯ CŨ ---
        setServo(CH2, 38);
        setServo(CH3, 107);
        delay(500); // Cho servo thời gian quay về

        enterState(STATE_MAIN_MENU);
    } else if (code < 0) {
        oledError("Loi mang GAS:", http.errorToString(code).c_str());
        delay(OLED_MSG_DURATION_MS);
        enterState(STATE_MAIN_MENU);
    } else {
        char header[24];
        sprintf(header, "HTTP GAS: %d", code);
        oledError(header, "Xem Serial de biet");
        delay(OLED_MSG_DURATION_MS);
        enterState(STATE_MAIN_MENU);
    }
    http.end();
}

// ==========================================================
// HTTP: KIỂM TRA MÃ + MỞ CỬA LẤY THẺ
// ==========================================================
static void verifyCodeWithSheet(const char* testCode) {
    oledText("DANG KIEM TRA...", "Ket noi Sheet...", "Vui long cho", "");
    esp_task_wdt_reset();

    WiFiClientSecure client;
    client.setInsecure();

    HTTPClient http;
    http.begin(client, GAS_URL);
    http.setTimeout(HTTP_TIMEOUT_MS);
    
    http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
    http.addHeader("Content-Type", "application/json");

    const char * headerKeys[] = {"Location"};
    http.collectHeaders(headerKeys, 1);

    char payload[64];
    sprintf(payload, "{\"action\":\"verify_code\",\"code\":\"%s\"}", testCode);

    int code = http.POST(payload);
    String response = "";

    if (code == 302 || code == 303) {
        String location = http.header("Location"); 
        http.end(); 

        if (location.length() > 0) {
            http.begin(client, location); 
            http.setTimeout(HTTP_TIMEOUT_MS);
            code = http.GET(); 
            if (code == 200) response = http.getString();
        }
    } else if (code == 200) {
        response = http.getString();
    }

    if (code == 200 && response.length() > 0) {
        if (response.indexOf("\"status\":\"OK\"") >= 0) {
            // --- 1. MỞ SERVO CỬA ---
            setServo(CH1, 90);
            
            // --- 2. CHUYỂN SANG TRẠNG THÁI CHỜ KHOÁ CỬA ---
            enterState(STATE_WAIT_LOCK_DOOR);
            
        } else if (response.indexOf("\"status\":\"FAIL\"") >= 0) {
            oledError("SAI MA!", "Kiem tra lai email");
            delay(OLED_MSG_DURATION_MS);
            enterState(STATE_RETRIEVE_INPUT); 
        } else {
            oledError("Phan hoi la", "Kiem tra GAS");
            delay(OLED_MSG_DURATION_MS);
            enterState(STATE_RETRIEVE_INPUT);
        }
    } else if (code < 0) {
        oledError("Loi mang GAS:", http.errorToString(code).c_str());
        delay(OLED_MSG_DURATION_MS);
        enterState(STATE_RETRIEVE_INPUT);
    } else {
        char header[24];
        sprintf(header, "HTTP GAS: %d", code);
        oledError(header, "Khong doc duoc kq");
        delay(OLED_MSG_DURATION_MS);
        enterState(STATE_RETRIEVE_INPUT);
    }
    
    http.end();
}

// ==========================================================
// UART: NHẬN TÍN HIỆU TỪ CAM
// ==========================================================
static void handleUart() {
    if (!Serial2.available()) return;
    String msg = Serial2.readStringUntil('\n');
    msg.trim();
    if (msg.length() == 0) return;

    if (msg == "CAM_READY") {
        if (appState == STATE_WAIT_CAM || appState == STATE_SETUP_CAM) {
            oledText("Camera san sang!", "Chuyen vao Menu...");
            delay(1000);
            enterState(STATE_MAIN_MENU);
        }
    } else if (msg == "CAM_ERROR") {
        oledError("Camera bao loi", "Khoi dong lai CAM");
        delay(OLED_MSG_DURATION_MS);
    } else if (msg.startsWith("SETUP_IP:")) {
        String camIP = msg.substring(9);
        String l4 = "IP: " + camIP;
        oledText("CAM DANG SETUP", "Vao chung mang nha", l4.c_str(), "[D] Thoat Setup");
    } else if (msg == "SETUP_ERR") {
        oledError("Loi Setup CAM", "Khong vao dc mang");
        delay(3000);
        enterState(STATE_MAIN_MENU);
    }
}

void configModeCallback(WiFiManager* myWiFiManager) {
    oledText("Khong co mang!", "Ket noi WiFi:", "Setup_Esp32", "IP: 192.168.4.1");
}

// ==========================================================
// SETUP
// ==========================================================
void setup() {
    Serial.begin(115200);
    Serial.println("\n[BOOT] He thong khoi dong...");

    esp_task_wdt_init(WDT_TIMEOUT, true);
    esp_task_wdt_add(NULL);

    // SETUP SERVO (Làm trước để servo đóng ngay lập tức)
    ledcSetup(CH1, SERVO_FREQ, SERVO_RES);
    ledcSetup(CH2, SERVO_FREQ, SERVO_RES);
    ledcSetup(CH3, SERVO_FREQ, SERVO_RES);
    
    ledcAttachPin(SERVO1_PIN, CH1);
    ledcAttachPin(SERVO2_PIN, CH2);
    ledcAttachPin(SERVO3_PIN, CH3);

    // KỊCH BẢN KHỞI ĐỘNG CƠ KHÍ
    setServo(CH1, 0);   // Khoá cửa
    setServo(CH2, 38);  // Gạt thẻ góc mặc định
    setServo(CH3, 107); // Gạt thẻ góc mặc định
    delay(500);         // Đợi servo định vị

    Wire.begin(SDA_PIN, SCL_PIN);
    u8g2.begin();
    oledText("Khoi dong...", "Kiem tra phan cung");

    if (!pcf8574.begin()) {
        oledError("LOI PHAN CUNG", "Keypad I2C khong");
        Serial.println("[ERR] PCF8574 khong tim thay!");
        while (1) {
            esp_task_wdt_reset();
            delay(1000);
        }
    }
    Serial.println("[OK] PCF8574 Keypad");

    Serial2.begin(115200, SERIAL_8N1, CAM_RX_PIN, CAM_TX_PIN);
    Serial.println("[OK] Serial2 (CAM UART)");

    imageQueue  = xQueueCreate(NUM_DIGITS, sizeof(int));
    resultQueue = xQueueCreate(NUM_DIGITS, sizeof(int));
    xTaskCreatePinnedToCore(TaskAI_Code, "TaskAI", 16384, NULL, 1, &TaskAI_Handle, 0);

    randomSeed(esp_random());

    oledText("Khoi dong...", "Dang ket noi WiFi", "Vui long cho...");
    WiFi.begin();
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < WIFI_CONNECT_TIMEOUT) {
        delay(500);
        esp_task_wdt_reset();
        attempts++;
        Serial.print(".");
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("[WiFi] Da ket noi: %s\n", WiFi.SSID().c_str());
        WiFi.mode(WIFI_AP_STA);
        WiFi.softAP(AP_SSID, AP_PASS);
        enterState(STATE_WAIT_CAM);
    } else {
        Serial.println("[WiFi] Khong co mang da luu");
        enterState(STATE_WAIT_WIFI_SETUP);
    }

    server.on("/upload", HTTP_POST,
        [](AsyncWebServerRequest* req) {
            int idx = req->hasParam("index") ? req->getParam("index")->value().toInt() : -1;
            if (idx >= 0 && idx < NUM_DIGITS) {
                if (xQueueSend(imageQueue, &idx, 0) != pdPASS) {}
            }
            req->send(200, "text/plain", "OK");
        },
        NULL,
        [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
            int idx = req->hasParam("index") ? req->getParam("index")->value().toInt() : -1;
            if (idx >= 0 && idx < NUM_DIGITS && index + len <= DIGIT_BYTES) {
                memcpy(rawBufs[idx] + index, data, len);
            }
        }
    );
    server.begin();
}

// ==========================================================
// LOOP - STATE MACHINE CHÍNH
// ==========================================================
void loop() {
    esp_task_wdt_reset();
    handleUart();
    char key = scanKeypadNonBlocking();

    switch (appState) {

        case STATE_WAIT_WIFI_SETUP:
            if (screenNeedsUpdate) {
                oledText("Khong co WiFi!", "Bam [D] de", "Setup WiFi moi", "");
                screenNeedsUpdate = false;
            }
            if (key == 'D') {
                oledText("Dang mo Portal...", "Ket noi WiFi:", "Setup_Esp32", "IP: 192.168.4.1");
                esp_task_wdt_delete(NULL); 
                WiFiManager wm;
                wm.setConnectTimeout(60);         
                wm.setConfigPortalTimeout(180);   
                wm.setAPCallback(configModeCallback);
                if (!wm.startConfigPortal("Setup_Esp32")) {
                    oledError("Setup WiFi loi", "Khoi dong lai...");
                    delay(2000); ESP.restart();
                }
                esp_task_wdt_add(NULL); 
                WiFi.mode(WIFI_AP_STA);
                WiFi.softAP(AP_SSID, AP_PASS);
                enterState(STATE_WAIT_CAM);
            }
            break;

        case STATE_WAIT_CAM:
            if (screenNeedsUpdate) {
                oledText("WiFi OK!", "Dang cho Camera", "khoi dong...", "");
                String extSSID = WiFi.SSID();
                String extPASS = WiFi.psk();
                Serial2.println("SYNC_WIFI:" + extSSID + "|" + extPASS);
                screenNeedsUpdate = false;
            }
            if (millis() - stateEnteredAt > CAM_READY_TIMEOUT_MS) {
                oledError("Camera khong phan hoi", "Kiem tra day cap");
                delay(OLED_MSG_DURATION_MS);
                stateEnteredAt = millis(); screenNeedsUpdate = true;
            }
            break;

        case STATE_MAIN_MENU:
            if (screenNeedsUpdate) {
                oledText("[A] Doc the SV", "[B] Lay the", "[C] Cai dat CAM", "[D] Nhap tay MSV");
                screenNeedsUpdate = false;
            }
            if (key == 'A') {
                memset(results, -1, sizeof(results));
                predictedCount = 0;
                xQueueReset(imageQueue); xQueueReset(resultQueue);
                oledText("DANG XU LY...", "Ra lenh CAM chup", "Vui long cho...", "");
                if (sendCaptureCommand() == true) enterState(STATE_PROCESSING);
                else enterState(STATE_MAIN_MENU); 
            }
            else if (key == 'B') {
                memset(retrieveBuffer, 0, sizeof(retrieveBuffer));
                retrieveIdx = 0;
                enterState(STATE_RETRIEVE_INPUT);
            }
            else if (key == 'C') {
                Serial2.println("CMD_SETUP");
                oledText("Gui lenh CAM...", "Cho CAM vao che", "do cai dat...", "");
                enterState(STATE_SETUP_CAM); 
            }
            else if (key == 'D') {
                memset(manualBuffer, 0, sizeof(manualBuffer));
                manualIdx = 0;
                enterState(STATE_MANUAL_INPUT);
            }
            break;
            
        case STATE_SETUP_CAM:
            if (key == 'D') {
                Serial2.println("CMD_EXIT_SETUP"); 
                oledText("Dang thoat...", "Cho CAM reset lai", "mang noi bo...");
                enterState(STATE_WAIT_CAM); 
            }
            break;

        case STATE_PROCESSING: {
            int finishedIdx;
            if (xQueueReceive(resultQueue, &finishedIdx, 0) == pdPASS) {
                predictedCount++;
                lastProgressAt = millis(); 
                char l2[24];
                sprintf(l2, "Tien do: %d/%d", predictedCount, NUM_DIGITS);
                oledText("DANG DOC THE...", l2, "Vui long cho...", "");

                if (predictedCount >= NUM_DIGITS) {
                    bool hasError = false;
                    for (int i = 0; i < NUM_DIGITS; i++) {
                        if (results[i] >= 0 && results[i] <= 9) currentMSV[i] = '0' + results[i];
                        else { currentMSV[i] = '?'; hasError = true; }
                    }
                    currentMSV[NUM_DIGITS] = '\0';
                    enterState(STATE_RESULT);
                }
            }

            if (millis() - lastProgressAt > AI_PROCESS_TIMEOUT_MS) {
                oledError("Timeout xu ly AI", "CAM co the bi loi");
                delay(OLED_MSG_DURATION_MS); enterState(STATE_MAIN_MENU);
            }
            break;
        }

        case STATE_RESULT:
            if (screenNeedsUpdate) {
                bool hasQuestion = (strchr(currentMSV, '?') != NULL);
                char header[24]; sprintf(header, "MSV: %s", currentMSV);
                if (hasQuestion) oledText(header, "[A] Chup lai", "[B] Gui Gmail(?)", "");
                else oledText(header, "[A] Chup lai", "[B] Gui Gmail", "");
                screenNeedsUpdate = false;
            }
            if (key == 'A') enterState(STATE_MAIN_MENU);
            else if (key == 'B') sendEmailThroughAppScript();
            break;

        case STATE_MANUAL_INPUT:
            if (screenNeedsUpdate) {
                char displayBuf[9] = "";
                for (int i = 0; i < 8; i++) displayBuf[i] = (i < manualIdx) ? manualBuffer[i] : '_';
                displayBuf[8] = '\0';
                oledText("NHAP MSV (8 SO):", displayBuf, "[B]Xoa [C]Clear", "[A]GuiMail [D]Thoat");
                screenNeedsUpdate = false;
            }
            if (key >= '0' && key <= '9') {
                if (manualIdx < 8) { manualBuffer[manualIdx++] = key; screenNeedsUpdate = true; }
            }
            else if (key == 'B') { if (manualIdx > 0) { manualBuffer[--manualIdx] = '\0'; screenNeedsUpdate = true; } }
            else if (key == 'C') { memset(manualBuffer, 0, sizeof(manualBuffer)); manualIdx = 0; screenNeedsUpdate = true; }
            else if (key == 'D') enterState(STATE_MAIN_MENU);
            else if (key == 'A') { 
                if (manualIdx == 8) { strcpy(currentMSV, manualBuffer); sendEmailThroughAppScript(); } 
                else { oledError("Thieu so!", "Can nhap du 8 so"); delay(1500); screenNeedsUpdate = true; }
            }
            break;

        case STATE_RETRIEVE_INPUT:
            if (screenNeedsUpdate) {
                char displayBuf[5] = "";
                for (int i = 0; i < 4; i++) displayBuf[i] = (i < retrieveIdx) ? retrieveBuffer[i] : '_';
                displayBuf[4] = '\0';
                oledText("NHAP MA 4 SO:", displayBuf, "[B]Xoa [C]Clear", "[A]XacNhan [D]Thoat");
                screenNeedsUpdate = false;
            }
            if (key >= '0' && key <= '9') {
                if (retrieveIdx < 4) { retrieveBuffer[retrieveIdx++] = key; screenNeedsUpdate = true; }
            }
            else if (key == 'B') { if (retrieveIdx > 0) { retrieveBuffer[--retrieveIdx] = '\0'; screenNeedsUpdate = true; } }
            else if (key == 'C') { memset(retrieveBuffer, 0, sizeof(retrieveBuffer)); retrieveIdx = 0; screenNeedsUpdate = true; }
            else if (key == 'D') enterState(STATE_MAIN_MENU);
            else if (key == 'A') { 
                if (retrieveIdx == 4) verifyCodeWithSheet(retrieveBuffer);
                else { oledError("Thieu so!", "Can nhap du 4 so"); delay(1500); screenNeedsUpdate = true; }
            }
            break;

        // TRẠNG THÁI MỚI: CHỜ NGƯỜI DÙNG KHOÁ CỬA
        case STATE_WAIT_LOCK_DOOR:
            if (screenNeedsUpdate) {
                oledText("CUA DANG MO!", "Lay the ra roi", "dong cua lai", "Bam [A] de KHOA");
                screenNeedsUpdate = false;
            }
            if (key == 'A') {
                oledText("DANG KHOA...", "Vui long doi", "", "");
                setServo(CH1, 0); // Đóng khóa cửa lại
                delay(1000); // Chờ servo quay xong
                enterState(STATE_MAIN_MENU);
            }
            break;

        default:
            enterState(STATE_MAIN_MENU);
            break;
    }
}