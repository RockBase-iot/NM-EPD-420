#pragma once
// T4 鈥?ES8311 CODEC test
//   T4-A: I2C probe at 0x18 (auto pass/fail)
//   T4-B: I2S 1kHz sine sweep playback via PA (human listen confirmation)
//
// IDF 4.4 / Arduino-ESP32 stock SDK has a known bug on ESP32-S3 where
// i2s_set_pin cannot route MCLK through the GPIO matrix. As a workaround,
// we generate MCLK = 4.000 MHz on GPIO21 with LEDC, and tell I2S to NOT
// touch GPIO21 (mck_io_num = NO_CHANGE). Sample rate = 4MHz/256 = 15625 Hz.

#include "test_runner.h"
#include "config.h"
#include <Wire.h>
#include <driver/i2s_std.h>
#include <driver/gpio.h>
#include <math.h>

#define T4_DEBUG 1
#if T4_DEBUG
#define T4_LOG(fmt, ...) Serial.printf("[T4] " fmt "\n", ##__VA_ARGS__)
#else
#define T4_LOG(fmt, ...)
#endif

#define ES8311_ADDR 0x18

static uint8_t _es8311_write(uint8_t reg, uint8_t val) {
    Wire.beginTransmission(ES8311_ADDR);
    Wire.write(reg);
    Wire.write(val);
    uint8_t err = Wire.endTransmission();
    T4_LOG("I2C WR reg=0x%02X val=0x%02X err=%u", reg, val, err);
    return err;
}

static bool _es8311_read(uint8_t reg, uint8_t& outVal) {
    Wire.beginTransmission(ES8311_ADDR);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom((uint8_t)ES8311_ADDR, (uint8_t)1) != 1) return false;
    outVal = Wire.read();
    // T4_LOG("I2C RD reg=0x%02X val=0x%02X", reg, outVal);
    return true;
}

static void _es8311_dump_key_regs() {
    const uint8_t regs[] = {0x00, 0x01, 0x06, 0x07, 0x08, 0x09, 0x0A,
                             0x0D, 0x0E, 0x12, 0x14, 0x31, 0x32};
    for (size_t i = 0; i < sizeof(regs); i++) {
        uint8_t v = 0;
        (void)_es8311_read(regs[i], v);
    }
}

static void _es8311_init_playback() {
    T4_LOG("ES8311 init start");
    _es8311_write(0x44, 0x08);
    _es8311_write(0x44, 0x08);

    _es8311_write(0x01, 0x30);
    _es8311_write(0x02, 0x00);
    _es8311_write(0x03, 0x10);
    _es8311_write(0x16, 0x24);
    _es8311_write(0x04, 0x20);
    _es8311_write(0x05, 0x00);
    _es8311_write(0x0B, 0x00);
    _es8311_write(0x0C, 0x00);
    _es8311_write(0x10, 0x1F);
    _es8311_write(0x11, 0x7F);

    _es8311_write(0x00, 0x80);
    _es8311_write(0x01, 0x3F);

    // MCLK=4.000MHz, FS=15.625kHz 鈫?LRCK_DIV=256
    _es8311_write(0x07, 0x00);
    _es8311_write(0x08, 0xFF);
    _es8311_write(0x06, 0x03);

    _es8311_write(0x09, 0x0C);
    _es8311_write(0x0A, 0x0C);

    _es8311_write(0x13, 0x10);
    _es8311_write(0x1B, 0x0A);
    _es8311_write(0x1C, 0x6A);
    _es8311_write(0x44, 0x58);

    _es8311_write(0x17, 0xBF);
    _es8311_write(0x0E, 0x02);
    _es8311_write(0x12, 0x00);
    _es8311_write(0x14, 0x1A);
    _es8311_write(0x0D, 0x01);
    _es8311_write(0x15, 0x40);
    _es8311_write(0x37, 0x08);
    _es8311_write(0x45, 0x00);
    _es8311_write(0x32, 0xD3);   // ~ -22 dB DAC volume
    _es8311_write(0x31, 0x00);

    T4_LOG("ES8311 init done, dump key regs");
    _es8311_dump_key_regs();
}

// Wake ES8311 from power-down and enter playback mode.
static void _es8311_wakeup_playback() {
    T4_LOG("ES8311 wakeup -> playback mode");
    _es8311_init_playback();
}

