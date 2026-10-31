// audio.h -- minimal I2S tone output to the MAX98357A.
//
// Targets the LEGACY I2S driver (driver/i2s.h), which is what Arduino-ESP32
// core 2.x ships. This is the core PlatformIO's espressif32 platform installs.
//
// Playback is non-blocking: tone()/play() only set up state, and update()
// pushes one chunk per loop() into whatever DMA space is free. Nothing here
// waits on the DMA, so a key press during a beep is never dropped.
#pragma once

#include <Arduino.h>
#include <driver/i2s.h>
#include <math.h>
#include "pins.h"

class Audio {
public:
    static constexpr uint32_t SAMPLE_RATE = 22050;
    static constexpr i2s_port_t PORT = I2S_NUM_0;

    // One step of a melody. freqHz <= 0 is a rest (silence for ms).
    struct Note {
        float freqHz;
        uint32_t ms;
    };

    // Deepest melody we can hold. The alert patterns are the longest user.
    static constexpr uint8_t QUEUE_MAX = 12;

    bool begin() {
#ifdef AUDIO_SD_CONNECTED
        // Drive shutdown low before anything else so the amp stays off while
        // the I2S clocks come up. IO38 floats until this runs, so the breakout's
        // own pull-up leaves the amp briefly enabled at power-on. Expect a small
        // pop; this shortens it but cannot remove it in firmware.
        pinMode(PIN_AUDIO_SD, OUTPUT);
        digitalWrite(PIN_AUDIO_SD, LOW);
        ampOn_ = false;
#endif

        i2s_config_t cfg = {};
        cfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
        cfg.sample_rate = SAMPLE_RATE;
        cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
        cfg.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
        cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
        cfg.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
        cfg.dma_buf_count = DMA_BUF_COUNT;
        cfg.dma_buf_len = DMA_BUF_LEN;
        cfg.use_apll = false;
        cfg.tx_desc_auto_clear = true; // output silence on underrun
        cfg.fixed_mclk = 0;

        if (i2s_driver_install(PORT, &cfg, 0, nullptr) != ESP_OK) return false;

        i2s_pin_config_t pins = {};
        pins.mck_io_num = I2S_PIN_NO_CHANGE;
        pins.bck_io_num = PIN_AUDIO_BCLK;
        pins.ws_io_num = PIN_AUDIO_LRC;
        pins.data_out_num = PIN_AUDIO_DOUT;
        pins.data_in_num = I2S_PIN_NO_CHANGE;

        if (i2s_set_pin(PORT, &pins) != ESP_OK) return false;

        i2s_zero_dma_buffer(PORT);
        ok_ = true;
        return true;
    }

    // ---- amplifier shutdown control (SD_MODE) ----
    //
    // SD_MODE is an analog threshold pin, not a plain enable. Below ~0.16V the
    // amp shuts down. Above ~1.4V it wakes in left-channel mode, which is fine
    // here because playback writes identical samples to both slots.
    //
    // Powering the amp up mid-silence still thumps a little, so instead of
    // toggling per beep we enable on demand and drop back to shutdown a short
    // while after the last sound. Back-to-back key presses then share one
    // power-up instead of clicking on every keystroke.

    static constexpr uint32_t AMP_WAKE_MS = 10;  // settle time after enable
    static constexpr uint32_t AMP_IDLE_MS = 400; // hold-on after last sound

    static constexpr size_t DMA_BUF_COUNT = 8;
    static constexpr size_t DMA_BUF_LEN = 256;
    static constexpr uint32_t DMA_DRAIN_MS =
        (DMA_BUF_COUNT * DMA_BUF_LEN * 1000) / SAMPLE_RATE + 5;

    void ampEnable() {
#ifdef AUDIO_SD_CONNECTED
        if (!ampOn_) {
            digitalWrite(PIN_AUDIO_SD, HIGH);
            delay(AMP_WAKE_MS);
            ampOn_ = true;
        }
        muteAt_ = 0; // cancel any pending shutdown
#endif
    }

    void ampDisable() {
#ifdef AUDIO_SD_CONNECTED
        digitalWrite(PIN_AUDIO_SD, LOW);
        ampOn_ = false;
        muteAt_ = 0;
#endif
    }

    // ---- playback ----

    // Play a single tone immediately, discarding anything already queued.
    // Key feedback uses this so the newest press always wins.
    void tone(float freqHz, uint32_t durationMs) {
        const Note n = {freqHz, durationMs};
        play(&n, 1);
    }

    // Play a melody immediately, discarding anything already queued. The first
    // note starts now; the rest are queued and picked up by update().
    void play(const Note *notes, size_t count) {
        if (!ok_ || !notes || count == 0) return;
        ampEnable();

        qCount_ = 0;
        qHead_ = 0;
        const size_t queued = min<size_t>(count - 1, QUEUE_MAX);
        for (size_t i = 0; i < queued; i++) queue_[i] = notes[i + 1];
        qCount_ = (uint8_t)queued;

        startNote_(notes[0]);
    }

