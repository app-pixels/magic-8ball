/*
 * app_8ball.cpp — Magic 8-Ball
 *
 * Portrait 368×448, direct gfx (no canvas).
 * Shows an "8" ball at rest. Shake or press BOOT to get an answer.
 * 20 answers (mix of positive, neutral, negative) in German or English,
 * selected by `BALL_LANGUAGE = de|en` in /setup/setup.txt (default: de).
 *
 * Controls:
 *   Shake / BOOT – ask a question
 *   PWR          – nothing (pass-through)
 */

#include "app_8ball.h"
#include "app_common.h"
#include <Arduino.h>
#include <Wire.h>
#include <SD_MMC.h>
#include <FS.h>
#include <math.h>
#include "canvas/Arduino_Canvas.h"
#include "pin_config.h"
#include "HWCDC.h"
#include <Adafruit_XCA9554.h>
#include "SensorQMI8658.hpp"

extern USBCDC USBSerial;
extern Arduino_Canvas *g_canvas;

#define BOOT_BTN       0
#define PWR_POLL_MS    50
#define SHAKE_THRESH   2.0f
#define SHAKE_COOLDOWN 2000   // ms

static Arduino_Canvas  *canvas   = nullptr;
static Adafruit_XCA9554 expander;
static SensorQMI8658    qmi;
static bool             s_bootWas   = false;
static uint32_t         s_lastPwr   = 0;
static uint32_t         s_lastShake = 0;
static bool             s_emaInit   = false;
static float            s_emaAx = 0, s_emaAy = 0, s_emaAz = 1;
static bool             s_showAns   = false;
static uint32_t         s_ansStart  = 0;
static int              s_ansIdx    = 0;
static bool             s_rolling   = false;
static uint32_t         s_rollStart = 0;
static uint8_t          s_rollFrame = 0;
#define ROLL_DURATION   1500   // ms of rolling animation

static const char *ANSWERS_DE[] = {
    "Ja, definitiv!",
    "Es ist beschlossen.",
    "Ohne Zweifel.",
    "Ganz sicher!",
    "Auf jeden Fall.",
    "Sieht sehr gut aus.",
    "Ja.",
    "Die Zeichen stehen gut.",
    "Aller Voraussicht nach.",
    "Sieht gut aus.",
    "Frag nochmal.",
    "Bitte später fragen.",
    "Besser nicht sagen.",
    "Im Moment ungewiss.",
    "Konzentrier dich.",
    "Glaub nicht daran.",
    "Nein.",
    "Quellen sagen Nein.",
    "Sieht nicht gut aus.",
    "Sehr zweifelhaft.",
};
static const char *ANSWERS_EN[] = {
    "Yes, definitely!",
    "It is decided.",
    "Without a doubt.",
    "Absolutely!",
    "By all means.",
    "Outlook very good.",
    "Yes.",
    "Signs point to yes.",
    "Most likely.",
    "Looks good.",
    "Ask again.",
    "Ask later.",
    "Better not tell now.",
    "Reply hazy.",
    "Concentrate and ask.",
    "Don't count on it.",
    "No.",
    "Sources say no.",
    "Outlook not so good.",
    "Very doubtful.",
};
static const int N_ANSWERS = 20;
static bool s_useEN = false;
static const char *answer(int idx) {
    return s_useEN ? ANSWERS_EN[idx] : ANSWERS_DE[idx];
}

// ── Colours by answer group ────────────────────────────────────────────────
static uint16_t answerColor(int idx) {
    if (idx < 10) return 0x07E0;   // green — positive
    if (idx < 14) return 0xFFE0;   // yellow — neutral
    return 0xF800;                  // red — negative
}

