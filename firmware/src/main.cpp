// main.cpp -- Amigo bring-up firmware.
//
// Scope: boot, ILI9341 display, 3x3 Choc matrix, MAX98357A tone output,
// battery voltage readout, status LED heartbeat.
//
// STATUS: written against the schematic, not yet flashed to hardware.

#include <Arduino.h>
#include <TFT_eSPI.h>
#include "pins.h"
#include "keypad.h"
#include "audio.h"

TFT_eSPI tft;
Keypad keys;
Audio audio;

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
    tft.drawString("bring-up", 8, 46, 2);
}

void drawBattery(float volts) {
    tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    tft.fillRect(0, tft.height() - 22, tft.width(), 22, TFT_BLACK);
    char buf[24];
    snprintf(buf, sizeof(buf), "BATT %.2f V", volts);
    tft.drawString(buf, 8, tft.height() - 20, 2);
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

    if (!audio.begin()) {
        Serial.println("[audio] I2S init failed");
        tft.setTextColor(TFT_RED, TFT_BLACK);
        tft.drawString("audio init FAILED", 8, 64, 2);
    }

    drawGrid();
    drawBattery(readBatteryVolts());

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

    if (keys.scan()) {
        drawGrid();
        const int k = keys.lastPressed();
        if (k >= 0 && k < 9) {
            const uint8_t r = k / 3, c = k % 3;
            if (keys.isDown(r, c)) {
                Serial.printf("[key] %d down\n", k + 1);
                audio.tone(KEY_TONES[k], 70);
            }
        }
    }

    const uint32_t now = millis();

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
