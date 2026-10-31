// alarm_timer.h -- software time-of-day clock, daily alarm, countdown timer.
//
// There is no RTC on the board, so the clock is a millis() offset seeded by
// the user and it restarts at 00:00 on every power cycle. The alarm hour and
// minute DO survive a reboot (NVS via Preferences); the time of day does not.
//
// Everything here is non-blocking and owns no display or key state: update()
// is called once per loop() and raises alerting(), which the UI renders and
// any key press dismisses.
#pragma once

#include <Arduino.h>
#include <Preferences.h>
#include "audio.h"

class AlarmTimer {
public:
    static constexpr uint32_t DAY_SECONDS = 24UL * 60UL * 60UL;
    static constexpr uint32_t MAX_TIMER_SECONDS = 99UL * 60UL + 59UL; // 99:59

    // Gap between repeats of the alert pattern, and how long an unacknowledged
    // alert keeps beeping before it gives up (so a timer that finishes in a
    // bag does not flatten the battery).
    static constexpr uint32_t RING_PERIOD_MS = 1600;
    static constexpr uint32_t ALERT_TIMEOUT_MS = 60000;

    // Alarm edits are keypress-driven, so coalesce them and only touch NVS
    // once the user has stopped adjusting. Saves needless flash wear.
    static constexpr uint32_t SAVE_DEBOUNCE_MS = 2000;

    enum Alert : uint8_t { ALERT_NONE, ALERT_ALARM, ALERT_TIMER };

    void begin() {
        prefs_.begin("amigo", false);
        alarmHour_ = constrainU8_(prefs_.getUChar("alarmHour", 7), 23);
        alarmMinute_ = constrainU8_(prefs_.getUChar("alarmMin", 0), 59);
        alarmEnabled_ = prefs_.getBool("alarmOn", false);
        timerSetSeconds_ = prefs_.getUInt("timerSet", 5 * 60);
        if (timerSetSeconds_ > MAX_TIMER_SECONDS) timerSetSeconds_ = 5 * 60;

        refMs_ = millis();
        baseSeconds_ = 0;
        timerRemainingMs_ = timerSetSeconds_ * 1000UL;
    }

    // ---- clock ----

    uint32_t clockSeconds() const { return baseSeconds_; }
    uint8_t clockHour() const { return (uint8_t)(baseSeconds_ / 3600); }
    uint8_t clockMinute() const { return (uint8_t)((baseSeconds_ / 60) % 60); }
    uint8_t clockSecond() const { return (uint8_t)(baseSeconds_ % 60); }

    void setClock(uint8_t hour, uint8_t minute, uint8_t second = 0) {
        baseSeconds_ = ((hour % 24) * 3600UL) + ((minute % 60) * 60UL) + (second % 60);
        refMs_ = millis();
        // Scrolling the clock THROUGH the alarm minute while setting it should
        // not ring. Latch if we landed on it, so it arms for the next pass
        // instead; clear the latch otherwise so that pass can still fire.
        alarmLatched_ = (minuteOfDay_() == alarmMinuteOfDay_());
    }

    void adjustClockHour(int delta) {
        setClock((uint8_t)((clockHour() + 24 + delta) % 24), clockMinute(), clockSecond());
    }

    void adjustClockMinute(int delta) {
        setClock(clockHour(), (uint8_t)((clockMinute() + 60 + delta) % 60), clockSecond());
    }

    // Drop the seconds so the user can sync to an external clock on the minute.
    void zeroClockSeconds() { setClock(clockHour(), clockMinute(), 0); }

    // ---- alarm ----

    uint8_t alarmHour() const { return alarmHour_; }
    uint8_t alarmMinute() const { return alarmMinute_; }
    bool alarmEnabled() const { return alarmEnabled_; }

    void adjustAlarmHour(int delta) {
        alarmHour_ = (uint8_t)((alarmHour_ + 24 + delta) % 24);
        markDirty_();
    }

    void adjustAlarmMinute(int delta) {
        alarmMinute_ = (uint8_t)((alarmMinute_ + 60 + delta) % 60);
        markDirty_();
    }

    void toggleAlarm() {
        alarmEnabled_ = !alarmEnabled_;
        // Enabling while already sitting on the alarm minute should not fire
        // instantly; wait for the next time we land on it.
        alarmLatched_ = alarmEnabled_ && (minuteOfDay_() == alarmMinuteOfDay_());
        markDirty_();
    }

    // ---- countdown timer ----

    bool timerRunning() const { return timerRunning_; }
    bool timerArmed() const { return timerRunning_ || timerRemainingMs_ > 0; }
    uint32_t timerSetSeconds() const { return timerSetSeconds_; }

    uint32_t timerRemainingSeconds() const {
        return (remainingMsNow_() + 999UL) / 1000UL;
    }

    void adjustTimer(int deltaSeconds) {
        if (timerRunning_) return; // editing a running timer is confusing
        int32_t next = (int32_t)timerSetSeconds_ + deltaSeconds;
        if (next < 0) next = 0;
        if (next > (int32_t)MAX_TIMER_SECONDS) next = (int32_t)MAX_TIMER_SECONDS;
        timerSetSeconds_ = (uint32_t)next;
        timerRemainingMs_ = timerSetSeconds_ * 1000UL;
        markDirty_();
    }

    // Start, or resume a paused countdown. Restarts from the configured
    // duration once the previous run has been consumed.
    void startTimer() {
        uint32_t remaining = timerRemainingMs_;
        if (remaining == 0) remaining = timerSetSeconds_ * 1000UL;
        if (remaining == 0) return; // nothing configured
        timerEndMs_ = millis() + remaining;
        timerRemainingMs_ = remaining;
        timerRunning_ = true;
    }

