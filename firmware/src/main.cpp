// main.cpp -- Amigo alarm, timer, and keypad firmware.

#include <Arduino.h>
#include <TFT_eSPI.h>
#include "pins.h"
#include "keypad.h"
#include "audio.h"
#include "alarm_timer.h"

TFT_eSPI tft;
Keypad keys;
Audio audio;
AlarmTimer alarmTimer;

enum class Screen : uint8_t { HOME, ALARM, TIMER };
static Screen screen = Screen::HOME;

// Per-key tones, laid out to match the physical 3x3 grid (C major pentatonic).
static const float KEY_TONES[9] = {
    261.63f, 293.66f, 329.63f,
    392.00f, 440.00f, 523.25f,
    587.33f, 659.25f, 783.99f
};

// ---- display layout ----
static constexpr int GRID_X = 40;
static constexpr int GRID_Y = 90;
static constexpr int CELL = 50;
static constexpr int GAP = 6;

static uint32_t lastHeartbeat = 0;
static uint32_t lastBattery = 0;
static float batteryVolts = 0.0f;

void setBacklight(uint8_t level) {
    ledcWrite(BL_LEDC_CHANNEL, level); // core 2.x takes a channel, not a pin
}

float readBatteryVolts() {
    // 32 samples to knock down ADC noise. analogReadMilliVolts applies the
    // per-chip eFuse calibration, which is much better than raw counts.
    uint32_t acc = 0;
    for (int i = 0; i < 32; i++) acc += analogReadMilliVolts(PIN_BAT_SENSE);
    return (acc / 32.0f) * BAT_DIVIDER_RATIO / 1000.0f;
}

void drawCell(uint8_t r, uint8_t c, bool down) {
    const int x = GRID_X + c * (CELL + GAP);
    const int y = GRID_Y + r * (CELL + GAP);
    tft.fillRoundRect(x, y, CELL, CELL, 6, down ? TFT_CYAN : TFT_DARKGREY);
    tft.drawRoundRect(x, y, CELL, CELL, 6, TFT_WHITE);

    tft.setTextColor(down ? TFT_BLACK : TFT_WHITE);
    tft.setTextDatum(MC_DATUM);
    tft.drawNumber(r * 3 + c + 1, x + CELL / 2, y + CELL / 2, 4);
    tft.setTextDatum(TL_DATUM);
}

void drawGrid() {
    for (uint8_t r = 0; r < 3; r++)
        for (uint8_t c = 0; c < 3; c++)
            drawCell(r, c, keys.isDown(r, c));
}

void drawBattery(float volts) {
    tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    tft.fillRect(0, tft.height() - 22, tft.width(), 22, TFT_BLACK);
    char buf[24];
    snprintf(buf, sizeof(buf), "BATT %.2f V", volts);
    tft.drawString(buf, 8, tft.height() - 20, 2);
}

void drawHome() {
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextDatum(TC_DATUM);
    tft.drawString("AMIGO", tft.width() / 2, 14, 4);
    tft.setTextDatum(TL_DATUM);
    tft.drawString("1 Alarm   2 Timer", 12, 48, 2);
    drawGrid();
}

void drawAlarm() {
    char value[24];
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextDatum(TC_DATUM);
    tft.drawString("ALARM", tft.width() / 2, 18, 4);
    tft.setTextDatum(TL_DATUM);
    snprintf(value, sizeof(value), "Clock  %02u:%02u", alarmTimer.clockHour(), alarmTimer.clockMinute());
    tft.drawString(value, 16, 68, 4);
    snprintf(value, sizeof(value), "Alarm  %02u:%02u  %s",
             alarmTimer.alarmHour(), alarmTimer.alarmMinute(),
             alarmTimer.alarmEnabled() ? "ON" : "OFF");
    tft.drawString(value, 16, 112, 2);
    tft.setTextColor(alarmTimer.alerting() ? TFT_RED : TFT_LIGHTGREY, TFT_BLACK);
    tft.drawString(alarmTimer.alerting() ? "ALARM RINGING" : "Clock resets at power-on", 16, 148, 2);
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.drawString("1/2 Clock hour/min +", 16, 190, 2);
    tft.drawString("4/5 Alarm hour/min +", 16, 214, 2);
    tft.drawString("7 Toggle alarm  9 Back", 16, 238, 2);
}

