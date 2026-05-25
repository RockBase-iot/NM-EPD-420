#pragma once
// T5 - LMD4737 PDM DMIC record + speaker playback test.
//
// Sequence:
//   1. Init I2S RX + ES8311 DMIC capture.
//   2. Record 5s @ 15.625 kHz / 16-bit mono into PSRAM (~156 KB).
//   3. Show RMS / peak.
//   4. Tear down RX, init I2S TX + ES8311 DAC, raise PA_CTRL.
//   5. Play back the recorded buffer (mono -> stereo).
//   6. Lower PA_CTRL, free buffer, wait for USER/BOOT verdict.
//
// Path: LMD4737 (PDM) -> ES8311 DMIC -> PCM -> I2S DIN (GPIO16).
//       Playback: ESP I2S DOUT (GPIO18) -> ES8311 DAC -> NS4150B PA -> speaker.

#include "test_runner.h"
#include "config.h"
#include <Wire.h>
#include <driver/i2s_std.h>
#include <driver/gpio.h>
#include <esp_heap_caps.h>
#include <math.h>

#define T5_DEBUG 1
#if T5_DEBUG
#define T5_LOG(fmt, ...) Serial.printf("[T5] " fmt "\n", ##__VA_ARGS__)
#else
#define T5_LOG(fmt, ...)
#endif

#ifndef ES8311_ADDR
#define ES8311_ADDR 0x18
#endif

// Migrated to i2s_std (IDF 5.x) - the new driver computes MCLK/BCLK
// dividers correctly on ESP32-S3.
//
// LMD4737 PDM clock spec is 1.0-3.5 MHz. Hardware route 1: PDM clock pin
// shares the I2S BCLK net, so PDM_CLK = BCLK = Fs * 16 * 2 (16-bit stereo).
// To stay in spec we MUST run Fs >= ~31.25 kHz.
//
// At Fs=32 kHz: BCLK = 1.024 MHz, MCLK = 8.192 MHz. ES8311 register config
// (LRCK_DIV=256, ADC_OSR=0x03) is MCLK/Fs ratio based, so it auto-scales.
#define T5_SAMPLE_RATE   32000
#define T5_BUF_SAMPLES   512
#define T5_RECORD_SEC    5

// I2S channel handle (single channel, reused for RX then TX)
static i2s_chan_handle_t _t5_rx_handle = nullptr;
static i2s_chan_handle_t _t5_tx_handle = nullptr;

// --- ES8311 helpers ---------------------------------------------------------
static uint8_t _t5_es8311_write(uint8_t reg, uint8_t val) {
    Wire.beginTransmission(ES8311_ADDR);
    Wire.write(reg);
    Wire.write(val);
    return Wire.endTransmission();
}