static void drawIdle() {
    canvas->fillScreen(0x0000);

    int16_t cx = LCD_WIDTH / 2;
    int16_t cy = 200;
    int16_t r  = 120;

    // Outer glow rings (AA against background)
    drawCircleAA(canvas, cx, cy, r + 4, 0x0841, 0x0000);
    drawCircleAA(canvas, cx, cy, r + 3, 0x0841, 0x0000);

    // Ball body — dark gradient effect (two-tone)
    fillCircleAA(canvas, cx, cy, r, 0x1082, 0x0000);
    fillCircleAA(canvas, cx, cy - 8, r - 10, 0x18C3, 0x1082);   // slight top highlight
    drawCircleAA(canvas, cx, cy, r, 0x2945, 0x1082);

    // Inner white circle
    fillCircleAA(canvas, cx, cy - 12, 48, 0xFFFF, 0x18C3);
    fillCircleAA(canvas, cx, cy - 12, 46, 0xEF7D, 0xFFFF);   // slight off-white for depth

    // "8" digit
    canvas->setTextSize(6, 7, 2);
    canvas->setTextColor(0x18C3);
    canvas->setCursor(cx - 19, cy - 36);
    canvas->print("8");

    // Hint — convert UTF-8 to CP437 for accurate width calc
    canvas->setTextSize(2);
    canvas->setTextColor(0x2945);
    char hcp[16];
    utf8_to_cp437(hcp, sizeof(hcp), s_useEN ? "Shake!" : "Schütteln!");
    canvas->setCursor((LCD_WIDTH - (int16_t)(strlen(hcp) * 12)) / 2, 370);
    canvas->print(hcp);

    draw_battery_g(canvas, LCD_WIDTH, LCD_HEIGHT);
    draw_watermark_g(canvas, LCD_WIDTH, LCD_HEIGHT);
    draw_pill_label(canvas, 0, 0, "shake");
    canvas->flush();
}

static void drawAnswer(int idx) {
    uint16_t col = answerColor(idx);
    uint16_t dimCol = (col >> 2) & 0x39E7;   // quarter-brightness for glow
    canvas->fillScreen(0x0000);

    int16_t cx = LCD_WIDTH / 2;
    int16_t cy = 210;

    // Glow ring (annulus rendered as two filled circles)
    fillCircleAA(canvas, cx, cy, 130, dimCol, 0x0000);
    fillCircleAA(canvas, cx, cy, 124, 0x0000, dimCol);

    // Inner circle
    fillCircleAA(canvas, cx, cy, 120, 0x1082, 0x0000);
    drawCircleAA(canvas, cx, cy, 120, col, 0x1082);

    // Answer text
    const char *ans = answer(idx);
    canvas->setTextSize(2);
    canvas->setTextColor(col);
    // Convert UTF-8 to CP437 for width calc and display
    char cp[40];
    utf8_to_cp437(cp, sizeof(cp), ans);
    int16_t tw = (int16_t)(strlen(cp) * 12);
    if (tw <= LCD_WIDTH - 60) {
        canvas->setCursor((LCD_WIDTH - tw) / 2, cy - 8);
        canvas->print(cp);
    } else {
        int mid = strlen(cp) / 2;
        int split = mid;
        while (split > 0 && cp[split] != ' ') split--;
        if (split == 0) split = mid;
        cp[split] = '\0';
        int16_t w1 = (int16_t)(strlen(cp) * 12);
        int16_t w2 = (int16_t)(strlen(cp + split + 1) * 12);
        canvas->setCursor((LCD_WIDTH - w1) / 2, cy - 20);
        canvas->print(cp);
        canvas->setCursor((LCD_WIDTH - w2) / 2, cy + 4);
        canvas->print(cp + split + 1);
    }

    draw_battery_g(canvas, LCD_WIDTH, LCD_HEIGHT);
    draw_watermark_g(canvas, LCD_WIDTH, LCD_HEIGHT);
    canvas->flush();
}

static void drawRolling(uint8_t frame) {
    canvas->fillScreen(0x0000);
    int16_t cx = LCD_WIDTH / 2;
    int16_t cy = 200;

    // Ball body — dark, spinning feel
    fillCircleAA(canvas, cx, cy, 120, 0x1082, 0x0000);
    drawCircleAA(canvas, cx, cy, 120, 0x2945, 0x1082);

    // Spinning dots around the "window"
    float angle = (float)frame * 0.5f;
    for (int i = 0; i < 3; i++) {
        float a = angle + i * 2.094f;  // 120° apart
        int16_t dx = cx + (int16_t)(40.0f * cosf(a));
        int16_t dy = cy + (int16_t)(40.0f * sinf(a));
        canvas->fillCircle(dx, dy, 6, 0x4A49);
    }

    // "..." pulsing in center
    const char *dots = "...";
    canvas->setTextSize(4, 5, 1);
    canvas->setTextColor(0x39C7);
    int16_t tw = (int16_t)(3 * 25);
    canvas->setCursor(cx - tw / 2, cy - 16);
    canvas->print(dots);

    draw_battery_g(canvas, LCD_WIDTH, LCD_HEIGHT);
    draw_watermark_g(canvas, LCD_WIDTH, LCD_HEIGHT);
    canvas->flush();
}

