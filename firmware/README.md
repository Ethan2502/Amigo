# Amigo firmware

PlatformIO project for the ESP32-S3-WROOM-1 (N16R8) main board.

```sh
pio run                 # build
pio run -t upload       # flash over USB-C
pio device monitor      # serial at 115200
```

The board definition is the stock `esp32-s3-devkitc-1`, which is the N8
variant. `platformio.ini` overrides flash size and memory type for N16R8, so
do not drop those lines or you lose 8MB of flash and all of the PSRAM.

## Modules

| File | What it does |
| --- | --- |
| `src/pins.h` | GPIO map, transcribed from the schematic |
| `src/keypad.h` | 3x3 Choc matrix scan with per-key debounce |
| `src/audio.h` | Non-blocking I2S tone and melody output to the MAX98357A |
| `src/alarm_timer.h` | Time of day, daily alarm, countdown timer |
| `src/main.cpp` | Boot, display, mode UI, event loop |

## Keys

Nine keys are shared across four modes. **Key 9 always cycles the mode.**
While an alert is sounding, any key silences it and does nothing else.

| Mode | 1 | 2 | 3 | 4 | 5 | 6 |
| --- | --- | --- | --- | --- | --- | --- |
| KEYPAD | tone | tone | tone | tone | tone | tone |
| CLOCK | hour + | hour - | min + | min - | zero seconds | |
| ALARM | hour + | hour - | min + | min - | enable/disable | |
| TIMER | +1 min | -1 min | +10 s | -10 s | start/pause | reset |

In KEYPAD mode keys 1-8 play the pentatonic bring-up tones.

## Clock accuracy

There is no RTC on the board. The clock is a `millis()` offset seeded by the
user in CLOCK mode, so:

- it starts at **00:00 on every power cycle** and has to be set again,
- it drifts with the ESP32 crystal, roughly a few seconds per day.

The alarm hour, alarm on/off state, and the timer duration **do** survive a
reboot — they live in NVS via `Preferences` under the `amigo` namespace. The
time of day does not. Wiring up SNTP over WiFi, or an external RTC, is the
fix; neither is done yet.

## Audio notes

`Audio::update()` must be called every `loop()`. It is what feeds the I2S DMA:
`tone()` and `play()` only set up state and return immediately, so a key press
during a beep is never dropped. `i2s_write` is called with a zero timeout, so
a long note is spread across many loop passes instead of stalling the scan.

The MAX98357A `SD_MODE` pin is **not routed on rev A** — it is a bodge wire
from J4 pin 5 to the IO38 pad. Comment out `AUDIO_SD_CONNECTED` in `pins.h` to
build for a board without the wire; the amp is then always on and idles with a
little hiss.

Because `SD_MODE` floats until `Audio::begin()` runs, expect a small pop at
power-on. Firmware can shorten that but not remove it.
