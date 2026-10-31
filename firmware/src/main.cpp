// main.cpp -- Amigo bring-up firmware.
//
// Scope: boot, ILI9341 display, 3x3 Choc matrix, MAX98357A tone output,
// battery voltage readout, status LED heartbeat, time of day with a daily
// alarm and a countdown timer.
//
// STATUS: written against the schematic, not yet flashed to hardware.
//
// The nine keys are shared across four modes. Key 9 always cycles the mode,
// and while an alert is sounding ANY key silences it and nothing else.
//
//   KEYPAD  1-8 play tones                       (the original bring-up pad)
//   CLOCK   1/2 hour  3/4 minute  5 zero seconds
//   ALARM   1/2 hour  3/4 minute  5 enable/disable
//   TIMER   1/2 +/-1min  3/4 +/-10s  5 start/pause  6 reset

#include <Arduino.h>
#include <TFT_eSPI.h>
#include "pins.h"
#include "keypad.h"
#include "audio.h"
#include "alarm_timer.h"

TFT_eSPI tft;
Keypad keys;
Audio audio;
AlarmTimer clockAlarm;

// Per-key tones, laid out to match the physical 3x3 grid (C major pentatonic).
static const float KEY_TONES[9] = {
    261.63f, 293.66f, 329.63f,
    392.00f, 440.00f, 523.25f,
    587.33f, 659.25f, 783.99f
};

enum Mode : uint8_t { MODE_KEYPAD, MODE_CLOCK, MODE_ALARM, MODE_TIMER, MODE_COUNT };

static const char *MODE_NAMES[MODE_COUNT] = {"KEYPAD", "CLOCK", "ALARM", "TIMER"};

static constexpr uint8_t KEY_MODE = 8; // key 9, bottom right

// ---- display layout ----
static constexpr int GRID_X = 40;
static constexpr int GRID_Y = 90;
static constexpr int CELL = 50;
static constexpr int GAP = 6;

static constexpr int CONTENT_Y = 64;  // below the header
static constexpr int BOTTOM_H = 22;   // battery strip
static constexpr int VALUE_Y = 110;   // big value baseline area
static constexpr int SUB_Y = 170;     // secondary line
static constexpr int HINT_Y = 220;    // first hint line
static constexpr int HINT_STEP = 18;
static constexpr int GRID_HINT_Y = 264; // below the 3x3 grid (ends at y=252)

static Mode mode = MODE_KEYPAD;
static bool uiDirty = true;     // full content repaint needed
static bool alertShown = false; // alert banner currently painted

static uint32_t lastHeartbeat = 0;
static uint32_t lastBattery = 0;
static uint32_t lastClockPaint = 0;
static char lastValue[16] = "";

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

void drawChrome() {
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextDatum(TC_DATUM);
    tft.drawString("AMIGO", tft.width() / 2, 14, 4);
    tft.setTextDatum(TL_DATUM);
}

void drawBattery(float volts) {
    tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    tft.fillRect(0, tft.height() - BOTTOM_H, tft.width(), BOTTOM_H, TFT_BLACK);
    char buf[24];
    snprintf(buf, sizeof(buf), "BATT %.2f V", volts);
    tft.drawString(buf, 8, tft.height() - 20, 2);
}

// Wipe everything between the header and the battery strip.
void clearContent() {
    tft.fillRect(0, CONTENT_Y, tft.width(),
                 tft.height() - CONTENT_Y - BOTTOM_H, TFT_BLACK);
    lastValue[0] = '\0';
}

// Centered big value. Padded so a shorter string cannot leave fragments of
// the previous one behind.
void drawValue(const char *text, uint16_t colour) {
    tft.setTextColor(colour, TFT_BLACK);
    tft.setTextDatum(MC_DATUM);
    tft.setTextSize(2);
    tft.setTextPadding(tft.width());
    tft.drawString(text, tft.width() / 2, VALUE_Y, 4);
    tft.setTextPadding(0);
    tft.setTextSize(1);
    tft.setTextDatum(TL_DATUM);
}

void drawCentered(const char *text, int y, uint16_t colour, uint8_t font) {
    tft.setTextColor(colour, TFT_BLACK);
    tft.setTextDatum(MC_DATUM);
    tft.setTextPadding(tft.width());
    tft.drawString(text, tft.width() / 2, y, font);
    tft.setTextPadding(0);
    tft.setTextDatum(TL_DATUM);
}