// Configure ES8311 for DMIC input + I2S ADC output.
static void _t5_es8311_init_record() {
    T5_LOG("ES8311 record init start");

    _t5_es8311_write(0x44, 0x08);
    _t5_es8311_write(0x44, 0x08);

    _t5_es8311_write(0x01, 0x30);
    _t5_es8311_write(0x02, 0x00);
    _t5_es8311_write(0x03, 0x10);
    _t5_es8311_write(0x16, 0x24);
    _t5_es8311_write(0x04, 0x20);
    _t5_es8311_write(0x05, 0x00);
    _t5_es8311_write(0x0B, 0x00);
    _t5_es8311_write(0x0C, 0x00);
    _t5_es8311_write(0x10, 0x1F);
    _t5_es8311_write(0x11, 0x7F);

    _t5_es8311_write(0x00, 0x80);
    _t5_es8311_write(0x01, 0x3F);

    _t5_es8311_write(0x07, 0x00);
    _t5_es8311_write(0x08, 0xFF);
    _t5_es8311_write(0x06, 0x03);

    _t5_es8311_write(0x09, 0x0C);
    _t5_es8311_write(0x0A, 0x0C);

    _t5_es8311_write(0x13, 0x10);
    _t5_es8311_write(0x1B, 0x0A);
    _t5_es8311_write(0x1C, 0x6A);
    _t5_es8311_write(0x44, 0x58);   // ADC -> I2S SDP

    // DMIC mode ON (bit7=1) + DMIC_POL=1 (sample on falling edge of PDM_CLK)
    // + PGA gain (low 4 bits, 3 dB/step, 0..10 -> 0..30 dB).
    // POL=0 yielded only noise even with proper BCLK; POL=1 matches LMD4737.
    // Tweak T5_DMIC_PGA to find the best gain for your environment.
    #ifndef T5_DMIC_PGA
    #define T5_DMIC_PGA 0xA   // 0..0xA -> 0/3/6/.../30 dB
    #endif
    _t5_es8311_write(0x14, 0xC0 | (T5_DMIC_PGA & 0x0F));
    T5_LOG("DMIC PGA=%d dB (reg 0x14=0x%02X)",
           (int)((T5_DMIC_PGA & 0x0F) * 3),
           0xC0 | (T5_DMIC_PGA & 0x0F));

    _t5_es8311_write(0x17, 0xFF);   // ADC digital volume max
    _t5_es8311_write(0x0E, 0x02);
    _t5_es8311_write(0x12, 0x00);
    _t5_es8311_write(0x0D, 0x01);
    _t5_es8311_write(0x15, 0x40);
    _t5_es8311_write(0x37, 0x08);

    T5_LOG("ES8311 record init done");
}

// Wake ES8311 from power-down and enter record mode.
static void _t5_es8311_wakeup_record() {
    T5_LOG("ES8311 wakeup -> record mode");
    _t5_es8311_init_record();
}

// Same playback sequence as T4 (kept local to avoid coupling to test_t4_codec.h).
static void _t5_es8311_init_playback() {
    T5_LOG("ES8311 playback init start");
    _t5_es8311_write(0x44, 0x08);
    _t5_es8311_write(0x44, 0x08);

    _t5_es8311_write(0x01, 0x30);
    _t5_es8311_write(0x02, 0x00);
    _t5_es8311_write(0x03, 0x10);
    _t5_es8311_write(0x16, 0x24);
    _t5_es8311_write(0x04, 0x20);
    _t5_es8311_write(0x05, 0x00);
    _t5_es8311_write(0x0B, 0x00);
    _t5_es8311_write(0x0C, 0x00);
    _t5_es8311_write(0x10, 0x1F);
    _t5_es8311_write(0x11, 0x7F);

    _t5_es8311_write(0x00, 0x80);
    _t5_es8311_write(0x01, 0x3F);

    _t5_es8311_write(0x07, 0x00);
    _t5_es8311_write(0x08, 0xFF);
    _t5_es8311_write(0x06, 0x03);

    _t5_es8311_write(0x09, 0x0C);
    _t5_es8311_write(0x0A, 0x0C);

    _t5_es8311_write(0x13, 0x10);
    _t5_es8311_write(0x1B, 0x0A);
    _t5_es8311_write(0x1C, 0x6A);
    _t5_es8311_write(0x44, 0x58);

    _t5_es8311_write(0x17, 0xBF);
    _t5_es8311_write(0x0E, 0x02);
    _t5_es8311_write(0x12, 0x00);
    _t5_es8311_write(0x14, 0x1A);
    _t5_es8311_write(0x0D, 0x01);
    _t5_es8311_write(0x15, 0x40);
    _t5_es8311_write(0x37, 0x08);
    _t5_es8311_write(0x45, 0x00);
    _t5_es8311_write(0x32, 0xBF);
    _t5_es8311_write(0x31, 0x00);
    T5_LOG("ES8311 playback init done");
}

// Wake ES8311 from power-down and enter playback mode.
static void _t5_es8311_wakeup_playback() {
    T5_LOG("ES8311 wakeup -> playback mode");
    _t5_es8311_init_playback();
}