    // Stop playback now and let the amp fall back to shutdown as usual.
    void stop() {
        playing_ = false;
        qCount_ = 0;
        qHead_ = 0;
        muteAt_ = millis() + DMA_DRAIN_MS + AMP_IDLE_MS;
    }

    // True while a note is sounding or still queued.
    bool busy() const { return playing_ || qCount_ > 0; }

    // Call every loop. Feeds the DMA and services the deferred amp shutdown.
    void update() {
        if (ok_ && playing_) {
            constexpr size_t CHUNK = 128;
            int16_t buf[CHUNK * 2]; // interleaved L/R

            const size_t n = min<size_t>(CHUNK, noteFrames_ - framesCompleted_);
            for (size_t i = 0; i < n; i++) {
                int16_t s = 0;
                if (!silent_) {
                    // Attack/release envelope so the beep does not click at the
                    // edges. pos is how far through the note this frame sits.
                    const float pos =
                        float(framesCompleted_ + i) / float(noteFrames_);
                    float env = 1.0f;
                    if (pos < 0.05f) env = pos / 0.05f;
                    else if (pos > 0.90f) env = (1.0f - pos) / 0.10f;

                    s = int16_t(sinf(phase_) * amplitude_ * env * 32767.0f);
                    phase_ += phaseStep_;
                    if (phase_ > 2.0f * PI) phase_ -= 2.0f * PI;
                }
                buf[i * 2] = s;
                buf[i * 2 + 1] = s;
            }

            // Timeout 0 makes this non-blocking: it writes only what fits in
            // the free DMA space right now and reports how much landed, so a
            // long note is spread across many loop() passes instead of
            // stalling the key scan. Advance by what was actually accepted.
            size_t bytesWritten = 0;
            const size_t bytesToWrite = n * 2 * sizeof(int16_t);
            if (i2s_write(PORT, buf, bytesToWrite, &bytesWritten, 0) == ESP_OK) {
                framesCompleted_ += bytesWritten / (2 * sizeof(int16_t));
            }

            // Phase was advanced for the whole chunk, so rewind the part the
            // DMA refused; otherwise a full buffer would skip samples and
            // detune the note.
            const size_t framesDropped =
                n - min<size_t>(n, bytesWritten / (2 * sizeof(int16_t)));
            if (framesDropped && !silent_) {
                phase_ -= phaseStep_ * float(framesDropped);
                while (phase_ < 0.0f) phase_ += 2.0f * PI;
            }

            if (framesCompleted_ >= noteFrames_) advance_();
        }

#ifdef AUDIO_SD_CONNECTED
        if (muteAt_ && (int32_t)(millis() - muteAt_) >= 0) ampDisable();
#endif
    }

    bool ampIsOn() const { return ampOn_; }

    uint32_t calculateNoteFrames(uint32_t durationMs) const {
        return (durationMs * SAMPLE_RATE) / 1000;
    }

    void bootChime() {
        static const Note chime[] = {
            {523.25f, 90},  // C5
            {659.25f, 90},  // E5
            {783.99f, 140}, // G5
        };
        play(chime, sizeof(chime) / sizeof(chime[0]));
    }

    bool ready() const { return ok_; }

private:
    // Load a note as the one now sounding. Phase restarts per note; the
    // envelope attack keeps that from clicking.
    void startNote_(const Note &n) {
        silent_ = (n.freqHz <= 0.0f);
        phase_ = 0.0f;
        phaseStep_ = silent_ ? 0.0f : (2.0f * PI * n.freqHz) / float(SAMPLE_RATE);
        noteFrames_ = calculateNoteFrames(n.ms);
        framesCompleted_ = 0;
        playing_ = (noteFrames_ > 0);
        if (!playing_) advance_(); // zero-length note: skip straight on
    }

    // Current note finished: take the next queued one, or go idle.
    void advance_() {
        if (qCount_ > 0) {
            const Note next = queue_[qHead_];
            qHead_ = (uint8_t)((qHead_ + 1) % QUEUE_MAX);
            qCount_--;
            startNote_(next);
            return;
        }
        playing_ = false;
        muteAt_ = millis() + DMA_DRAIN_MS + AMP_IDLE_MS;
    }

    bool ok_ = false;
    bool ampOn_ = false;
    uint32_t muteAt_ = 0; // 0 = no pending shutdown

    // Note state. Kept out of tone() so playback is non-blocking and a new
    // press can preempt mid-note without cutting the DMA off abruptly.
    bool playing_ = false;
    bool silent_ = false;     // current note is a rest
    float phase_ = 0.0f;      // where we are in the sine wave cycle
    float phaseStep_ = 0.0f;  // how much to advance the phase each sample
    float amplitude_ = 0.25f; // volume of the tone (0.0 to 1.0)
    uint32_t noteFrames_ = 0;      // frames in the current note
    uint32_t framesCompleted_ = 0; // frames of it already handed to the DMA

    Note queue_[QUEUE_MAX];
    uint8_t qHead_ = 0;
    uint8_t qCount_ = 0;
};
