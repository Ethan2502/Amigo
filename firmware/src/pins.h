// pins.h -- Amigo GPIO map, transcribed from the ESP32-S3-WROOM-1 schematic sheet.
#pragma once

// Status LED
constexpr int PIN_LED_STATUS = 2;

// Battery sense (ADC1_CH3, safe to use with WiFi active)
constexpr int PIN_BAT_SENSE = 4;

// MAX98357A I2S audio
constexpr int PIN_AUDIO_BCLK = 5;
constexpr int PIN_AUDIO_LRC = 6;
constexpr int PIN_AUDIO_DOUT = 7;

// MAX98357A SD_MODE. Not routed on rev A: this is the bodge wire from J4 pin 5
// to the IO38 castellated pad. Comment out AUDIO_SD_CONNECTED to build for a
// board without the wire, in which case the amp is always on.
#define AUDIO_SD_CONNECTED 1
constexpr int PIN_AUDIO_SD = 38;

// ILI9341 2.8" SPI display
constexpr int PIN_LCD_RST = 8;
constexpr int PIN_LCD_BL = 9;
constexpr int PIN_LCD_SCK = 10;
constexpr int PIN_LCD_MOSI = 11;
constexpr int PIN_LCD_CS = 12;
constexpr int PIN_LCD_DC = 13;

// Kailh Choc 3x3 matrix
constexpr int PIN_ROW[3] = {14, 15, 16};
constexpr int PIN_COL[3] = {17, 18, 21};

// Backlight PWM (LEDC)
constexpr int BL_LEDC_CHANNEL = 0;
constexpr int BL_LEDC_FREQ_HZ = 5000;
constexpr int BL_LEDC_RES_BITS = 8;

// Battery divider: R4 100k (BAT_SW to tap) / R5 100k (tap to GND), tap on IO4,
// filtered by C9 100nF. Ratio = (R4 + R5) / R5 = 2.0. Confirmed from schematic.
constexpr float BAT_DIVIDER_RATIO = 2.0f;
