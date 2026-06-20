// ==========================================================
// FILE: esp32_cam_mixed.cpp
// VAI TRÒ: ESP32-CAM - Xử lý RAW, Nhận đồng bộ WiFi từ DEV
// VERSION: 2.1 - Fix kẹt state Setup WiFi, Tối ưu UART
// ==========================================================

#include <Arduino.h>
#include "esp_camera.h"
#include <WiFi.h>
#include <WebServer.h>
#include "img_converters.h"
#include <HTTPClient.h>
#include <math.h>
#include <Preferences.h>

// ----------------------------------------------------------
// CẤU HÌNH MẠNG NỘI BỘ
// ----------------------------------------------------------
const char* WIFI_SSID = "ESP32_INTERNAL_NET";
const char* WIFI_PASS = "12345678";

IPAddress CAM_STATIC_IP(192, 168, 4, 2);
IPAddress GATEWAY(192, 168, 4, 1);
IPAddress SUBNET(255, 255, 255, 0);
const char* DEV_IP = "192.168.4.1";

// Khai báo biến toàn cục để lưu cấu hình được đồng bộ từ DEV
String ext_ssid = "";
String ext_pass = "";

// ----------------------------------------------------------
// BIẾN THÔNG SỐ XỬ LÝ ẢNH
// ----------------------------------------------------------
Preferences prefs;

int p_led    = 1;
int p_thresh = 130;
int p_offset = 25;
int p_x      = 450;
int p_y      = 70;
int p_w      = 128;
int p_h      = 238;
int p_cw     = 224;
int p_ch     = 44;
int p_ox     = -220;
int p_oy     = 54;

#define NUM_DIGITS  8
#define DIGIT_W     28
#define DIGIT_H     44
#define DIGIT_BYTES (DIGIT_W * DIGIT_H)  // 1232 bytes mỗi ảnh

// ----------------------------------------------------------
// UART GIAO TIẾP VỚI ESP32-DEV
// ----------------------------------------------------------
HardwareSerial DevSerial(1);
bool isSetupMode = false;

void uartSend(const String& msg) {
    DevSerial.println(msg);
    Serial.println("[UART TX] " + msg);
}

// ----------------------------------------------------------
// CHÂN CAMERA
// ----------------------------------------------------------
#define LED_GPIO_NUM    4
#define PWDN_GPIO_NUM   32
#define RESET_GPIO_NUM  -1
#define XCLK_GPIO_NUM   0
#define SIOD_GPIO_NUM   26
#define SIOC_GPIO_NUM   27
#define Y9_GPIO_NUM     35
#define Y8_GPIO_NUM     34
#define Y7_GPIO_NUM     39
#define Y6_GPIO_NUM     36
#define Y5_GPIO_NUM     21
#define Y4_GPIO_NUM     19
#define Y3_GPIO_NUM     18
#define Y2_GPIO_NUM     5
#define VSYNC_GPIO_NUM  25
#define HREF_GPIO_NUM   23
#define PCLK_GPIO_NUM   22

struct Point { int16_t x; int16_t y; };

WebServer camServer(80);

