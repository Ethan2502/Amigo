// alarm_timer.h -- software clock, daily alarm, and countdown timer.
#pragma once

#include <Arduino.h>
#include <Preferences.h>
#include "audio.h"

class AlarmTimer {
public:
    void begin() {
        preferences_.begin("amigo", false);
        alarmHour_ = preferences_.getUChar("alarmHour", 7);
        alarmMinute_ = preferences_.getUChar("alarmMinute", 0);
        alarmEnabled_ = preferences_.getBool("alarmOn", false);
        clockStartedAt_ = millis();
    }

    void update(Audio &audio) {
        const uint32_t now = millis();
        const uint32_t elapsedSeconds = (now - clockStartedAt_) / 1000;
        const uint32_t currentSeconds = clockBaseSeconds_ + elapsedSeconds;
        const uint32_t minuteCount = currentSeconds / 60;
        const uint16_t minuteOfDay = (currentSeconds / 60) % (24 * 60);

        if (alarmEnabled_ && minuteOfDay == alarmHour_ * 60 + alarmMinute_ &&
            minuteCount != lastAlarmMinute_) {
            lastAlarmMinute_ = minuteCount;
            alerting_ = true;
        }

        if (timerRunning_) {
            const uint32_t timerElapsed = now - timerStartedAt_;
            if (timerElapsed >= timerRemainingMs_) {
                timerRemainingMs_ = 0;
                timerRunning_ = false;
                alerting_ = true;
            } else {
                timerRemainingMs_ -= timerElapsed;
                timerStartedAt_ = now;
            }
        }

        if (alerting_ && (lastRingAt_ == 0 || now - lastRingAt_ >= 1200)) {
            audio.tone(880.0f, 260);
            lastRingAt_ = now;
        }
    }

    uint8_t clockHour() const {
        return (clockSeconds() / 3600) % 24;
    }

    uint8_t clockMinute() const {
        return (clockSeconds() / 60) % 60;
    }

    uint8_t alarmHour() const { return alarmHour_; }
    uint8_t alarmMinute() const { return alarmMinute_; }
    bool alarmEnabled() const { return alarmEnabled_; }
    bool alerting() const { return alerting_; }
    bool timerRunning() const { return timerRunning_; }
    uint32_t timerRemainingSeconds() const {
        return (timerRemainingMs_ + 999) / 1000;
    }

    void adjustClockHour(int amount) {
        setClock((clockHour() + 24 + amount) % 24, clockMinute());
    }

    void adjustClockMinute(int amount) {
        setClock(clockHour(), (clockMinute() + 60 + amount) % 60);
    }

    void adjustAlarmHour(int amount) {
        alarmHour_ = (alarmHour_ + 24 + amount) % 24;
        saveAlarm();
    }

    void adjustAlarmMinute(int amount) {
        alarmMinute_ = (alarmMinute_ + 60 + amount) % 60;
        saveAlarm();
    }

    void toggleAlarm() {
        alarmEnabled_ = !alarmEnabled_;
        saveAlarm();
    }

    void addTimerMinutes(uint8_t minutes) {
        if (!timerRunning_ && !alerting_) {
            timerRemainingMs_ += uint32_t(minutes) * 60 * 1000;
        }
    }

    void startPauseTimer() {
        if (alerting_) return;
        if (timerRunning_) {
            timerRunning_ = false;
        } else if (timerRemainingMs_ > 0) {
            timerStartedAt_ = millis();
            timerRunning_ = true;
        }
    }

    void resetTimer() {
        timerRunning_ = false;
        timerRemainingMs_ = 0;
    }

    void dismissAlert() {
        alerting_ = false;
        lastRingAt_ = 0;
    }

private:
    uint32_t clockSeconds() const {
        return clockBaseSeconds_ + (millis() - clockStartedAt_) / 1000;
    }

    void setClock(uint8_t hour, uint8_t minute) {
        clockBaseSeconds_ = uint32_t(hour) * 3600 + uint32_t(minute) * 60;
        clockStartedAt_ = millis();
        lastAlarmMinute_ = UINT32_MAX;
    }

    void saveAlarm() {
        preferences_.putUChar("alarmHour", alarmHour_);
        preferences_.putUChar("alarmMinute", alarmMinute_);
        preferences_.putBool("alarmOn", alarmEnabled_);
    }

    Preferences preferences_;
    uint32_t clockStartedAt_ = 0;
    uint32_t clockBaseSeconds_ = 0;
    uint32_t lastAlarmMinute_ = UINT32_MAX;
    uint32_t timerStartedAt_ = 0;
    uint32_t timerRemainingMs_ = 0;
    uint32_t lastRingAt_ = 0;
    uint8_t alarmHour_ = 7;
    uint8_t alarmMinute_ = 0;
    bool alarmEnabled_ = false;
    bool timerRunning_ = false;
    bool alerting_ = false;
};