    void pauseTimer() {
        if (!timerRunning_) return;
        timerRemainingMs_ = remainingMsNow_();
        timerRunning_ = false;
    }

    void toggleTimer() { timerRunning_ ? pauseTimer() : startTimer(); }

    void resetTimer() {
        timerRunning_ = false;
        timerRemainingMs_ = timerSetSeconds_ * 1000UL;
    }

    // ---- alert ----

    bool alerting() const { return alert_ != ALERT_NONE; }
    Alert alertKind() const { return alert_; }

    void dismiss() {
        alert_ = ALERT_NONE;
        lastRingAt_ = 0;
        // Nothing else to undo: an expired timer already zeroed its remainder
        // in tickTimer_, and a paused one must keep whatever it had left.
    }

    // Call once per loop().
    void update(Audio &audio) {
        const uint32_t now = millis();

        tickClock_(now);
        tickAlarm_();
        tickTimer_(now);
        tickAlert_(now, audio);
        flushPrefs_(now);
    }

private:
    // ---- clock ----

    // Advance the stored time of day by whole elapsed seconds and move the
    // reference forward by exactly that much, so the sub-second remainder is
    // carried rather than discarded. Unsigned subtraction makes this safe
    // across the ~49 day millis() rollover.
    void tickClock_(uint32_t now) {
        const uint32_t elapsed = now - refMs_;
        if (elapsed < 1000) return;
        const uint32_t whole = elapsed / 1000;
        refMs_ += whole * 1000;
        baseSeconds_ = (baseSeconds_ + whole) % DAY_SECONDS;
    }

    uint16_t minuteOfDay_() const { return (uint16_t)(baseSeconds_ / 60); }
    uint16_t alarmMinuteOfDay_() const {
        return (uint16_t)(alarmHour_ * 60 + alarmMinute_);
    }

    // Fire on the edge into the alarm minute and latch, so it rings once per
    // day rather than once per loop for a whole minute.
    void tickAlarm_() {
        const bool onAlarmMinute =
            alarmEnabled_ && (minuteOfDay_() == alarmMinuteOfDay_());
        if (onAlarmMinute && !alarmLatched_) {
            alarmLatched_ = true;
            raise_(ALERT_ALARM);
        } else if (!onAlarmMinute) {
            alarmLatched_ = false;
        }
    }

    // ---- timer ----

    uint32_t remainingMsNow_() const {
        if (!timerRunning_) return timerRemainingMs_;
        const int32_t left = (int32_t)(timerEndMs_ - millis());
        return left > 0 ? (uint32_t)left : 0;
    }

    void tickTimer_(uint32_t now) {
        if (!timerRunning_) return;
        if ((int32_t)(now - timerEndMs_) < 0) return;
        timerRunning_ = false;
        timerRemainingMs_ = 0;
        raise_(ALERT_TIMER);
    }

    // ---- alert ----

    void raise_(Alert kind) {
        alert_ = kind;
        alertStartedAt_ = millis();
        lastRingAt_ = 0; // ring immediately on the next tick
    }

    void tickAlert_(uint32_t now, Audio &audio) {
        if (alert_ == ALERT_NONE) return;

        if (now - alertStartedAt_ >= ALERT_TIMEOUT_MS) {
            dismiss();
            return;
        }
        if (lastRingAt_ != 0 && (now - lastRingAt_) < RING_PERIOD_MS) return;

        // Two voices so you can tell which one went off without looking.
        static const Audio::Note alarmRing[] = {
            {880.00f, 150}, {0.0f, 80}, {880.00f, 150},
            {0.0f, 80},     {1174.66f, 220},
        };
        static const Audio::Note timerRing[] = {
            {1046.50f, 110}, {0.0f, 70}, {1046.50f, 110},
            {0.0f, 70},      {1046.50f, 110},
        };

        if (alert_ == ALERT_ALARM) {
            audio.play(alarmRing, sizeof(alarmRing) / sizeof(alarmRing[0]));
        } else {
            audio.play(timerRing, sizeof(timerRing) / sizeof(timerRing[0]));
        }
        lastRingAt_ = now;
    }

    // ---- persistence ----

    void markDirty_() {
        dirty_ = true;
        dirtyAt_ = millis();
    }

    void flushPrefs_(uint32_t now) {
        if (!dirty_ || (now - dirtyAt_) < SAVE_DEBOUNCE_MS) return;
        prefs_.putUChar("alarmHour", alarmHour_);
        prefs_.putUChar("alarmMin", alarmMinute_);
        prefs_.putBool("alarmOn", alarmEnabled_);
        prefs_.putUInt("timerSet", timerSetSeconds_);
        dirty_ = false;
    }

    static uint8_t constrainU8_(uint8_t v, uint8_t hi) { return v > hi ? 0 : v; }

    Preferences prefs_;

    // Clock: time of day in seconds, plus the millis() it was last advanced at.
    uint32_t baseSeconds_ = 0;
    uint32_t refMs_ = 0;

    uint8_t alarmHour_ = 7;
    uint8_t alarmMinute_ = 0;
    bool alarmEnabled_ = false;
    bool alarmLatched_ = false;

    uint32_t timerSetSeconds_ = 5 * 60; // configured duration
    uint32_t timerRemainingMs_ = 0;     // authoritative only while paused
    uint32_t timerEndMs_ = 0;           // authoritative only while running
    bool timerRunning_ = false;

    Alert alert_ = ALERT_NONE;
    uint32_t alertStartedAt_ = 0;
    uint32_t lastRingAt_ = 0;

    bool dirty_ = false;
    uint32_t dirtyAt_ = 0;
};