void drawTimer() {
    char value[24];
    const uint32_t remaining = alarmTimer.timerRemainingSeconds();
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextDatum(TC_DATUM);
    tft.drawString("TIMER", tft.width() / 2, 18, 4);
    tft.setTextDatum(TL_DATUM);
    snprintf(value, sizeof(value), "%02lu:%02lu", (unsigned long)(remaining / 60), (unsigned long)(remaining % 60));
    tft.setTextColor(alarmTimer.alerting() ? TFT_RED : TFT_WHITE, TFT_BLACK);
    tft.drawString(value, 58, 76, 6);
    tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    tft.drawString(alarmTimer.alerting() ? "TIMER DONE" : (alarmTimer.timerRunning() ? "RUNNING" : "PAUSED"), 16, 146, 2);
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.drawString("1 +1m  2 +5m  3 +10m", 16, 190, 2);
    tft.drawString("5 Start/Pause  8 Reset", 16, 214, 2);
    tft.drawString("9 Back / dismiss", 16, 238, 2);
}

void drawScreen() {
    if (screen == Screen::ALARM) drawAlarm();
    else if (screen == Screen::TIMER) drawTimer();
    else drawHome();
    drawBattery(batteryVolts);
}

void handleKey(uint8_t key) {
    if (screen == Screen::HOME) {
        if (key == 0) screen = Screen::ALARM;
        else if (key == 1) screen = Screen::TIMER;
        else if (key < 9) audio.tone(KEY_TONES[key], 70);
        drawScreen();
        return;
    }

    if (screen == Screen::ALARM) {
        switch (key) {
        case 0: alarmTimer.adjustClockHour(1); break;
        case 1: alarmTimer.adjustClockMinute(1); break;
        case 3: alarmTimer.adjustAlarmHour(1); break;
        case 4: alarmTimer.adjustAlarmMinute(1); break;
        case 6: alarmTimer.toggleAlarm(); break;
        case 8:
            alarmTimer.dismissAlert();
            screen = Screen::HOME;
            break;
        }
    } else {
        switch (key) {
        case 0: alarmTimer.addTimerMinutes(1); break;
        case 1: alarmTimer.addTimerMinutes(5); break;
        case 2: alarmTimer.addTimerMinutes(10); break;
        case 4: alarmTimer.startPauseTimer(); break;
        case 7: alarmTimer.resetTimer(); break;
        case 8:
            alarmTimer.dismissAlert();
            alarmTimer.resetTimer();
            screen = Screen::HOME;
            break;
        }
    }
    drawScreen();
}

void setup() {
    Serial.begin(115200);

    pinMode(PIN_LED_STATUS, OUTPUT);
    digitalWrite(PIN_LED_STATUS, LOW);

    // Backlight off during init so the user does not see a garbage frame.
    ledcSetup(BL_LEDC_CHANNEL, BL_LEDC_FREQ_HZ, BL_LEDC_RES_BITS);
    ledcAttachPin(PIN_LCD_BL, BL_LEDC_CHANNEL);
    setBacklight(0);

    tft.init();
    tft.setRotation(0);

    analogSetPinAttenuation(PIN_BAT_SENSE, ADC_11db); // ~0 to 3.1V usable range

    keys.begin();
    alarmTimer.begin();

    if (!audio.begin()) {
        Serial.println("[audio] I2S init failed");
        tft.setTextColor(TFT_RED, TFT_BLACK);
        tft.drawString("audio init FAILED", 8, 64, 2);
    }

    batteryVolts = readBatteryVolts();
    drawScreen();

    // Fade the backlight up.
    for (int i = 0; i <= 255; i += 5) {
        setBacklight(i);
        delay(4);
    }

    if (audio.ready()) audio.bootChime();

    Serial.printf("[boot] PSRAM: %u bytes free\n", (unsigned)ESP.getFreePsram());
}

void loop() {
    audio.update(); // services the deferred amplifier shutdown
    alarmTimer.update(audio);

    if (keys.scan()) {
        const int k = keys.lastPressed();
        if (k >= 0 && k < 9 && keys.isDown(k / 3, k % 3)) {
            Serial.printf("[key] %d down\n", k + 1);
            handleKey(k);
        }
    }

    static uint32_t lastClockDraw = 0;
    const uint32_t now = millis();
    if (screen != Screen::HOME && now - lastClockDraw >= 1000) {
        lastClockDraw = now;
        drawScreen();
    }

    if (now - lastHeartbeat >= 1000) {
        lastHeartbeat = now;
        digitalWrite(PIN_LED_STATUS, !digitalRead(PIN_LED_STATUS));
    }

    if (now - lastBattery >= 5000) {
        lastBattery = now;
        batteryVolts = readBatteryVolts();
        drawBattery(batteryVolts);
    }

    delay(2);
}