void drawHintsAt(const char *a, const char *b, int y) {
    tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
    tft.setTextDatum(TC_DATUM);
    tft.setTextPadding(tft.width());
    if (a) tft.drawString(a, tft.width() / 2, y, 2);
    if (b) tft.drawString(b, tft.width() / 2, y + HINT_STEP, 2);
    tft.setTextPadding(0);
    tft.setTextDatum(TL_DATUM);
}

void drawHints(const char *a, const char *b) { drawHintsAt(a, b, HINT_Y); }

void drawModeLabel() {
    char buf[32];
    snprintf(buf, sizeof(buf), "%s  (9 = mode)", MODE_NAMES[mode]);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextDatum(TC_DATUM);
    tft.setTextPadding(tft.width());
    tft.drawString(buf, tft.width() / 2, 46, 2);
    tft.setTextPadding(0);
    tft.setTextDatum(TL_DATUM);
}

// The alert banner takes over the content area until a key is pressed.
void drawAlert() {
    const bool isAlarm = clockAlarm.alertKind() == AlarmTimer::ALERT_ALARM;
    tft.fillRect(0, CONTENT_Y, tft.width(),
                 tft.height() - CONTENT_Y - BOTTOM_H, isAlarm ? TFT_MAROON : TFT_NAVY);
    tft.setTextColor(TFT_WHITE, isAlarm ? TFT_MAROON : TFT_NAVY);
    tft.setTextDatum(MC_DATUM);
    tft.setTextSize(2);
    tft.drawString(isAlarm ? "ALARM" : "TIME UP", tft.width() / 2, VALUE_Y, 4);
    tft.setTextSize(1);
    tft.drawString("press any key", tft.width() / 2, SUB_Y, 2);
    tft.setTextDatum(TL_DATUM);
}

// Repaint the body for the current mode. Only writes the big value when its
// text actually changed, so the 1 Hz refresh does not flicker the whole page.
void drawBody(bool full) {
    char value[16];
    char sub[32];

    switch (mode) {
    case MODE_KEYPAD:
        if (full) {
            drawGrid();
            drawHintsAt("1-8 play tones", nullptr, GRID_HINT_Y);
        }
        return;

    case MODE_CLOCK:
        snprintf(value, sizeof(value), "%02u:%02u", clockAlarm.clockHour(),
                 clockAlarm.clockMinute());
        snprintf(sub, sizeof(sub), "sec %02u", clockAlarm.clockSecond());
        if (full) drawHints("1/2 hour   3/4 minute", "5 zero seconds");
        break;

    case MODE_ALARM:
        snprintf(value, sizeof(value), "%02u:%02u", clockAlarm.alarmHour(),
                 clockAlarm.alarmMinute());
        snprintf(sub, sizeof(sub), "alarm %s",
                 clockAlarm.alarmEnabled() ? "ON" : "OFF");
        if (full) drawHints("1/2 hour   3/4 minute", "5 enable/disable");
        break;

    case MODE_TIMER: {
        const uint32_t left = clockAlarm.timerRemainingSeconds();
        snprintf(value, sizeof(value), "%02u:%02u", (unsigned)(left / 60),
                 (unsigned)(left % 60));
        const char *state = "finished";
        if (clockAlarm.timerRunning()) state = "running";
        else if (left == clockAlarm.timerSetSeconds() && left) state = "ready";
        else if (left) state = "paused";
        snprintf(sub, sizeof(sub), "%s", state);
        if (full) drawHints("1/2 +-min   3/4 +-10s", "5 start/pause   6 reset");
        break;
    }

    default:
        return;
    }

    if (full || strcmp(value, lastValue) != 0) {
        uint16_t colour = TFT_WHITE;
        if (mode == MODE_ALARM && !clockAlarm.alarmEnabled()) colour = TFT_DARKGREY;
        if (mode == MODE_TIMER && clockAlarm.timerRunning()) colour = TFT_GREENYELLOW;
        drawValue(value, colour);
        snprintf(lastValue, sizeof(lastValue), "%s", value);
    }
    drawCentered(sub, SUB_Y, TFT_LIGHTGREY, 2);
}

void render(bool full) {
    if (clockAlarm.alerting()) {
        if (!alertShown) {
            drawAlert();
            alertShown = true;
        }
        return;
    }
    if (alertShown) { // alert just cleared: rebuild the normal body
        alertShown = false;
        clearContent();
        drawModeLabel();
        full = true;
    }
    if (full) {
        drawModeLabel();
        drawBody(true);
    } else {
        drawBody(false);
    }
}

