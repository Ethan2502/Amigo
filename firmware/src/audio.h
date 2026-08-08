// audio.h -- minimal I2S tone output to the MAX98357A.
//
// Targets the LEGACY I2S driver (driver/i2s.h), which is what Arduino-ESP32
// core 2.x ships. This is the core PlatformIO's espressif32 platform installs.
#pragma once

#include <Arduino.h>
#include <driver/i2s.h>
#include <math.h>
#include "pins.h"

class Audio {
public:
    static constexpr uint32_t SAMPLE_RATE = 22050;
    static constexpr i2s_port_t PORT = I2S_NUM_0;

      


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
        cfg.dma_buf_count = 8;
        cfg.dma_buf_len = 256;
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
    // here because tone() writes identical samples to both slots.
    //
    // Powering the amp up mid-silence still thumps a little, so instead of
    // toggling per beep we enable on demand and drop back to shutdown a short
    // while after the last sound. Back-to-back key presses then share one
    // power-up instead of clicking on every keystroke.

    static constexpr uint32_t AMP_WAKE_MS = 10; // settle time after enable
    static constexpr uint32_t AMP_IDLE_MS = 400; // hold-on after last sound

    static constexpr uint32_t DMA_DRAIN_MS = (8 * 256 * 1000) / SAMPLE_RATE + 5;

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

    // Call every loop. Handles the deferred shutdown.
    void update() {
        
        constexpr size_t CHUNK = 256;
        int16_t buf[CHUNK * 2]; // interleaved L/R


        //Generates a chunk and stores it in buffer if playing_ is true. If the note has finished playing, it sets playing_ to false.
        if (playing_) {
           

            const size_t n = min<size_t>(CHUNK, noteFrames_ - framesCompleted_);
            for (size_t i = 0; i < n; i++) {
                // Attack/release envelope so the beep does not click at the edges.
                const float pos = float(framesCompleted_ + i) / float(noteFrames_);
                float env = 1.0f;
                if (pos < 0.05f) env = pos / 0.05f;
                else if (pos > 0.90f) env = (1.0f - pos) / 0.10f;

                const int16_t s = int16_t(sinf(phase_) * amplitude_ * env * 32767.0f);
                buf[i * 2] = s;
                buf[i * 2 + 1] = s;
                phase_ += phaseStep_;
                if (phase_ > 2.0f * PI) phase_ -= 2.0f * PI;
            }


            if (framesCompleted_ >= noteFrames_) { 
                muteAt_ = millis() + DMA_DRAIN_MS + AMP_IDLE_MS;
                playing_ = false;
            }

        }
#ifdef AUDIO_SD_CONNECTED
        if (muteAt_ && (int32_t)(millis() - muteAt_) >= 0) ampDisable();
#endif
    }

    bool ampIsOn() const { return ampOn_; }

    uint32_t calculateNoteFrames(uint32_t durationMs) const {
        return (durationMs * SAMPLE_RATE) / 1000;
    }

    void tone(float freqHz, uint32_t durationMs) {
        if (!ok_) return;
        ampEnable();
        playing_ = true;

        phaseStep_ = (2.0f * PI * freqHz) / float(SAMPLE_RATE);
        noteFrames_ = calculateNoteFrames(durationMs);
        framesCompleted_ = 0;
        phase_ = 0.0f;
    }

    void bootChime() {
        tone(523.25f, 90); // C5
        tone(659.25f, 90); // E5
        tone(783.99f, 140); // G5
    }

    bool ready() const { return ok_; }

private:
    bool ok_ = false;
    bool ampOn_ = false;
    uint32_t muteAt_ = 0; // 0 = no pending shutdown

    //Moved from tone() so tone() is non-blocking and can be called from loop() without cutting off the previous tone.
    bool playing_ = false;
    float phase_ = 0.0f; // Where we are in the sine wave cycle
    float phaseStep_ = 0.0f; //How much to advance the phase each sample
    float amplitude_ = 0.25f; // Volume of the tone (0.0 to 1.0)
    uint32_t noteFrames_ = 0; // How many frames to play for the current note
    uint32_t framesCompleted_ = 0; // How many frames have been played for the current note

};
