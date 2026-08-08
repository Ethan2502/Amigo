// keypad.h -- 3x3 Kailh Choc matrix scanner with per-key debounce.
//
// Scan strategy: drive one row LOW at a time, read columns with internal
// pull-ups. A pressed key pulls its column LOW. Rows are held in a
// high-impedance state when not being scanned, which keeps the matrix safe
// even if the board has no per-key diodes (ghosting is still possible on
// 3-key rollover, but shorts are not).
#pragma once

#include <Arduino.h>
#include "pins.h"

class Keypad {
public:
    static constexpr uint8_t ROWS = 3;
    static constexpr uint8_t COLS = 3;
    static constexpr uint32_t DEBOUNCE_MS = 20;

    void begin() {
        for (uint8_t r = 0; r < ROWS; r++) {
            pinMode(PIN_ROW[r], INPUT);   // hi-Z until scanned
        }
        for (uint8_t c = 0; c < COLS; c++) {
            pinMode(PIN_COL[c], INPUT_PULLUP);
        }
        memset(state_, 0, sizeof(state_));
        memset(stable_, 0, sizeof(stable_));
        memset(lastChange_, 0, sizeof(lastChange_));
    }

    // Call every loop. Returns true if any key changed state this scan.
    bool scan() {
        bool changed = false;
        const uint32_t now = millis();

        for (uint8_t r = 0; r < ROWS; r++) {
            pinMode(PIN_ROW[r], OUTPUT);
            digitalWrite(PIN_ROW[r], LOW);
            delayMicroseconds(5);         // let the line settle

            for (uint8_t c = 0; c < COLS; c++) {
                const bool raw = (digitalRead(PIN_COL[c]) == LOW);
                if (raw != state_[r][c]) {
                    state_[r][c] = raw;
                    lastChange_[r][c] = now;
                } else if (state_[r][c] != stable_[r][c] &&
                           (now - lastChange_[r][c]) >= DEBOUNCE_MS) {
                    stable_[r][c] = state_[r][c];
                    changed = true;
                    if (stable_[r][c]) lastPressed_ = r * COLS + c;
                }
            }

            pinMode(PIN_ROW[r], INPUT);   // release the row
        }
        return changed;
    }

    bool isDown(uint8_t r, uint8_t c) const { return stable_[r][c]; }
    int lastPressed() const { return lastPressed_; }

private:
    bool state_[ROWS][COLS];
    bool stable_[ROWS][COLS];
    uint32_t lastChange_[ROWS][COLS];
    int lastPressed_ = -1;
};