// ----------------------------------------------------------
// GIAO DIỆN HTML - SETUP MODE
// ----------------------------------------------------------
const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>ESP32-CAM Setup</title>
    <link rel="preconnect" href="https://fonts.googleapis.com">
    <link href="https://fonts.googleapis.com/css2?family=Space+Mono:wght@400;700&family=Syne:wght@400;700;800&display=swap" rel="stylesheet">
    <style>
        :root {
            --bg: #0d0f14;
            --surface: #161923;
            --border: #252d3d;
            --accent: #00e5ff;
            --accent2: #ff6b35;
            --text: #e8eaf0;
            --muted: #5a6478;
            --success: #00c896;
            --warn: #ffb300;
            --danger: #ff4444;
        }
        * { box-sizing: border-box; margin: 0; padding: 0; }
        body { font-family: 'Space Mono', monospace; background: var(--bg); color: var(--text); min-height: 100vh; padding: 0; }
        .header { background: linear-gradient(135deg, #0d0f14 0%, #161923 100%); border-bottom: 1px solid var(--border); padding: 18px 24px; display: flex; align-items: center; gap: 16px; position: sticky; top: 0; z-index: 100; backdrop-filter: blur(10px); }
        .header-dot { width: 10px; height: 10px; border-radius: 50%; background: var(--accent); box-shadow: 0 0 12px var(--accent); animation: pulse 2s ease-in-out infinite; }
        @keyframes pulse { 0%,100%{opacity:1;transform:scale(1)} 50%{opacity:.5;transform:scale(1.4)} }
        .header h1 { font-family: 'Syne', sans-serif; font-size: 18px; font-weight: 800; letter-spacing: 2px; color: var(--accent); text-transform: uppercase; }
        .header span { color: var(--muted); font-size: 12px; margin-left: auto; }
        .main { display: grid; grid-template-columns: 320px 1fr; gap: 0; min-height: calc(100vh - 57px); }
        .sidebar { background: var(--surface); border-right: 1px solid var(--border); padding: 20px; overflow-y: auto; }
        .section-title { font-family: 'Syne', sans-serif; font-size: 11px; font-weight: 700; letter-spacing: 3px; color: var(--muted); text-transform: uppercase; margin-bottom: 14px; margin-top: 24px; padding-bottom: 6px; border-bottom: 1px solid var(--border); }
        .section-title:first-child { margin-top: 0; }
        .param-grid { display: grid; grid-template-columns: 1fr 1fr; gap: 10px; }
        .param-item { display: flex; flex-direction: column; gap: 5px; }
        .param-item label { font-size: 10px; letter-spacing: 1px; color: var(--muted); text-transform: uppercase; }
        .param-item input { background: var(--bg); border: 1px solid var(--border); color: var(--text); font-family: 'Space Mono', monospace; font-size: 14px; font-weight: 700; padding: 8px 10px; border-radius: 6px; width: 100%; transition: border-color 0.2s, box-shadow 0.2s; }
        .param-item input:focus { outline: none; border-color: var(--accent); box-shadow: 0 0 0 2px rgba(0,229,255,0.15); }
        .btn { display: flex; align-items: center; justify-content: center; gap: 8px; width: 100%; padding: 13px; font-family: 'Syne', sans-serif; font-size: 13px; font-weight: 700; letter-spacing: 2px; text-transform: uppercase; border: none; border-radius: 8px; cursor: pointer; transition: all 0.2s; margin-top: 10px; }
        .btn-capture { background: var(--accent); color: #000; }
        .btn-capture:hover { background: #33eaff; transform: translateY(-1px); box-shadow: 0 4px 20px rgba(0,229,255,0.3); }
        .btn-capture:active { transform: translateY(0); }
        .btn-save { background: transparent; color: var(--success); border: 1px solid var(--success); margin-top: 6px; }
        .btn-save:hover { background: rgba(0,200,150,0.1); }
        .btn:disabled { opacity: 0.4; cursor: not-allowed; transform: none; }
        .status-bar { margin-top: 14px; padding: 10px 12px; border-radius: 6px; font-size: 12px; min-height: 38px; display: flex; align-items: center; gap: 8px; background: rgba(255,255,255,0.03); border: 1px solid var(--border); transition: all 0.3s; }
        .status-bar.ok { border-color: var(--success); color: var(--success); }
        .status-bar.err { border-color: var(--danger); color: var(--danger); }
        .status-bar.loading { border-color: var(--accent); color: var(--accent); }
        .status-bar.warn { border-color: var(--warn); color: var(--warn); }
        .spinner { width: 12px; height: 12px; border: 2px solid transparent; border-top-color: currentColor; border-radius: 50%; animation: spin 0.7s linear infinite; flex-shrink: 0; }
        @keyframes spin { to { transform: rotate(360deg); } }
        .content { padding: 24px; overflow-y: auto; display: flex; flex-direction: column; gap: 24px; }
        .panel { background: var(--surface); border: 1px solid var(--border); border-radius: 12px; overflow: hidden; }
        .panel-header { padding: 14px 18px; border-bottom: 1px solid var(--border); display: flex; align-items: center; gap: 10px; }
        .panel-header h3 { font-family: 'Syne', sans-serif; font-size: 12px; font-weight: 700; letter-spacing: 2px; color: var(--muted); text-transform: uppercase; }
        .panel-badge { margin-left: auto; font-size: 10px; padding: 2px 8px; border-radius: 20px; background: rgba(0,229,255,0.1); color: var(--accent); border: 1px solid rgba(0,229,255,0.2); }
        .panel-body { padding: 18px; }
        .debug-img-wrap { background: #000; border-radius: 8px; overflow: auto; display: flex; align-items: center; justify-content: center; min-height: 80px; border: 1px solid var(--border); }
        #comboImg { display: none; image-rendering: pixelated; max-width: 100%; }
        .img-placeholder { color: var(--muted); font-size: 12px; display: flex; flex-direction: column; align-items: center; gap: 8px; padding: 30px; }
        .img-placeholder svg { opacity: 0.3; }
        .digits-grid { display: grid; grid-template-columns: repeat(8, 1fr); gap: 10px; }
        .digit-cell { display: flex; flex-direction: column; align-items: center; gap: 6px; }
        .digit-canvas-wrap { border: 1px solid var(--border); border-radius: 6px; overflow: hidden; background: #000; width: 100%; aspect-ratio: 28/44; display: flex; align-items: center; justify-content: center; }
        .digit-canvas-wrap canvas { image-rendering: pixelated; width: 100%; height: 100%; }
        .digit-label { font-size: 10px; color: var(--muted); letter-spacing: 1px; }
        .digit-placeholder { width: 100%; aspect-ratio: 28/44; background: rgba(255,255,255,0.02); border: 1px dashed var(--border); border-radius: 6px; }
        .empty-digits { padding: 30px; text-align: center; color: var(--muted); font-size: 12px; display: flex; flex-direction: column; align-items: center; gap: 10px; }
        @media (max-width: 900px) { .main { grid-template-columns: 1fr; } .sidebar { border-right: none; border-bottom: 1px solid var(--border); } .digits-grid { grid-template-columns: repeat(4, 1fr); } }
    </style>
</head>
<body>
    <div class="header">
        <div class="header-dot"></div>
        <h1>ESP32-CAM &nbsp;/&nbsp; Setup</h1>
        <span>RAW MODE — pixel-perfect</span>
    </div>

    <div class="main">
        <div class="sidebar">
            <div class="section-title">Lọc & Sáng</div>
            <div class="param-grid">
                <div class="param-item"><label>LED</label><input type="number" id="led" min="0" max="255" value="VAL_LED"></div>
                <div class="param-item"><label>Thresh</label><input type="number" id="thresh" value="VAL_THRESH"></div>
                <div class="param-item" style="grid-column:span 2"><label>Offset chữ</label><input type="number" id="offset" value="VAL_OFFSET"></div>
            </div>

            <div class="section-title">Toạ độ & Crop thô</div>
            <div class="param-grid">
                <div class="param-item"><label>X</label><input type="number" id="x" value="VAL_X"></div>
                <div class="param-item"><label>Y</label><input type="number" id="y" value="VAL_Y"></div>
                <div class="param-item"><label>W thô</label><input type="number" id="w" value="VAL_W"></div>
                <div class="param-item"><label>H thô</label><input type="number" id="h" value="VAL_H"></div>
            </div>

            <div class="section-title">Dải số (sau xoay)</div>
            <div class="param-grid">
                <div class="param-item"><label>Crop W</label><input type="number" id="cw" value="VAL_CW"></div>
                <div class="param-item"><label>Crop H</label><input type="number" id="ch" value="VAL_CH"></div>
                <div class="param-item"><label>Offset X</label><input type="number" id="ox" value="VAL_OX"></div>
                <div class="param-item"><label>Offset Y</label><input type="number" id="oy" value="VAL_OY"></div>
            </div>

            <button class="btn btn-capture" onclick="processImage()" id="btnCapture">
                <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5"><circle cx="12" cy="12" r="3"/><path d="M20 7h-3.5l-1.5-2h-6L7.5 7H4a2 2 0 00-2 2v10a2 2 0 002 2h16a2 2 0 002-2V9a2 2 0 00-2-2z"/></svg>
                Chụp & Xử lý
            </button>
            <button class="btn btn-save" onclick="saveConfig()" id="btnSave">
                <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M19 21H5a2 2 0 01-2-2V5a2 2 0 012-2h11l5 5v11a2 2 0 01-2 2z"/><polyline points="17,21 17,13 7,13 7,21"/><polyline points="7,3 7,8 15,8"/></svg>
                Lưu & Reset
            </button>

            <div class="status-bar" id="statusBar">
                <svg width="12" height="12" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><circle cx="12" cy="12" r="10"/><line x1="12" y1="8" x2="12" y2="12"/><line x1="12" y1="16" x2="12.01" y2="16"/></svg>
                Sẵn sàng
            </div>
        </div>

        <div class="content">
            <div class="panel">
                <div class="panel-header">
                    <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="var(--muted)" stroke-width="2"><rect x="3" y="3" width="18" height="18" rx="2"/><circle cx="8.5" cy="8.5" r="1.5"/><polyline points="21,15 16,10 5,21"/></svg>
                    <h3>Toàn cảnh Debug</h3>
                    <span class="panel-badge">Ảnh thô / nhị phân / dải</span>
                </div>
                <div class="panel-body">
                    <div class="debug-img-wrap" id="comboWrap">
                        <div class="img-placeholder" id="comboPlaceholder">
                            <svg width="32" height="32" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.5"><rect x="3" y="3" width="18" height="18" rx="2"/><circle cx="8.5" cy="8.5" r="1.5"/><polyline points="21,15 16,10 5,21"/></svg>
                            Bấm "Chụp & Xử lý" để xem ảnh debug
                        </div>
                        <img id="comboImg" src="" alt="debug">
                    </div>
                </div>
            </div>

            <div class="panel">
                <div class="panel-header">
                    <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="var(--muted)" stroke-width="2"><rect x="2" y="3" width="20" height="14" rx="2"/><line x1="8" y1="21" x2="16" y2="21"/><line x1="12" y1="17" x2="12" y2="21"/></svg>
                    <h3>8 Chữ số — Pixel-Perfect Raw</h3>
                    <span class="panel-badge">28 × 44 px · grayscale</span>
                </div>
                <div class="panel-body" id="digitsBody">
                    <div class="empty-digits" id="digitsEmpty">
                        <svg width="28" height="28" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.5" opacity="0.3"><path d="M21 16V8a2 2 0 00-1-1.73l-7-4a2 2 0 00-2 0l-7 4A2 2 0 003 8v8a2 2 0 001 1.73l7 4a2 2 0 002 0l7-4A2 2 0 0021 16z"/></svg>
                        Chưa có dữ liệu — bấm chụp để render raw bytes
                    </div>
                    <div class="digits-grid" id="digitsGrid" style="display:none"></div>
                </div>
            </div>
        </div>
    </div>

<script>
    function getQuery() {
        return `?x=${v('x')}&y=${v('y')}&w=${v('w')}&h=${v('h')}` +
               `&cw=${v('cw')}&ch=${v('ch')}&ox=${v('ox')}&oy=${v('oy')}` +
               `&led=${v('led')}&thresh=${v('thresh')}&offset=${v('offset')}&t=${Date.now()}`;
    }
    function v(id) { return document.getElementById(id).value; }

    function setStatus(msg, type='') {
        const el = document.getElementById('statusBar');
        el.className = 'status-bar' + (type ? ' ' + type : '');
        el.innerHTML = type === 'loading'
            ? `<div class="spinner"></div>${msg}`
            : `<svg width="12" height="12" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5"><circle cx="12" cy="12" r="10"/></svg>${msg}`;
    }

    async function processImage() {
        const btnCapture = document.getElementById('btnCapture');
        const btnSave    = document.getElementById('btnSave');
        btnCapture.disabled = true;
        btnSave.disabled = true;
        setStatus('Đang chụp & xử lý...', 'loading');

        const comboImg = document.getElementById('comboImg');
        const placeholder = document.getElementById('comboPlaceholder');
        comboImg.style.display = 'none';
        placeholder.style.display = 'flex';

        const digitsGrid  = document.getElementById('digitsGrid');
        const digitsEmpty = document.getElementById('digitsEmpty');
        digitsGrid.style.display = 'none';
        digitsEmpty.style.display = 'flex';
        digitsGrid.innerHTML = '';

        try {
            const comboRes = await fetch('/process' + getQuery());
            const comboStatus = comboRes.headers.get('X-App-Status') || '';
            const comboBlob = await comboRes.blob();

            const url = URL.createObjectURL(comboBlob);
            comboImg.onload = () => {
                placeholder.style.display = 'none';
                comboImg.style.display = 'block';
            };
            comboImg.src = url;

            const digitsRes = await fetch('/digits' + getQuery());
            if (!digitsRes.ok) throw new Error('HTTP ' + digitsRes.status);
            const rawBuf = await digitsRes.arrayBuffer();

            const bytes = new Uint8Array(rawBuf);
            const W = 28, H = 44, BYTES_PER = W * H;

            if (bytes.length < NUM_DIGITS * BYTES_PER) {
                setStatus(`Raw thiếu bytes: nhận ${bytes.length}, cần ${NUM_DIGITS * BYTES_PER}`, 'err');
                return;
            }

            digitsGrid.innerHTML = '';
            for (let i = 0; i < NUM_DIGITS; i++) {
                const cell = document.createElement('div');
                cell.className = 'digit-cell';
                const wrap = document.createElement('div');
                wrap.className = 'digit-canvas-wrap';
                const canvas = document.createElement('canvas');
                canvas.width = W; canvas.height = H;
                const ctx = canvas.getContext('2d');
                const imgData = ctx.createImageData(W, H);
                const offset = i * BYTES_PER;
                for (let p = 0; p < BYTES_PER; p++) {
                    const val = bytes[offset + p];
                    imgData.data[p*4 + 0] = val;
                    imgData.data[p*4 + 1] = val;
                    imgData.data[p*4 + 2] = val;
                    imgData.data[p*4 + 3] = 255;
                }
                ctx.putImageData(imgData, 0, 0);
                wrap.appendChild(canvas);
                const label = document.createElement('div');
                label.className = 'digit-label';
                label.textContent = 'D' + i;
                cell.appendChild(wrap);
                cell.appendChild(label);
                digitsGrid.appendChild(cell);
            }

            digitsEmpty.style.display = 'none';
            digitsGrid.style.display = 'grid';

            if (comboStatus === 'Success') {
                setStatus('Hoàn tất — raw bytes đã render pixel-perfect', 'ok');
            } else {
                setStatus('Lỗi xử lý: ' + decodeURIComponent(comboStatus), 'warn');
            }
        } catch(err) {
            setStatus('Lỗi: ' + err.message, 'err');
        } finally {
            btnCapture.disabled = false;
            btnSave.disabled = false;
        }
    }

    const NUM_DIGITS = 8;

    function saveConfig() {
        if (!confirm('Lưu thông số và khởi động lại ESP32-CAM?')) return;
        const btnSave = document.getElementById('btnSave');
        btnSave.disabled = true;
        setStatus('Đang lưu...', 'loading');
        fetch('/save' + getQuery())
            .then(() => {
                setStatus('Đã lưu! Đang khởi động lại...', 'ok');
            })
            .catch(() => {
                setStatus('Lỗi lưu cấu hình', 'err');
                btnSave.disabled = false;
            });
    }
</script>
</body>
</html>
)rawliteral";

// ----------------------------------------------------------
// TẢI THÔNG SỐ TỪ FLASH
// ----------------------------------------------------------
void loadConfig() {
    prefs.begin("cam_cfg", true);
    p_led    = prefs.getInt("led",    1);
    p_thresh = prefs.getInt("thresh", 130);
    p_offset = prefs.getInt("offset", 25);
    p_x      = prefs.getInt("x",     450);
    p_y      = prefs.getInt("y",     70);
    p_w      = prefs.getInt("w",     128);
    p_h      = prefs.getInt("h",     238);
    p_cw     = prefs.getInt("cw",    224);
    p_ch     = prefs.getInt("ch",    44);
    p_ox     = prefs.getInt("ox",    -220);
    p_oy     = prefs.getInt("oy",    54);

    // Tải WiFi đồng bộ
    ext_ssid = prefs.getString("ext_ssid", "");
    ext_pass = prefs.getString("ext_pass", "");

    prefs.end();
}

// ----------------------------------------------------------
// HÀM DÙNG CHUNG: Xử lý ảnh → tách 8 slot raw buf
// ----------------------------------------------------------
String processAndSplitRaw(
    int led_val, int threshold, int auto_offset,
    int crop_x, int crop_y, int crop_w_tho, int crop_h_tho,
    int CROP_W, int CROP_H, int OFFSET_X, int OFFSET_Y,
    uint8_t* out_bufs[NUM_DIGITS]
) {
    for (int i = 0; i < NUM_DIGITS; i++) out_bufs[i] = NULL;

    ledcWrite(0, led_val); delay(150);
    camera_fb_t* fb = NULL;
    for (int i = 0; i < 3; i++) {
        fb = esp_camera_fb_get();
        if (!fb) { ledcWrite(0, 0); return "Loi_Chup_Anh"; }
        if (i < 2) { esp_camera_fb_return(fb); delay(20); }
    }
    ledcWrite(0, 0);

    if (crop_x + crop_w_tho > (int)fb->width)  crop_w_tho = fb->width  - crop_x;
    if (crop_y + crop_h_tho > (int)fb->height) crop_h_tho = fb->height - crop_y;

    int dest_w = crop_h_tho, dest_h = crop_w_tho;
    uint8_t* gray_buf = (uint8_t*)ps_malloc(dest_w * dest_h);
    if (!gray_buf) { esp_camera_fb_return(fb); return "Loi_Malloc_Gray"; }

    for (int y_d = 0; y_d < dest_h; y_d++) {
        for (int x_d = 0; x_d < dest_w; x_d++) {
            int src_x = crop_x + y_d;
            int src_y = crop_y + (crop_h_tho - 1 - x_d);
            gray_buf[y_d * dest_w + x_d] = fb->buf[src_y * fb->width + src_x];
        }
    }
    esp_camera_fb_return(fb);

    uint8_t* bin_buf = (uint8_t*)ps_malloc(dest_w * dest_h);
    if (!bin_buf) { free(gray_buf); return "Loi_Malloc_Bin"; }

    int white_pixels = 0;
    for (int i = 0; i < dest_w * dest_h; i++) {
        if (gray_buf[i] >= threshold) {
            bin_buf[i] = 255;
            white_pixels++;
        } else {
            bin_buf[i] = 0;
        }
    }

    int min_white_required = (dest_w * dest_h) * 0.03;
    if (white_pixels < min_white_required) {
        free(gray_buf);
        free(bin_buf);
        return "Khong_Co_The";
    }

    int MIN_RUN = 15, start_x = -1, start_y = -1;
    for (int x = dest_w - 1; x >= 0; x--) {
        int count = 0, best_len = 0, best_y = -1;
        for (int y = dest_h - 1; y >= 0; y--) {
            if (bin_buf[y * dest_w + x] == 0) { count++; if (count > best_len) { best_len = count; best_y = y; } }
            else count = 0;
        }
        if (best_len > MIN_RUN) { start_x = x; start_y = best_y + (best_len / 2); break; }
    }
    if (start_x < 0) { free(gray_buf); free(bin_buf); return "Khong_Thay_Barcode"; }

    int MAX_STACK = 4000;
    Point* stack = (Point*)ps_malloc(MAX_STACK * sizeof(Point));
    if (!stack) { free(gray_buf); free(bin_buf); return "Loi_Malloc_Stack"; }

    uint8_t* dfs_buf = (uint8_t*)ps_malloc(dest_w * dest_h);
    if (!dfs_buf) { free(gray_buf); free(bin_buf); free(stack); return "Loi_Malloc_DFS"; }
    memcpy(dfs_buf, bin_buf, dest_w * dest_h);
    free(bin_buf);

    int stack_ptr = 0;
    stack[stack_ptr++] = {(int16_t)start_x, (int16_t)start_y};
    long max_sum = -1e9, min_diff = 1e9;
    Point br = {0,0}, bl = {0,0};
    bool stack_overflow = false;

    while (stack_ptr > 0) {
        if (stack_ptr >= MAX_STACK) { stack_overflow = true; break; }
        Point p = stack[--stack_ptr];
        int idx = p.y * dest_w + p.x;
        if (dfs_buf[idx] != 0) continue;
        dfs_buf[idx] = 255;

        long s = p.x + p.y, d = p.x - p.y;
        if (s > max_sum) { max_sum = s; br = p; }
        if (d < min_diff) { min_diff = d; bl = p; }

        if (p.x+1 < dest_w  && dfs_buf[p.y*dest_w+(p.x+1)] == 0) stack[stack_ptr++] = {(int16_t)(p.x+1), p.y};
        if (p.x-1 >= 0      && dfs_buf[p.y*dest_w+(p.x-1)] == 0) stack[stack_ptr++] = {(int16_t)(p.x-1), p.y};
        if (p.y+1 < dest_h  && dfs_buf[(p.y+1)*dest_w+p.x] == 0) stack[stack_ptr++] = {p.x, (int16_t)(p.y+1)};
        if (p.y-1 >= 0      && dfs_buf[(p.y-1)*dest_w+p.x] == 0) stack[stack_ptr++] = {p.x, (int16_t)(p.y-1)};
    }
    free(stack); free(dfs_buf);
    if (stack_overflow) { free(gray_buf); return "Tran_RAM_DFS"; }

    float vx = br.x - bl.x, vy = br.y - bl.y;
    float rad = atan2(vy, vx), cos_a = cos(rad), sin_a = sin(rad);

    uint8_t* strip_buf = (uint8_t*)ps_malloc(CROP_W * CROP_H);
    if (!strip_buf) { free(gray_buf); return "Loi_Malloc_Strip"; }

    float anchor_x = br.x + OFFSET_X * cos_a + (CROP_H + OFFSET_Y) * sin_a;
    float anchor_y = br.y + OFFSET_X * sin_a - (CROP_H + OFFSET_Y) * cos_a;

    for (int y_c = 0; y_c < CROP_H; y_c++) {
        for (int x_c = 0; x_c < CROP_W; x_c++) {
            int sx = (int)(anchor_x + x_c * cos_a - y_c * sin_a);
            int sy = (int)(anchor_y + x_c * sin_a + y_c * cos_a);
            strip_buf[y_c * CROP_W + x_c] = (sx >= 0 && sx < dest_w && sy >= 0 && sy < dest_h)
                                            ? gray_buf[sy * dest_w + sx] : 255;
        }
    }
    free(gray_buf);

    int* col_bg    = (int*)ps_malloc(CROP_W * sizeof(int));
    int* smooth_bg = (int*)ps_malloc(CROP_W * sizeof(int));
    if (!col_bg || !smooth_bg) { free(strip_buf); if(col_bg) free(col_bg); if(smooth_bg) free(smooth_bg); return "Loi_Malloc_BG"; }

    for (int x = 0; x < CROP_W; x++) {
        long sum = 0;
        for (int y = 0; y < CROP_H; y++) sum += strip_buf[y * CROP_W + x];
        col_bg[x] = sum / CROP_H;
    }
    for (int x = 0; x < CROP_W; x++) {
        int w_sum = 0, w_count = 0;
        for (int k = -5; k <= 5; k++) {
            if (x+k >= 0 && x+k < CROP_W) { w_sum += col_bg[x+k]; w_count++; }
        }
        smooth_bg[x] = w_sum / w_count;
    }
    free(col_bg);

    float slot_width = (float)CROP_W / 8.0f;
    for (int i = 0; i < NUM_DIGITS; i++) {
        int slot_start = (int)round(i * slot_width);
        int slot_end   = (int)round((i+1) * slot_width) - 1;
        if (slot_end >= CROP_W) slot_end = CROP_W - 1;

        int min_x = slot_end, max_x = slot_start, count_p = 0;
        long bg_sum = 0; int bg_count = 0;

        for (int x = slot_start; x <= slot_end; x++) {
            for (int y = 0; y < CROP_H; y++) {
                uint8_t pv = strip_buf[y * CROP_W + x];
                if (pv < (smooth_bg[x] - auto_offset)) {
                    if (x < min_x) min_x = x;
                    if (x > max_x) max_x = x;
                    count_p++;
                } else { bg_sum += pv; bg_count++; }
            }
        }

        int center_x = (count_p > 5) ? ((min_x + max_x) / 2) : ((slot_start + slot_end) / 2);
        int center_y = CROP_H / 2;
        uint8_t local_bg = (bg_count > 0) ? (uint8_t)(bg_sum / bg_count) : 200;

        int bx = center_x - DIGIT_W / 2;
        int by = center_y - DIGIT_H / 2;

        uint8_t* slot_buf = (uint8_t*)ps_malloc(DIGIT_BYTES);
        if (!slot_buf) continue;

        for (int dy = 0; dy < DIGIT_H; dy++) {
            for (int dx = 0; dx < DIGIT_W; dx++) {
                int sx = bx + dx, sy = by + dy;
                slot_buf[dy * DIGIT_W + dx] = (sx >= 0 && sx < CROP_W && sy >= 0 && sy < CROP_H)
                                             ? strip_buf[sy * CROP_W + sx]
                                             : local_bg;
            }
        }
        out_bufs[i] = slot_buf;
    }

    free(smooth_bg);
    free(strip_buf);
    return "OK";
}

// ----------------------------------------------------------
// GỬI RAW BYTES VỀ ESP32-DEV (HTTP POST octet-stream)
// ----------------------------------------------------------
void sendRawToDev(uint8_t* raw_bufs[NUM_DIGITS]) {
    for (int i = 0; i < NUM_DIGITS; i++) {
        if (!raw_bufs[i]) continue;
        char url[80];
        sprintf(url, "http://%s/upload?index=%d&w=%d&h=%d", DEV_IP, i, DIGIT_W, DIGIT_H);
        HTTPClient http;
        http.begin(url);
        http.addHeader("Content-Type", "application/octet-stream");
        http.setTimeout(10000);
        http.POST(raw_bufs[i], DIGIT_BYTES);
        http.end();
        free(raw_bufs[i]);
        raw_bufs[i] = NULL;
        delay(50);
    }
}

// ----------------------------------------------------------
// SETUP MAIN
// ----------------------------------------------------------
void setup() {
    Serial.begin(115200);

    ledcSetup(0, 5000, 8);
    ledcAttachPin(LED_GPIO_NUM, 0);
    ledcWrite(0, 0);

    DevSerial.begin(115200, SERIAL_8N1, 14, 15);
    loadConfig();

    camera_config_t config;
    config.ledc_channel = LEDC_CHANNEL_0; config.ledc_timer = LEDC_TIMER_0;
    config.pin_d0 = Y2_GPIO_NUM; config.pin_d1 = Y3_GPIO_NUM;
    config.pin_d2 = Y4_GPIO_NUM; config.pin_d3 = Y5_GPIO_NUM;
    config.pin_d4 = Y6_GPIO_NUM; config.pin_d5 = Y7_GPIO_NUM;
    config.pin_d6 = Y8_GPIO_NUM; config.pin_d7 = Y9_GPIO_NUM;
    config.pin_xclk = XCLK_GPIO_NUM; config.pin_pclk = PCLK_GPIO_NUM;
    config.pin_vsync = VSYNC_GPIO_NUM; config.pin_href = HREF_GPIO_NUM;
    config.pin_sscb_sda = SIOD_GPIO_NUM; config.pin_sscb_scl = SIOC_GPIO_NUM;
    config.pin_pwdn = PWDN_GPIO_NUM; config.pin_reset = RESET_GPIO_NUM;
    config.xclk_freq_hz = 20000000;
    config.pixel_format = PIXFORMAT_GRAYSCALE;
    config.frame_size   = FRAMESIZE_VGA;
    config.fb_count     = 1;

    if (esp_camera_init(&config) != ESP_OK) {
        Serial.println("FATAL: Camera init that bai!");
        while (1) delay(1000);
    }

    WiFi.config(CAM_STATIC_IP, GATEWAY, SUBNET);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    int retries = 0;
    while (WiFi.status() != WL_CONNECTED && retries < 30) {
        delay(500);
        retries++;
    }
    if (WiFi.status() != WL_CONNECTED) {
        ESP.restart();
    }

    uartSend("CAM_READY");

    camServer.on("/capture", HTTP_GET, []() {
        if (isSetupMode) { camServer.send(503, "text/plain", "BUSY_SETUP"); return; }
        uartSend("ACK_PROCESSING");

        uint8_t* raw_bufs[NUM_DIGITS];
        String result = processAndSplitRaw(
            p_led, p_thresh, p_offset,
            p_x, p_y, p_w, p_h,
            p_cw, p_ch, p_ox, p_oy,
            raw_bufs
        );

        if (result == "OK") {
            camServer.send(200, "text/plain", "PROCESSING");
            sendRawToDev(raw_bufs);
        } else {
            uartSend("ERR:" + result);
            camServer.send(500, "text/plain", result); // DEV sẽ đọc chuỗi result này
            for (int i = 0; i < NUM_DIGITS; i++) if (raw_bufs[i]) { free(raw_bufs[i]); raw_bufs[i]=NULL; }
        }
    });

    camServer.on("/process", HTTP_GET, []() {
        if (!isSetupMode) { camServer.send(403, "text/plain", "NOT_IN_SETUP_MODE"); return; }
        int led_val    = camServer.hasArg("led")    ? camServer.arg("led").toInt()    : 1;
        int threshold  = camServer.hasArg("thresh") ? camServer.arg("thresh").toInt() : 130;
        int auto_offset= camServer.hasArg("offset") ? camServer.arg("offset").toInt() : 25;
        int crop_x     = camServer.hasArg("x")  ? camServer.arg("x").toInt()  : 450;
        int crop_y     = camServer.hasArg("y")  ? camServer.arg("y").toInt()  : 70;
        int crop_w_tho = camServer.hasArg("w")  ? camServer.arg("w").toInt()  : 128;
        int crop_h_tho = camServer.hasArg("h")  ? camServer.arg("h").toInt()  : 238;
        int CROP_W     = camServer.hasArg("cw") ? camServer.arg("cw").toInt() : 224;
        int CROP_H     = camServer.hasArg("ch") ? camServer.arg("ch").toInt() : 44;
        int OFFSET_X   = camServer.hasArg("ox") ? camServer.arg("ox").toInt() : -220;
        int OFFSET_Y   = camServer.hasArg("oy") ? camServer.arg("oy").toInt() : 54;

        ledcWrite(0, led_val); delay(150);
        camera_fb_t* fb = esp_camera_fb_get();
        if (fb) esp_camera_fb_return(fb); delay(20);
        fb = esp_camera_fb_get();
        ledcWrite(0, 0);
        if (!fb) { camServer.send(500, "text/plain", "Loi chup"); return; }

        if (crop_x + crop_w_tho > (int)fb->width)  crop_w_tho = fb->width  - crop_x;
        if (crop_y + crop_h_tho > (int)fb->height) crop_h_tho = fb->height - crop_y;

        int dest_w = crop_h_tho, dest_h = crop_w_tho;
        uint8_t* gray_buf = (uint8_t*)ps_malloc(dest_w * dest_h);
        uint8_t* bin_buf  = (uint8_t*)ps_malloc(dest_w * dest_h);

        int white_pixels = 0;
        for (int y_d = 0; y_d < dest_h; y_d++) {
            for (int x_d = 0; x_d < dest_w; x_d++) {
                int src_x = crop_x + y_d;
                int src_y = crop_y + (crop_h_tho - 1 - x_d);
                uint8_t val = fb->buf[src_y * fb->width + src_x];
                gray_buf[y_d * dest_w + x_d] = val;
                if (val >= threshold) {
                    bin_buf[y_d  * dest_w + x_d] = 255;
                    white_pixels++;
                } else {
                    bin_buf[y_d  * dest_w + x_d] = 0;
                }
            }
        }
        esp_camera_fb_return(fb);

        String error_msg = "OK"; bool is_success = false;
        uint8_t* strip_buf = NULL;

        int min_white_required = (dest_w * dest_h) * 0.03;
        if (white_pixels < min_white_required) {
            error_msg = "Khong_Co_The";
        } else {
            int MIN_RUN = 15, start_x = -1, start_y = -1;
            for (int x = dest_w - 1; x >= 0; x--) {
                int count = 0, best_len = 0, best_y = -1;
                for (int y = dest_h - 1; y >= 0; y--) {
                    if (bin_buf[y*dest_w+x]==0) { count++; if(count>best_len){best_len=count;best_y=y;} } else count=0;
                }
                if (best_len > MIN_RUN) { start_x = x; start_y = best_y + best_len/2; break; }
            }

            if (start_x < 0) { error_msg = "Khong_Thay_Barcode"; }
            else {
                uint8_t* dfs_buf = (uint8_t*)ps_malloc(dest_w * dest_h);
                memcpy(dfs_buf, bin_buf, dest_w * dest_h);
                int MAX_STACK = 4000;
                Point* stack = (Point*)ps_malloc(MAX_STACK * sizeof(Point));
                int sp = 0; stack[sp++] = {(int16_t)start_x, (int16_t)start_y};
                long max_sum = -(long)1e9, min_diff = (long)1e9;
                Point br={0,0}, bl={0,0}; bool overflow = false;
                while (sp > 0) {
                    if (sp >= MAX_STACK) { overflow = true; break; }
                    Point p = stack[--sp]; int idx = p.y*dest_w+p.x;
                    if (dfs_buf[idx]!=0) continue; dfs_buf[idx]=255;
                    long s=p.x+p.y, d=p.x-p.y;
                    if(s>max_sum){max_sum=s;br=p;} if(d<min_diff){min_diff=d;bl=p;}
                    if(p.x+1<dest_w  && dfs_buf[p.y*dest_w+(p.x+1)]==0) stack[sp++]={(int16_t)(p.x+1),p.y};
                    if(p.x-1>=0      && dfs_buf[p.y*dest_w+(p.x-1)]==0) stack[sp++]={(int16_t)(p.x-1),p.y};
                    if(p.y+1<dest_h  && dfs_buf[(p.y+1)*dest_w+p.x]==0) stack[sp++]={p.x,(int16_t)(p.y+1)};
                    if(p.y-1>=0      && dfs_buf[(p.y-1)*dest_w+p.x]==0) stack[sp++]={p.x,(int16_t)(p.y-1)};
                }
                free(stack); free(dfs_buf);
                if (overflow) { error_msg = "Tran_RAM_DFS"; }
                else {
                    float vx=br.x-bl.x, vy=br.y-bl.y;
                    float rad=atan2(vy,vx), cos_a=cos(rad), sin_a=sin(rad);
                    strip_buf = (uint8_t*)ps_malloc(CROP_W * CROP_H);
                    float ax = br.x + OFFSET_X*cos_a + (CROP_H+OFFSET_Y)*sin_a;
                    float ay = br.y + OFFSET_X*sin_a - (CROP_H+OFFSET_Y)*cos_a;
                    for (int y_c=0; y_c<CROP_H; y_c++) {
                        for (int x_c=0; x_c<CROP_W; x_c++) {
                            int sx=(int)(ax+x_c*cos_a-y_c*sin_a), sy=(int)(ay+x_c*sin_a+y_c*cos_a);
                            strip_buf[y_c*CROP_W+x_c] = (sx>=0&&sx<dest_w&&sy>=0&&sy<dest_h) ? gray_buf[sy*dest_w+sx] : 255;
                        }
                    }
                    is_success = true; error_msg = "Success";
                }
            }
        }

        int gap = 5;
        int combo_w = dest_w + gap + dest_w + gap + CROP_W;
        int combo_h = (dest_h > CROP_H) ? dest_h : CROP_H;
        uint8_t* combo_buf = (uint8_t*)ps_malloc(combo_w * combo_h);
        memset(combo_buf, 100, combo_w * combo_h);

        for (int y=0; y<dest_h; y++) memcpy(combo_buf + y*combo_w,              gray_buf + y*dest_w, dest_w);
        for (int y=0; y<dest_h; y++) memcpy(combo_buf + y*combo_w + dest_w+gap,  bin_buf  + y*dest_w, dest_w);
        if (strip_buf) {
            for (int y=0; y<CROP_H; y++) memcpy(combo_buf + y*combo_w + (dest_w+gap)*2, strip_buf + y*CROP_W, CROP_W);
            free(strip_buf);
        }

        free(gray_buf); free(bin_buf);

        uint8_t* jpeg_buf = NULL; size_t jpeg_len = 0;
        bool ok = fmt2jpg(combo_buf, combo_w*combo_h, combo_w, combo_h, PIXFORMAT_GRAYSCALE, 12, &jpeg_buf, &jpeg_len);
        free(combo_buf);

        if (ok) {
            String headers = "HTTP/1.1 200 OK\r\nContent-Type: image/jpeg\r\n";
            headers += "X-App-Status: " + error_msg + "\r\n";
            headers += "Access-Control-Expose-Headers: X-App-Status\r\n";
            headers += "Content-Length: " + String(jpeg_len) + "\r\n\r\n";
            camServer.client().write((const uint8_t*)headers.c_str(), headers.length());
            camServer.client().write(jpeg_buf, jpeg_len);
            free(jpeg_buf);
        } else {
            camServer.send(500, "text/plain", "Loi nen JPEG combo!");
        }
    });

    camServer.on("/digits", HTTP_GET, []() {
        if (!isSetupMode) { camServer.send(403, "text/plain", "NOT_IN_SETUP_MODE"); return; }
        int led_val    = camServer.hasArg("led")    ? camServer.arg("led").toInt()    : 1;
        int threshold  = camServer.hasArg("thresh") ? camServer.arg("thresh").toInt() : 130;
        int auto_offset= camServer.hasArg("offset") ? camServer.arg("offset").toInt() : 25;
        int crop_x     = camServer.hasArg("x")  ? camServer.arg("x").toInt()  : 450;
        int crop_y     = camServer.hasArg("y")  ? camServer.arg("y").toInt()  : 70;
        int crop_w_tho = camServer.hasArg("w")  ? camServer.arg("w").toInt()  : 128;
        int crop_h_tho = camServer.hasArg("h")  ? camServer.arg("h").toInt()  : 238;
        int CROP_W     = camServer.hasArg("cw") ? camServer.arg("cw").toInt() : 224;
        int CROP_H     = camServer.hasArg("ch") ? camServer.arg("ch").toInt() : 44;
        int OFFSET_X   = camServer.hasArg("ox") ? camServer.arg("ox").toInt() : -220;
        int OFFSET_Y   = camServer.hasArg("oy") ? camServer.arg("oy").toInt() : 54;

        uint8_t* raw_bufs[NUM_DIGITS];
        String result = processAndSplitRaw(
            led_val, threshold, auto_offset,
            crop_x, crop_y, crop_w_tho, crop_h_tho,
            CROP_W, CROP_H, OFFSET_X, OFFSET_Y,
            raw_bufs
        );

        if (result != "OK") {
            for (int i = 0; i < NUM_DIGITS; i++) if (raw_bufs[i]) { free(raw_bufs[i]); raw_bufs[i]=NULL; }
            camServer.send(500, "text/plain", result);
            return;
        }

        const size_t TOTAL = NUM_DIGITS * DIGIT_BYTES;
        uint8_t* blob = (uint8_t*)ps_malloc(TOTAL);
        if (!blob) {
            for (int i = 0; i < NUM_DIGITS; i++) if (raw_bufs[i]) { free(raw_bufs[i]); raw_bufs[i]=NULL; }
            camServer.send(500, "text/plain", "Loi_Malloc_Blob");
            return;
        }

        for (int i = 0; i < NUM_DIGITS; i++) {
            if (raw_bufs[i]) {
                memcpy(blob + i * DIGIT_BYTES, raw_bufs[i], DIGIT_BYTES);
                free(raw_bufs[i]); raw_bufs[i] = NULL;
            } else {
                memset(blob + i * DIGIT_BYTES, 128, DIGIT_BYTES);
            }
        }

        String headers = "HTTP/1.1 200 OK\r\n";
        headers += "Content-Type: application/octet-stream\r\n";
        headers += "Access-Control-Allow-Origin: *\r\n";
        headers += "Content-Length: " + String(TOTAL) + "\r\n\r\n";
        camServer.client().write((const uint8_t*)headers.c_str(), headers.length());
        camServer.client().write(blob, TOTAL);
        free(blob);
    });

    camServer.on("/", HTTP_GET, []() {
        if (!isSetupMode) { camServer.send(200, "text/plain", "RUN MODE - Bam nut SETUP de vao setup mode."); return; }
        String html = String(INDEX_HTML);
        html.replace("VAL_LED",    String(p_led));
        html.replace("VAL_THRESH", String(p_thresh));
        html.replace("VAL_OFFSET", String(p_offset));
        html.replace("VAL_X",  String(p_x));  html.replace("VAL_Y",  String(p_y));
        html.replace("VAL_W",  String(p_w));  html.replace("VAL_H",  String(p_h));
        html.replace("VAL_CW", String(p_cw)); html.replace("VAL_CH", String(p_ch));
        html.replace("VAL_OX", String(p_ox)); html.replace("VAL_OY", String(p_oy));
        camServer.send(200, "text/html", html);
    });

    camServer.on("/save", HTTP_GET, []() {
        if (!isSetupMode) { camServer.send(403, "text/plain", "NOT_IN_SETUP_MODE"); return; }
        prefs.begin("cam_cfg", false);
        if (camServer.hasArg("led"))    prefs.putInt("led",    camServer.arg("led").toInt());
        if (camServer.hasArg("thresh")) prefs.putInt("thresh", camServer.arg("thresh").toInt());
        if (camServer.hasArg("offset")) prefs.putInt("offset", camServer.arg("offset").toInt());
        if (camServer.hasArg("x"))  prefs.putInt("x",  camServer.arg("x").toInt());
        if (camServer.hasArg("y"))  prefs.putInt("y",  camServer.arg("y").toInt());
        if (camServer.hasArg("w"))  prefs.putInt("w",  camServer.arg("w").toInt());
        if (camServer.hasArg("h"))  prefs.putInt("h",  camServer.arg("h").toInt());
        if (camServer.hasArg("cw")) prefs.putInt("cw", camServer.arg("cw").toInt());
        if (camServer.hasArg("ch")) prefs.putInt("ch", camServer.arg("ch").toInt());
        if (camServer.hasArg("ox")) prefs.putInt("ox", camServer.arg("ox").toInt());
        if (camServer.hasArg("oy")) prefs.putInt("oy", camServer.arg("oy").toInt());
        prefs.end();
        camServer.send(200, "text/plain", "SAVED");
        delay(1000); ESP.restart();
    });

    camServer.on("/ping", HTTP_GET, []() { camServer.send(200, "text/plain", "PONG"); });

    camServer.begin();
    Serial.println("Server san sang.");
}

// ----------------------------------------------------------
// VÒNG LẶP CHÍNH & XỬ LÝ LỆNH UART MỚI
// ----------------------------------------------------------
void loop() {
    camServer.handleClient();

    if (DevSerial.available()) {
        String cmd = DevSerial.readStringUntil('\n');
        cmd.trim();
        Serial.println("[UART RX] " + cmd);

        // 1. NHẬN LỆNH ĐỒNG BỘ WIFI TỪ DEV (SYNC_WIFI:SSID|PASS)
        if (cmd.startsWith("SYNC_WIFI:")) {
            String data = cmd.substring(10);
            int pipeIdx = data.indexOf('|');

            if (pipeIdx > 0) {
                ext_ssid = data.substring(0, pipeIdx);
                ext_pass = data.substring(pipeIdx + 1);

                prefs.begin("cam_cfg", false);
                prefs.putString("ext_ssid", ext_ssid);
                prefs.putString("ext_pass", ext_pass);
                prefs.end();

                Serial.println("-> Da dong bo va luu WiFi tu DEV: " + ext_ssid);
            }
        }

        // 2. NHẬN LỆNH VÀO CHẾ ĐỘ SETUP
        else if (cmd == "CMD_SETUP" && !isSetupMode) {
            isSetupMode = true;
            Serial.println("====== CHUYEN SANG CHE DO SETUP ======");

            WiFi.disconnect(); delay(500);
            WiFi.config(IPAddress(0,0,0,0), IPAddress(0,0,0,0), IPAddress(255,255,255,0));

            if (ext_ssid == "") {
                uartSend("SETUP_ERR");
                Serial.println("Loi: Chua co thong tin WiFi duoc dong bo!");
            } else {
                Serial.println("Dang ket noi mang nha: " + ext_ssid);
                WiFi.begin(ext_ssid.c_str(), ext_pass.c_str());

                int retries = 0;
                while (WiFi.status() != WL_CONNECTED && retries < 40) {
                    delay(500);
                    retries++;
                }

                if (WiFi.status() == WL_CONNECTED) {
                    String newIP = WiFi.localIP().toString();
                    uartSend("SETUP_IP:" + newIP);
                    Serial.println("IP da vao mang nha: " + newIP);
                } else {
                    uartSend("SETUP_ERR");
                    Serial.println("Loi: Khong ket noi duoc mang nha!");
                }
            }
        }

        // 3. FIX CRITICAL: NHẬN LỆNH THOÁT CHẾ ĐỘ SETUP TỪ DEV
        else if (cmd == "CMD_EXIT_SETUP" && isSetupMode) {
            isSetupMode = false;
            Serial.println("====== THOAT SETUP - VE MANG NOI BO ======");
            
            WiFi.disconnect(); 
            delay(500);
            
            // Cấu hình lại IP tĩnh để làm client cho ESP32-DEV
            WiFi.config(CAM_STATIC_IP, GATEWAY, SUBNET);
            WiFi.begin(WIFI_SSID, WIFI_PASS);
            
            int retries = 0;
            while (WiFi.status() != WL_CONNECTED && retries < 30) {
                delay(500);
                retries++;
            }
            if (WiFi.status() == WL_CONNECTED) {
                uartSend("CAM_READY"); // Báo cáo cho DEV biết CAM đã về đúng mạng
                Serial.println("Da ket noi lai mang noi bo thanh cong!");
            } else {
                Serial.println("Loi ket noi mang noi bo, ESP dang khoi dong lai...");
                ESP.restart(); // Nếu vì lý do nào đó không thể kết nối, reset lại cho an toàn
            }
        }
    }
}