// Enter ES8311 suspend/power-down mode.
// Sequence aligned with Espressif ES8311 driver suspend flow.
static void _es8311_enter_powerdown() {
    T4_LOG("ES8311 enter power-down");
    _es8311_write(0x32, 0x00);  // DAC volume to 0 before power-off
    _es8311_write(0x17, 0x00);  // ADC digital volume to 0
    _es8311_write(0x0E, 0xFF);  // Power down system blocks
    _es8311_write(0x12, 0x02);  // Clock/path gate
    _es8311_write(0x14, 0x00);  // Disable ADC/DMIC path
    _es8311_write(0x0D, 0xFA);  // System power-down control
    _es8311_write(0x15, 0x00);  // Disable DAC path
    _es8311_write(0x45, 0x01);  // Enter low-power state
}

#define T4_SAMPLE_RATE  16000
#define T4_BUF_SAMPLES  512

static i2s_chan_handle_t _t4_tx_handle = nullptr;

static bool _t4_i2s_init() {
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num  = 4;
    chan_cfg.dma_frame_num = T4_BUF_SAMPLES;
    chan_cfg.auto_clear    = true;
    esp_err_t e = i2s_new_channel(&chan_cfg, &_t4_tx_handle, nullptr);
    T4_LOG("i2s_new_channel(TX) => %d", (int)e);
    if (e != ESP_OK) return false;

    i2s_std_config_t std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(T4_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = (gpio_num_t)PIN_I2S_MCLK,
            .bclk = (gpio_num_t)PIN_I2S_BCLK,
            .ws   = (gpio_num_t)PIN_I2S_LRCK,
            .dout = (gpio_num_t)PIN_I2S_DOUT,
            .din  = I2S_GPIO_UNUSED,
            .invert_flags = { .mclk_inv = false, .bclk_inv = false, .ws_inv = false },
        },
    };
    e = i2s_channel_init_std_mode(_t4_tx_handle, &std_cfg);
    T4_LOG("i2s_channel_init_std_mode => %d (mclk=%d bclk=%d lrck=%d dout=%d)",
           (int)e, PIN_I2S_MCLK, PIN_I2S_BCLK, PIN_I2S_LRCK, PIN_I2S_DOUT);
    if (e != ESP_OK) return false;

    e = i2s_channel_enable(_t4_tx_handle);
    T4_LOG("i2s_channel_enable(TX) => %d", (int)e);
    if (e != ESP_OK) return false;

    T4_LOG("i2s_std TX up: Fs=%uHz MCLK=GPIO%d (Fs*256=%uHz)",
           (unsigned)T4_SAMPLE_RATE, PIN_I2S_MCLK,
           (unsigned)(T4_SAMPLE_RATE * 256U));
    return true;
}

static void _t4_i2s_deinit() {
    if (_t4_tx_handle) {
        i2s_channel_disable(_t4_tx_handle);
        i2s_del_channel(_t4_tx_handle);
        _t4_tx_handle = nullptr;
    }
}