void cycleMode() {
    mode = (Mode)((mode + 1) % MODE_COUNT);
    clearContent();
    uiDirty = true;
    audio.tone(1318.51f, 40); // E6 blip to confirm the switch
}

void handleKeyDown(uint8_t k) {
    // An alert owns the keypad: the first press only silences it.
    if (clockAlarm.alerting()) {
        clockAlarm.dismiss();
        audio.stop();
        uiDirty = true;
        return;
    }

    if (k == KEY_MODE) {
        cycleMode();
        return;
    }

    switch (mode) {
    case MODE_KEYPAD:
        audio.tone(KEY_TONES[k], 70);
        return;

    case MODE_CLOCK:
        switch (k) {
        case 0: clockAlarm.adjustClockHour(+1); break;
        case 1: clockAlarm.adjustClockHour(-1); break;
        case 2: clockAlarm.adjustClockMinute(+1); break;
        case 3: clockAlarm.adjustClockMinute(-1); break;
        case 4: clockAlarm.zeroClockSeconds(); break;
        default: return;
        }
        break;

    case MODE_ALARM:
        switch (k) {
        case 0: clockAlarm.adjustAlarmHour(+1); break;
        case 1: clockAlarm.adjustAlarmHour(-1); break;
        case 2: clockAlarm.adjustAlarmMinute(+1); break;
        case 3: clockAlarm.adjustAlarmMinute(-1); break;
        case 4: clockAlarm.toggleAlarm(); break;
        default: return;
        }
        break;

    case MODE_TIMER:
        switch (k) {
        case 0: clockAlarm.adjustTimer(+60); break;
        case 1: clockAlarm.adjustTimer(-60); break;
        case 2: clockAlarm.adjustTimer(+10); break;
        case 3: clockAlarm.adjustTimer(-10); break;
        case 4: clockAlarm.toggleTimer(); break;
        case 5: clockAlarm.resetTimer(); break;
        default: return;
        }
        break;

    default:
        return;
    }

    audio.tone(1046.50f, 30); // C6 tick for an accepted edit
    uiDirty = true;
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
    drawChrome();

    analogSetPinAttenuation(PIN_BAT_SENSE, ADC_11db); // ~0 to 3.1V usable range

    keys.begin();
    clockAlarm.begin();

    if (!audio.begin()) {
        Serial.println("[audio] I2S init failed");
        tft.setTextColor(TFT_RED, TFT_BLACK);
        tft.drawString("audio init FAILED", 8, 64, 2);
    }

    render(true);
    drawBattery(readBatteryVolts());

    // Fade the backlight up.
    for (int i = 0; i <= 255; i += 5) {
        setBacklight(i);
        delay(4);
    }

    if (audio.ready()) audio.bootChime();

    Serial.printf("[boot] PSRAM: %u bytes free\n", (unsigned)ESP.getFreePsram());
    Serial.printf("[alarm] %02u:%02u %s\n", clockAlarm.alarmHour(),
                  clockAlarm.alarmMinute(),
                  clockAlarm.alarmEnabled() ? "enabled" : "disabled");
}

void loop() {
    audio.update();            // feeds the I2S DMA, services the amp shutdown
    clockAlarm.update(audio);  // clock tick, alarm/timer checks, alert ringing

    if (keys.scan()) {
        if (mode == MODE_KEYPAD && !clockAlarm.alerting()) drawGrid();
        const int k = keys.lastPressed();
        if (k >= 0 && k < 9) {
            const uint8_t r = k / 3, c = k % 3;
            if (keys.isDown(r, c)) {
                Serial.printf("[key] %d down (mode %s)\n", k + 1, MODE_NAMES[mode]);
                handleKeyDown((uint8_t)k);
            }
        }
    }

    const uint32_t now = millis();

    // An unacknowledged alert can time out on its own, so the banner has to be
    // able to come down without a key press.
    if (clockAlarm.alerting() != alertShown) uiDirty = true;

    if (uiDirty) {
        uiDirty = false;
        render(true);
    } else if (now - lastClockPaint >= 250) {
        lastClockPaint = now;
        render(false); // cheap: only redraws the value when its text changed
    }

    if (now - lastHeartbeat >= 1000) {
        lastHeartbeat = now;
        digitalWrite(PIN_LED_STATUS, !digitalRead(PIN_LED_STATUS));
    }

    if (now - lastBattery >= 5000) {
        lastBattery = now;
        drawBattery(readBatteryVolts());
    }

    delay(2);
}