static void shake() {
    s_ansIdx    = (int)(esp_random() % N_ANSWERS);
    s_rolling   = true;
    s_showAns   = false;
    s_rollStart = millis();
    s_rollFrame = 0;
    common_activity();
    drawRolling(0);
}

static void readLanguageConfig() {
    s_useEN = false;
    SD_MMC.setPins(SDMMC_CLK, SDMMC_CMD, SDMMC_DATA);
    if (!SD_MMC.begin("/sdcard", true)) return;
    File f = SD_MMC.open("/setup/setup.txt");
    if (f) {
        char line[160];
        while (f.available()) {
            int n = f.readBytesUntil('\n', line, sizeof(line) - 1);
            line[n] = '\0';
            const char *p = strstr(line, "LANGUAGE");
            if (!p) continue;
            p += strlen("LANGUAGE");
            while (*p == ' ' || *p == '\t' || *p == '=' || *p == '"') p++;
            if ((p[0] == 'e' || p[0] == 'E') && (p[1] == 'n' || p[1] == 'N')) {
                s_useEN = true;
            }
            break;
        }
        f.close();
    }
    SD_MMC.end();
}

void app_8ball_setup(Arduino_SH8601 *gfx) {
    (void)gfx;
    canvas = g_canvas;
    readLanguageConfig();

    if (!expander.begin(0x20)) USBSerial.println("XCA9554 init failed");
    expander.pinMode(1, OUTPUT); expander.digitalWrite(1, LOW);
    expander.pinMode(2, OUTPUT); expander.digitalWrite(2, LOW);
    delay(20);
    expander.digitalWrite(1, HIGH);
    expander.digitalWrite(2, HIGH);

    if (!qmi.begin(Wire, QMI8658_L_SLAVE_ADDRESS, IIC_SDA, IIC_SCL))
        USBSerial.println("QMI8658 not found");
    qmi.configAccelerometer(SensorQMI8658::ACC_RANGE_4G,
                            SensorQMI8658::ACC_ODR_250Hz,
                            SensorQMI8658::LPF_MODE_2);
    qmi.enableAccelerometer();

    s_bootWas   = false;
    s_lastPwr   = 0;
    s_lastShake = 0;
    s_emaInit   = false;
    s_showAns   = false;
    randomSeed(esp_random());

    pinMode(BOOT_BTN, INPUT_PULLUP);
    drawIdle();
}

void app_8ball_loop() {
    common_tick();
    uint32_t now = millis();

    // Rolling animation — update every 80ms, then reveal answer
    if (s_rolling) {
        uint32_t elapsed = now - s_rollStart;
        if (elapsed >= ROLL_DURATION) {
            s_rolling  = false;
            s_showAns  = true;
            s_ansStart = millis();
            drawAnswer(s_ansIdx);
        } else {
            uint8_t newFrame = (uint8_t)(elapsed / 80);
            if (newFrame != s_rollFrame) {
                s_rollFrame = newFrame;
                drawRolling(s_rollFrame);
            }
        }
        delay(20);
        return;
    }

    // Auto-clear answer after 4 seconds
    if (s_showAns && now - s_ansStart > 4000) {
        s_showAns = false;
        drawIdle();
    }

    // IMU shake detection
    if (!s_showAns && qmi.getDataReady()) {
        float ax, ay, az;
        qmi.getAccelerometer(ax, ay, az);
        float A = 0.25f;
        if (!s_emaInit) { s_emaAx=ax; s_emaAy=ay; s_emaAz=az; s_emaInit=true; }
        else { s_emaAx+=A*(ax-s_emaAx); s_emaAy+=A*(ay-s_emaAy); s_emaAz+=A*(az-s_emaAz); }
        float mag = sqrtf(ax*ax + ay*ay + az*az);
        if (mag > SHAKE_THRESH && now - s_lastShake > SHAKE_COOLDOWN) {
            s_lastShake = now;
            shake();
        }
    }

    bool boot = (digitalRead(BOOT_BTN) == LOW);
    if (boot && !s_bootWas && !s_rolling && now - s_lastShake > SHAKE_COOLDOWN) {
        s_lastShake = now;
        shake();
    }
    s_bootWas = boot;

    if (common_consume_pwr_short()) common_activity();

    delay(20);
}