// Enter ES8311 suspend/power-down mode.
// Sequence aligned with Espressif ES8311 driver suspend flow.
static void _t5_es8311_enter_powerdown() {
    T5_LOG("ES8311 enter power-down");
    _t5_es8311_write(0x32, 0x00);  // DAC volume to 0 before power-off
    _t5_es8311_write(0x17, 0x00);  // ADC digital volume to 0
    _t5_es8311_write(0x0E, 0xFF);  // Power down system blocks
    _t5_es8311_write(0x12, 0x02);  // Clock/path gate
    _t5_es8311_write(0x14, 0x00);  // Disable ADC/DMIC path
    _t5_es8311_write(0x0D, 0xFA);  // System power-down control
    _t5_es8311_write(0x15, 0x00);  // Disable DAC path
    _t5_es8311_write(0x45, 0x01);  // Enter low-power state
}

static bool _t5_i2s_init_rx() {
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    // 8 descriptors x 1024 frames = 8192 frames = 512 ms of buffering at 16kHz.
    // Generous buffering so EPD partial refresh stalls don't overrun DMA.
    chan_cfg.dma_desc_num  = 8;
    chan_cfg.dma_frame_num = 1024;
    esp_err_t e = i2s_new_channel(&chan_cfg, nullptr, &_t5_rx_handle);
    T5_LOG("i2s_new_channel(RX) => %d", (int)e);
    if (e != ESP_OK) return false;

    i2s_std_slot_config_t slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
        I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);

    i2s_std_config_t std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(T5_SAMPLE_RATE),
        .slot_cfg = slot_cfg,
        .gpio_cfg = {
            .mclk = (gpio_num_t)PIN_I2S_MCLK,
            .bclk = (gpio_num_t)PIN_I2S_BCLK,
            .ws   = (gpio_num_t)PIN_I2S_LRCK,
            .dout = I2S_GPIO_UNUSED,
            .din  = (gpio_num_t)PIN_I2S_DIN,
            .invert_flags = { .mclk_inv = false, .bclk_inv = false, .ws_inv = false },
        },
    };
    e = i2s_channel_init_std_mode(_t5_rx_handle, &std_cfg);
    T5_LOG("i2s_channel_init_std_mode(RX) => %d (din=%d mclk=%d 16bit slot)",
           (int)e, PIN_I2S_DIN, PIN_I2S_MCLK);
    if (e != ESP_OK) return false;

    e = i2s_channel_enable(_t5_rx_handle);
    T5_LOG("i2s_channel_enable(RX) => %d", (int)e);
    if (e != ESP_OK) return false;

    T5_LOG("i2s_std RX up: Fs=%uHz MCLK=%uHz BCLK=%uHz",
           (unsigned)T5_SAMPLE_RATE,
           (unsigned)(T5_SAMPLE_RATE * 256U),
           (unsigned)(T5_SAMPLE_RATE * 32U));
    return true;
}

static void _t5_i2s_deinit_rx() {
    if (_t5_rx_handle) {
        i2s_channel_disable(_t5_rx_handle);
        i2s_del_channel(_t5_rx_handle);
        _t5_rx_handle = nullptr;
    }
}

static bool _t5_i2s_init_tx() {
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num  = 4;
    chan_cfg.dma_frame_num = T5_BUF_SAMPLES;
    chan_cfg.auto_clear    = true;
    esp_err_t e = i2s_new_channel(&chan_cfg, &_t5_tx_handle, nullptr);
    T5_LOG("i2s_new_channel(TX) => %d", (int)e);
    if (e != ESP_OK) return false;

    i2s_std_config_t std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(T5_SAMPLE_RATE),
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
    e = i2s_channel_init_std_mode(_t5_tx_handle, &std_cfg);
    T5_LOG("i2s_channel_init_std_mode(TX) => %d (dout=%d mclk=%d)", (int)e, PIN_I2S_DOUT, PIN_I2S_MCLK);
    if (e != ESP_OK) return false;

    e = i2s_channel_enable(_t5_tx_handle);
    T5_LOG("i2s_channel_enable(TX) => %d", (int)e);
    if (e != ESP_OK) return false;

    T5_LOG("i2s_std TX up: Fs=%uHz MCLK=GPIO%d (Fs*256=%uHz)",
           (unsigned)T5_SAMPLE_RATE, PIN_I2S_MCLK,
           (unsigned)(T5_SAMPLE_RATE * 256U));
    return true;
}

