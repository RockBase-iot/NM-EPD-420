#pragma once

// ─── Firmware version ────────────────────────────────────────────────────────
#define FW_VERSION  "v1.4.02"
#define BOARD_NAME  "NM-EPD-420"

// ─── EPD (SPI0 / VSPI) ───────────────────────────────────────────────────────
#define PIN_EPD_SCK   2
#define PIN_EPD_MOSI  1
#define PIN_EPD_MISO  -1  // EPD is write-only and panel pin is NC; do NOT claim
                          //   a GPIO here — GPIO10 is used by the peripheral
                          //   HSPI bus (SD MOSI), and any overlap leaves the
                          //   FSPI bus in a bad state after SD operations.
#define PIN_EPD_CS   46
#define PIN_EPD_DC    4
#define PIN_EPD_RST   5
#define PIN_EPD_BUSY  6

// ─── SD card + LoRa (FSPI, shared bus) ───────────────────────────────────────
#define PIN_SD_CLK    9
#define PIN_SD_MOSI   10   // CMD
#define PIN_SD_MISO   11   // DAT0
#define PIN_SD_CS     7

#define PIN_LORA_NSS  8    // LoRa chip-select (CS), shares FSPI bus with SD
#define PIN_LORA_RST  12
#define PIN_LORA_BUSY 13
#define PIN_LORA_DIO1 14   // Not used in SPI connectivity test

// ─── I2S (ES8311 CODEC) ──────────────────────────────────────────────────────
// HW net mapping (per schematic): GPIO16=I2S_ASDOUT (ES8311 pin7, ADC->ESP),
//                                  GPIO18=I2S_DSIN  (ES8311 pin9, ESP->DAC)
#define PIN_I2S_MCLK  21
#define PIN_I2S_BCLK  15
#define PIN_I2S_LRCK  17
#define PIN_I2S_DOUT  18   // ESP TX → ES8311 DSDIN (pin 9)
#define PIN_I2S_DIN   16   // ES8311 ASDOUT (pin 7) → ESP RX

// ─── I2C (AHT20 + ES8311 shared bus) ─────────────────────────────────────────
#define PIN_I2C_SDA   39
#define PIN_I2C_SCL   38
#define PIN_TEMP_CTL  40   // AHT20 power enable (HIGH = on)

// ─── GPIO ────────────────────────────────────────────────────────────────────
#define PIN_PA_CTRL   41   // External amplifier enable (HIGH = on)
#define PIN_BOOT_BTN   0   // BOOT key, external pull-up, pressed = LOW (RTC GPIO)
#define PIN_USER_BTN  45   // USER key, external pull-up, pressed = LOW

// ─── Module enable pins (active HIGH) ────────────────────────────────────────
#define PIN_LORA_EN   47   // LoRa module power enable (HIGH = on)
#define PIN_CODEC_EN  44   // ES8311 codec power enable (HIGH = on)
#define PIN_ADC_EN    43   // Battery ADC circuit enable (HIGH = on)

// ─── Battery voltage ADC ─────────────────────────────────────────────────────
#define PIN_BATT_ADC   3   // Battery voltage sense input (via resistor divider)
#define BATT_ADC_DIV   2   // Divider ratio: ADC_mV * BATT_ADC_DIV = battery mV

// ─── EPD refresh tuning ─────────────────────────────────────────────────────
// Full-height page buffer reduces paged transfer overhead on ESP32-S3.
// The selected GDEY042Z98 driver still does full 3-color refresh, so this
// improves latency moderately but cannot remove the panel's inherent delay.
#define EPD_PAGE_HEIGHT           300   // 300 = full buffer, 150 = half buffer
#define EPD_FAST_FULL_UPDATE      1     // 1 = use fast full-refresh waveform

// ─── Feature switches ─────────────────────────────────────────────────────────
#define BATTERY_ADC_AVAILABLE  1   // 1 = test (IO3 + enable IO43)

// ─── DMIC recording thresholds ────────────────────────────────────────────────
#define DMIC_RMS_THRESHOLD_VOICE     150   // Phase-1: operator voice
#define DMIC_RMS_THRESHOLD_LOOPBACK   50   // Phase-2: speaker loopback