inline TestResult runTestT4(Display& disp, TestRunner& runner) {
    T4_LOG("START");

    // Enable ES8311 codec chip.
    pinMode(PIN_CODEC_EN, OUTPUT);
    digitalWrite(PIN_CODEC_EN, HIGH);
    delay(10);  // allow codec to power up before I2C probe

    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
    T4_LOG("Wire.begin SDA=%d SCL=%d", PIN_I2C_SDA, PIN_I2C_SCL);

    Wire.beginTransmission(ES8311_ADDR);
    bool i2cOk = (Wire.endTransmission() == 0);
    T4_LOG("I2C probe 0x18 ok=%d", (int)i2cOk);

    {
        char probe[32];
        snprintf(probe, sizeof(probe), "I2C probe(0x18)... %s", i2cOk ? "OK" : "FAIL");
        const char* lines[] = { probe };
        disp.showTestScreen(4, "ES8311 CODEC", lines, 1,
                            i2cOk ? "PASS" : "FAIL",
                            i2cOk ? "USER=Next" : "USER=Skip");
    }
    if (!i2cOk) { runner.waitForUser(); return TestResult::FAIL; }
    runner.waitForUser();

    bool i2sOk = _t4_i2s_init();
    T4_LOG("i2sOk=%d, waiting 50ms", (int)i2sOk);
    delay(50);
    if (i2sOk) _es8311_wakeup_playback();

    pinMode(PIN_PA_CTRL, OUTPUT);
    digitalWrite(PIN_PA_CTRL, HIGH);
    T4_LOG("PA_CTRL=%d -> HIGH (rb=%d)", PIN_PA_CTRL, digitalRead(PIN_PA_CTRL));
    delay(100);

    if (!i2sOk) {
        const char* lerr[] = { "I2S init FAILED" };
        disp.showTestScreen(4, "ES8311 CODEC-Play", lerr, 1, "FAIL", "USER=Next");
        runner.waitForUser();
        _es8311_enter_powerdown();
        return TestResult::FAIL;
    }

    T4_LOG("PLAY: sweep + 'Ode to Joy' melody, USER=PASS BOOT=FAIL");

    // 鈹€鈹€ Sweep frequencies (2 s each, all played under one screen) 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€
    static const float sweepFreqs[] = { 500.0f, 1000.0f, 2000.0f,
                                        3000.0f, 4000.0f };
    constexpr int nSweep = sizeof(sweepFreqs) / sizeof(sweepFreqs[0]);

    // 鈹€鈹€ Beethoven "Ode to Joy" (Symphony No.9, mvt.4 main theme) 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€
    // Key of C major. q 鈮?100 BPM (quarter = 300 ms). Two 8-bar phrases
    // 鈮?19.2 s total 鈥?fits the 20 s window cleanly with no looping.
    // Tones used: C4 D4 E4 F4 G4. No half-tones, easy to recognise globally.
    struct T4Note { float freq; uint16_t ms; };
    constexpr float NOTE_C  = 261.63f;
    constexpr float NOTE_D  = 293.66f;
    constexpr float NOTE_E  = 329.63f;
    constexpr float NOTE_F  = 349.23f;
    constexpr float NOTE_G  = 392.00f;
    constexpr uint16_t Q  = 300;   // quarter note
    constexpr uint16_t DQ = 450;   // dotted quarter
    constexpr uint16_t E8 = 150;   // eighth
    constexpr uint16_t H  = 600;   // half
    static const T4Note odeToJoy[] = {
        // Phrase 1 (8 bars):
        // | E E F G | G F E D | C C D E | E. D D |
        // | E E F G | G F E D | C C D E | D. C C |
        { NOTE_E, Q }, { NOTE_E, Q }, { NOTE_F, Q }, { NOTE_G, Q },
        { NOTE_G, Q }, { NOTE_F, Q }, { NOTE_E, Q }, { NOTE_D, Q },
        { NOTE_C, Q }, { NOTE_C, Q }, { NOTE_D, Q }, { NOTE_E, Q },
        { NOTE_E, DQ },{ NOTE_D, E8 },{ NOTE_D, H },

        { NOTE_E, Q }, { NOTE_E, Q }, { NOTE_F, Q }, { NOTE_G, Q },
        { NOTE_G, Q }, { NOTE_F, Q }, { NOTE_E, Q }, { NOTE_D, Q },
        { NOTE_C, Q }, { NOTE_C, Q }, { NOTE_D, Q }, { NOTE_E, Q },
        { NOTE_D, DQ },{ NOTE_C, E8 },{ NOTE_C, H },
    };
    constexpr int nMelody = sizeof(odeToJoy) / sizeof(odeToJoy[0]);
    constexpr uint32_t MELODY_TOTAL_MS = 20000;

    bool     exitTest = false;
    bool     verdict  = false;
    static int16_t  buf[T4_BUF_SAMPLES * 2];
    static uint32_t phase = 0;

    // Inline tone generator: plays `freq` for `durMs` ms (freq=0 -> silence).
    // Polls USER/BOOT each chunk so we can break out promptly.
    auto playTone = [&](float freq, uint32_t durMs, const char* tag) {
        uint64_t total64 = (uint64_t)T4_SAMPLE_RATE * (uint64_t)durMs / 1000ULL;
        uint32_t total   = (uint32_t)total64;
        // Linear attack/release ramp lengths (samples). Removes the wideband
        // click/pop that otherwise occurs at every note boundary because the
        // sine is cut at a non-zero phase. 8 ms attack and 25 ms release are
        // short enough to keep the tone crisp but long enough to be silent
        // at audible frequencies.
        const uint32_t attackN  = (T4_SAMPLE_RATE *  8) / 1000;
        const uint32_t releaseN = (T4_SAMPLE_RATE * 25) / 1000;
        uint32_t written = 0;
        const float twoPiFOverFs = 2.0f * (float)M_PI * freq / (float)T4_SAMPLE_RATE;
        while (written < total && !exitTest) {
            uint32_t chunk = T4_BUF_SAMPLES;
            if (written + chunk > total) chunk = total - written;
            if (freq < 0.5f) {
                memset(buf, 0, chunk * 4);
            } else {
                for (uint32_t i = 0; i < chunk; i++) {
                    uint32_t n = written + i;            // sample index in note
                    float env = 1.0f;
                    if (n < attackN) {
                        env = (float)n / (float)attackN;
                    } else if (n + releaseN > total) {
                        uint32_t left = total - n;       // samples remaining
                        env = (float)left / (float)releaseN;
                        if (env < 0.0f) env = 0.0f;
                    }
                    int16_t s = (int16_t)(16000.0f * env *
                                          sinf(twoPiFOverFs * (float)phase));
                    phase++;
                    buf[i * 2] = s; buf[i * 2 + 1] = s;
                }
            }
            size_t bw = 0;
            i2s_channel_write(_t4_tx_handle, buf, chunk * 4, &bw, portMAX_DELAY);
            written += chunk;
            if (digitalRead(PIN_USER_BTN) == LOW) {
                delay(50); while (digitalRead(PIN_USER_BTN) == LOW) {}
                exitTest = true; verdict = true;
            } else if (digitalRead(PIN_BOOT_BTN) == LOW) {
                delay(50); while (digitalRead(PIN_BOOT_BTN) == LOW) {}
                exitTest = true; verdict = false;
            }
        }
        // Reset oscillator phase between notes so the next note's attack
        // ramp starts at sin(0) = 0 and the falling edge of the release
        // ramp also lands near 0 鈥?keeps the join free of DC steps.
        phase = 0;
        (void)tag;
    };

    while (!exitTest) {
        // 鈹€鈹€ Phase A: sweep through tone list under a single screen 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€
        // We render the sweep page once and then play every frequency back
        // to back. Re-rendering the EPD between tones takes ~10 s per refresh
        // and stalls the audio, which defeats the point of a sweep.
        {
            const char* lines[] = { "PA Amp: ON",
                                    "Sweep: 500 Hz - 4 kHz",
                                    "Phase: SWEEP",
                                    "USER=PASS  BOOT=FAIL" };
            disp.showTestScreen(4, "CODEC SWEEP+MELODY", lines, 4, nullptr, nullptr);
        }
        for (int i = 0; i < nSweep && !exitTest; i++) {
            T4_LOG("sweep freq=%.0fHz", sweepFreqs[i]);
            playTone(sweepFreqs[i], 1000, "sweep");
        }

        // 鈹€鈹€ Phase B: 'Ode to Joy' for 20 s 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€
        {
            const char* lines[] = { "PA Amp: ON",
                                    "Tune: Ode to Joy",
                                    "Phase: MELODY (20s)",
                                    "USER=PASS  BOOT=FAIL" };
            disp.showTestScreen(4, "CODEC SWEEP+MELODY", lines, 4, nullptr, nullptr);
            T4_LOG("ode-to-joy melody start");
            uint32_t t0 = millis();
            int      ni = 0;
            while (!exitTest && (millis() - t0) < MELODY_TOTAL_MS) {
                const T4Note& n = odeToJoy[ni];
                playTone(n.freq, n.ms, "melody");
                ni = (ni + 1) % nMelody;
            }
            T4_LOG("ode-to-joy melody end (%lums)",
                   (unsigned long)(millis() - t0));
        }
    }

    digitalWrite(PIN_PA_CTRL, LOW);
    _t4_i2s_deinit();
    _es8311_enter_powerdown();

    // Disable ES8311 codec chip.
    digitalWrite(PIN_CODEC_EN, LOW);

    T4_LOG("END verdict=%s", verdict ? "PASS" : "FAIL");
    return verdict ? TestResult::PASS : TestResult::FAIL;
}