static void _t5_i2s_deinit_tx() {
    if (_t5_tx_handle) {
        i2s_channel_disable(_t5_tx_handle);
        i2s_del_channel(_t5_tx_handle);
        _t5_tx_handle = nullptr;
    }
}

inline TestResult runTestT5(Display& disp, TestRunner& runner) {
    T5_LOG("START");

    // Enable ES8311 codec chip (T4 disabled it at the end of its test).
    pinMode(PIN_CODEC_EN, OUTPUT);
    digitalWrite(PIN_CODEC_EN, HIGH);
    delay(10);  // allow codec to power up before I2C probe

    pinMode(PIN_PA_CTRL, OUTPUT);
    digitalWrite(PIN_PA_CTRL, LOW);

    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);

    Wire.beginTransmission(ES8311_ADDR);
    bool i2cOk = (Wire.endTransmission() == 0);
    T5_LOG("I2C probe 0x18 ok=%d", (int)i2cOk);
    if (!i2cOk) {
        const char* lines[] = { "I2C probe(0x18) FAIL" };
        disp.showTestScreen(5, "DMIC Record+Playback", lines, 1, "FAIL", "USER=Next");
        runner.waitForUser();
        return TestResult::FAIL;
    }

    // --- Allocate PSRAM mono buffer (5s x 15625 x 2B = ~156 KB) ------------
    const uint32_t totalSamples = (uint32_t)T5_SAMPLE_RATE * T5_RECORD_SEC;
    const size_t   bufBytes     = totalSamples * sizeof(int16_t);
    int16_t* monoBuf = (int16_t*)heap_caps_malloc(bufBytes, MALLOC_CAP_SPIRAM);
    if (!monoBuf) {
        // Fallback to internal RAM (will likely fail at this size).
        monoBuf = (int16_t*)heap_caps_malloc(bufBytes, MALLOC_CAP_8BIT);
    }
    T5_LOG("alloc monoBuf=%p bytes=%u", monoBuf, (unsigned)bufBytes);
    if (!monoBuf) {
        const char* lines[] = { "Out of memory", "Need PSRAM enabled" };
        disp.showTestScreen(5, "DMIC Record+Playback", lines, 2, "FAIL", "USER=Next");
        runner.waitForUser();
        return TestResult::FAIL;
    }

    // --- Phase 1: Record ----------------------------------------------------
    // [DEBUG] Loop record+playback until operator confirms PASS (USER).
    //         Pressing BOOT on the verdict screen retries the cycle.
    bool verdict = false;
    uint32_t attempt = 0;
    do {
        attempt++;
        T5_LOG("=== attempt #%u ===", (unsigned)attempt);

    if (!_t5_i2s_init_rx()) {
        free(monoBuf);
        const char* lines[] = { "I2S RX init FAILED" };
        disp.showTestScreen(5, "DMIC Record+Playback", lines, 1, "FAIL", "USER=Next");
        runner.waitForUser();
        return TestResult::FAIL;
    }
    delay(50);
    _t5_es8311_wakeup_record();
    delay(100);

    // Discard ~50 ms of warm-up samples.
    // 16-bit slot, stereo => 4 bytes/frame. Buffers are static to keep them
    // off the loopTask stack (~8 KB).
    {
        static int16_t warm[T5_BUF_SAMPLES * 2];
        size_t br = 0;
        for (int i = 0; i < 4; i++) {
            uint32_t t0 = millis();
            i2s_channel_read(_t5_rx_handle, warm, sizeof(warm), &br, portMAX_DELAY);
            T5_LOG("warm[%d] br=%u took=%ums", i, (unsigned)br, (unsigned)(millis()-t0));
        }
    }

    // Show static "recording..." screen ONCE before we start - any UI update
    // during the read loop blocks for hundreds of ms (EPD partial refresh)
    // and overruns the I2S DMA buffer, producing audible gaps / hiss.
    {
        char l0[32];
        snprintf(l0, sizeof(l0), "Attempt #%u", (unsigned)attempt);
        const char* lines[] = { l0, "Speak / sing for 5s", "",
                                 "Recording...",
                                 "(do NOT press anything)" };
        disp.showTestScreen(5, "DMIC Record", lines, 5, nullptr, nullptr);
    }

    static int16_t stereoBuf[T5_BUF_SAMPLES * 2];   // 16-bit slots, stereo (~4 KB - keep off stack)
    uint32_t  recorded = 0;
    uint64_t  sumSq    = 0;
    int16_t   peak     = 0;
    bool      earlyAbort   = false;
    bool      earlyVerdict = false;

    T5_LOG("Recording %us @ %uHz...", (unsigned)T5_RECORD_SEC, (unsigned)T5_SAMPLE_RATE);

    uint32_t recStartMs = millis();
    uint32_t readCalls  = 0;
    uint32_t readBytes  = 0;
    uint32_t lastDiagMs = recStartMs;

    while (recorded < totalSamples && !earlyAbort) {
        size_t br = 0;
        esp_err_t e = i2s_channel_read(_t5_rx_handle, stereoBuf, sizeof(stereoBuf),
                                       &br, pdMS_TO_TICKS(200));
        readCalls++;
        readBytes += br;
        if (e != ESP_OK) {
            T5_LOG("i2s_read err=%d br=%u", (int)e, (unsigned)br);
            continue;
        }
        if (br == 0) {
            T5_LOG("i2s_read TIMEOUT br=0 (codec not clocking?)");
            continue;
        }
        // 16-bit slot, stereo -> 4 B/frame.
        size_t frames = br / 4;
        if (recorded + frames > totalSamples) frames = totalSamples - recorded;

        for (size_t i = 0; i < frames; i++) {
            int32_t l = stereoBuf[i*2];
            int32_t r = stereoBuf[i*2 + 1];
            int32_t m = (l + r) / 2;
            monoBuf[recorded + i] = (int16_t)m;
            sumSq += (uint64_t)((int64_t)m * (int64_t)m);
            int16_t a = (m < 0) ? (int16_t)-m : (int16_t)m;
            if (a > peak) peak = a;
        }
        recorded += frames;

        uint32_t now = millis();
        if (now - lastDiagMs >= 1000) {
            uint32_t dt = now - lastDiagMs;
            T5_LOG("diag: t=%ums recorded=%u calls=%u bytes=%u rate~=%uHz",
                   (unsigned)(now-recStartMs), (unsigned)recorded,
                   (unsigned)readCalls, (unsigned)readBytes,
                   (unsigned)((readBytes / 4) * 1000U / dt));
            lastDiagMs = now;
            readCalls = 0;
            readBytes = 0;
        }
        // NO UI updates / button polling here - they would stall the read loop
        // (EPD refresh ~300 ms) and overrun the I2S DMA buffer.
    }

    uint32_t rms = (recorded > 0)
                    ? (uint32_t)sqrt((double)(sumSq / recorded))
                    : 0;
    T5_LOG("Recording done: samples=%u rms=%u peak=%d",
           (unsigned)recorded, (unsigned)rms, (int)peak);

    _t5_i2s_deinit_rx();

    if (earlyAbort) {
        // BOOT during record: retry instead of returning.
        T5_LOG("aborted by BOOT during record - retrying");
        (void)earlyVerdict;
        continue;
    }

    // --- Phase 2: Playback --------------------------------------------------
    if (!_t5_i2s_init_tx()) {
        _t5_es8311_enter_powerdown();
        free(monoBuf);
        const char* lines[] = { "I2S TX init FAILED" };
        disp.showTestScreen(5, "DMIC Playback", lines, 1, "FAIL", "USER=Next");
        runner.waitForUser();
        return TestResult::FAIL;
    }
    delay(50);
    _t5_es8311_wakeup_playback();
    delay(50);

    digitalWrite(PIN_PA_CTRL, HIGH);
    delay(80);

    {
        char l0[32], l1[32];
        snprintf(l0, sizeof(l0), "Attempt #%u", (unsigned)attempt);
        snprintf(l1, sizeof(l1), "RMS=%u  Peak=%d",
                 (unsigned)rms, (int)peak);
        const char* lines[] = { l0,
                                "Playing back recording...",
                                "(speaker should reproduce voice)",
                                "",
                                l1,
                                "BOOT=stop early" };
        disp.showTestScreen(5, "DMIC Playback", lines, 6, nullptr, nullptr);
    }

    T5_LOG("Playing back %u samples...", (unsigned)totalSamples);
    static int16_t txBuf[T5_BUF_SAMPLES * 2];
    uint32_t played = 0;
    bool     pbAbort = false;
    while (played < totalSamples && !pbAbort) {
        uint32_t chunk = T5_BUF_SAMPLES;
        if (played + chunk > totalSamples) chunk = totalSamples - played;
        for (uint32_t i = 0; i < chunk; i++) {
            int16_t s = monoBuf[played + i];
            txBuf[i*2]     = s;
            txBuf[i*2 + 1] = s;
        }
        size_t bw = 0;
        i2s_channel_write(_t5_tx_handle, txBuf, chunk * 4, &bw, portMAX_DELAY);
        played += chunk;

        if (digitalRead(PIN_BOOT_BTN) == LOW) {
            delay(50); while (digitalRead(PIN_BOOT_BTN) == LOW) {}
            pbAbort = true;
        }
    }
    T5_LOG("Playback done (played=%u abort=%d)",
           (unsigned)played, (int)pbAbort);

    // Tail flush so DMA empties before we kill the clocks.
    delay(80);
    digitalWrite(PIN_PA_CTRL, LOW);
    _t5_i2s_deinit_tx();
    _t5_es8311_enter_powerdown();

    // --- Verdict screen -----------------------------------------------------
    bool autoPass = (rms > DMIC_RMS_THRESHOLD_VOICE);
    {
        char l0[32], l1[32], l2[32], l3[32];
        snprintf(l0, sizeof(l0), "Attempt #%u  Auto:%s",
                 (unsigned)attempt, autoPass ? "PASS" : "FAIL");
        snprintf(l1, sizeof(l1), "RMS=%u (thr=%u)",
                 (unsigned)rms, (unsigned)DMIC_RMS_THRESHOLD_VOICE);
        snprintf(l2, sizeof(l2), "Peak=%d  Samples=%u",
                 (int)peak, (unsigned)recorded);
        snprintf(l3, sizeof(l3), "USER=PASS  BOOT=retry");
        const char* lines[] = { "Did you hear yourself?",
                                l0, l1, l2, "",
                                l3 };
        disp.showTestScreen(5, "DMIC Result", lines, 6,
                            autoPass ? "PASS" : "FAIL",
                            "USER/BOOT");
    }
    verdict = runner.waitForVerdict();
    T5_LOG("verdict=%s rms=%u (attempt=%u)",
           verdict ? "PASS" : "FAIL/RETRY", (unsigned)rms, (unsigned)attempt);
    } while (!verdict);

    free(monoBuf);
    T5_LOG("END verdict=PASS attempts=%u", (unsigned)attempt);

    // Disable ES8311 codec chip.
    digitalWrite(PIN_CODEC_EN, LOW);

    return TestResult::PASS;
}

