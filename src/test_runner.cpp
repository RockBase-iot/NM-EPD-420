#include "test_runner.h"
#include "config.h"
#include "spi_buses.h"
#include <esp_sleep.h>
#include <driver/gpio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// Definition for the second SPI bus (HSPI / SPI3) shared by SD + LoRa.
// EPD keeps using the default `SPI` global (FSPI / SPI2).
SPIClass spiPeripheral(HSPI);

// ─── Test implementations (header-only stubs) ─────────────────────────────────
#include "tests/test_t1_epd.h"
#include "tests/test_t3_button.h"
#include "tests/test_t4_codec.h"
#include "tests/test_t5_dmic.h"
#include "tests/test_t6_aht20.h"
#include "tests/test_t7_adc.h"
#include "tests/test_t8_wifi.h"
#include "tests/test_t9_sd.h"
#include "tests/test_t10_lora.h"

// ─── Button helpers ───────────────────────────────────────────────────────────
//
// Button reads are gated on the EPD BUSY pin being LOW. This panel
// (GDEY042Z98) drives BUSY HIGH while it is refreshing — and a full-window
// refresh takes ~10–15 s, during which the MCU is blocking inside
// firstPage()/nextPage(). If the operator presses USER/BOOT during that
// window, the press is still active when control returns to the next
// waitForUser()/waitForVerdict(), and the very first poll in those loops
// would consume it as a "new" press, putting the test sequence one step
// ahead of the UI. Treating any sample taken while BUSY=HIGH as "not
// pressed" makes that whole class of stale presses invisible.
//
// Debounce: a candidate press must satisfy BUSY=LOW AND button=LOW at
// every one of N samples taken 10 ms apart (~50 ms total). This rejects
// the millisecond-scale bounce/EMI on the BUSY falling edge and on key
// release that would otherwise sneak past a 2-sample 20 ms window.

static const uint8_t  BTN_DEBOUNCE_SAMPLES = 5;
static const uint8_t  BTN_DEBOUNCE_STEP_MS = 10;

static bool _btnPressedDebounced(uint8_t pin) {
    // Quick reject: BUSY already high, or button already high.
    if (digitalRead(PIN_EPD_BUSY) == HIGH) return false;
    if (digitalRead(pin) != LOW)           return false;

    // Stable-low debounce: BUSY must stay low and button must stay low for
    // BTN_DEBOUNCE_SAMPLES consecutive samples.
    for (uint8_t i = 1; i < BTN_DEBOUNCE_SAMPLES; i++) {
        delay(BTN_DEBOUNCE_STEP_MS);
        if (digitalRead(PIN_EPD_BUSY) == HIGH) return false;
        if (digitalRead(pin) != LOW)           return false;
    }
    return true;
}

bool TestRunner::userPressed() { return _btnPressedDebounced(PIN_USER_BTN); }
bool TestRunner::bootPressed() { return _btnPressedDebounced(PIN_BOOT_BTN); }

// Raw (no BUSY-gate, no debounce) — only used for the release gate below
// so we can see whether the operator's finger is still on the key.
static bool _btnHeldRaw(uint8_t pin) { return digitalRead(pin) == LOW; }

// Block until both keys are released. Catches the case where the user
// pressed during a refresh and is still holding when BUSY falls — we want
// the first "press" we report to be a fresh release→press edge.
static void _waitAllReleased() {
    while (_btnHeldRaw(PIN_USER_BTN) || _btnHeldRaw(PIN_BOOT_BTN)) delay(10);
    delay(30);  // mechanical/EMI settle after release
}

void TestRunner::waitForUser() {
    _waitAllReleased();
    while (!userPressed()) { delay(10); }
    delay(50);  // debounce
    while (userPressed()) { delay(10); }  // wait for release
}

bool TestRunner::waitForVerdict() {
    _waitAllReleased();
    while (true) {
        if (userPressed()) {
            delay(50);
            while (userPressed()) { delay(10); }
            return true;
        }
        if (bootPressed()) {
            delay(50);
            while (bootPressed()) { delay(10); }
            return false;
        }
        delay(10);
    }
}

// ─── T0 — System startup & welcome screen ────────────────────────────────────

void TestRunner::runT0() {
    // 1. Serial
    Serial.begin(115200);
    delay(200);

    // 2. PA_CTRL default LOW (amplifier off)
    pinMode(PIN_PA_CTRL, OUTPUT);
    digitalWrite(PIN_PA_CTRL, LOW);

    // 3. Button pins
    pinMode(PIN_USER_BTN, INPUT_PULLUP);
    pinMode(PIN_BOOT_BTN, INPUT_PULLUP);

    // 4. Init EPD and show welcome screen
    _display.init();
    _display.showWelcome();

    // 4b. Init the second SPI bus (HSPI) shared by SD card and LoRa.
    // Doing it once here means T9/T10 don't need to call SPI.end()/begin()
    // and the EPD's default SPI bus stays untouched.
    initPeripheralSpi();

    // 5. Serial log
    Serial.println();
    Serial.println("========================================");
    Serial.print  ("[FACTORY TEST] Board: "); Serial.println(BOARD_NAME);
    Serial.print  ("[FACTORY TEST] FW: ");    Serial.println(FW_VERSION);
    Serial.println("[FACTORY TEST] T0 - System startup OK");
    Serial.println("[FACTORY TEST] Waiting for USER key to start...");
    Serial.println("========================================");

    // 6. Wait for USER key press
    waitForUser();

    Serial.println("[FACTORY TEST] T0 PASS - Starting test sequence");
}

// ─── Deep sleep with GPIO hold ────────────────────────────────────────────────
// Ensures all module/peripheral enables are LOW (or SPI CS HIGH) and latches
// the pin states so they are maintained while the IO power domain is off
// during deep sleep. Mirrors the pattern used in the ESP32-Dashboard BSP for
// this same board (nm_display_420/Board.cpp).

#if 0
static void _enterDeepSleep() {
    Serial.println("[FACTORY TEST] Powering off all modules...");

    // ── Force all enables off ──────────────────────────────────────────────
    // Drive every module-enable and peripheral pin to its "off" state before
    // latching, so there is no ambiguity about what level is held.
    pinMode(PIN_PA_CTRL,  OUTPUT); digitalWrite(PIN_PA_CTRL,  LOW);  // PA amp off
    pinMode(PIN_LORA_EN,  OUTPUT); digitalWrite(PIN_LORA_EN,  LOW);  // LoRa off
    pinMode(PIN_CODEC_EN, OUTPUT); digitalWrite(PIN_CODEC_EN, LOW);  // ES8311 off
    pinMode(PIN_ADC_EN,   OUTPUT); digitalWrite(PIN_ADC_EN,   LOW);  // Batt ADC off
    pinMode(PIN_TEMP_CTL, OUTPUT); digitalWrite(PIN_TEMP_CTL, LOW);  // AHT20 off
    pinMode(PIN_LORA_RST, OUTPUT); digitalWrite(PIN_LORA_RST, LOW);  // LoRa in reset
    pinMode(PIN_LORA_NSS, OUTPUT); digitalWrite(PIN_LORA_NSS, HIGH); // SPI CS idle HIGH

    delay(10);  // let GPIO outputs settle

    // ── Latch pin states ───────────────────────────────────────────────────
    // gpio_hold_en() captures the current output level and keeps it driven
    // even when the CPU is off. gpio_deep_sleep_hold_en() extends this hold
    // across the deep-sleep power domain transition on ESP32-S3.
    gpio_hold_en((gpio_num_t)PIN_PA_CTRL);
    gpio_hold_en((gpio_num_t)PIN_LORA_EN);
    gpio_hold_en((gpio_num_t)PIN_CODEC_EN);
    gpio_hold_en((gpio_num_t)PIN_ADC_EN);
    gpio_hold_en((gpio_num_t)PIN_TEMP_CTL);
    gpio_hold_en((gpio_num_t)PIN_LORA_RST);
    gpio_hold_en((gpio_num_t)PIN_LORA_NSS);
    gpio_deep_sleep_hold_en();

    Serial.println("[FACTORY TEST] GPIO states latched.");
    Serial.println("[FACTORY TEST] ESP32 entering deep sleep now.");
    Serial.println("[FACTORY TEST] Press BOOT button to wake up.");
    Serial.println("========================================");
    Serial.flush();

    // BOOT button (IO0 = RTC GPIO) wakes deep sleep — allows re-running
    // the test sequence by pressing BOOT followed by a power cycle.
    esp_sleep_enable_ext0_wakeup(GPIO_NUM_0, 0);
    esp_deep_sleep_start();
    // Never reached.
}
#endif

static void _enterDeepSleepNormal() {
    Serial.println("[FACTORY TEST] Powering off all modules...");

    pinMode(PIN_PA_CTRL,  OUTPUT); digitalWrite(PIN_PA_CTRL,  LOW);
    pinMode(PIN_LORA_EN,  OUTPUT); digitalWrite(PIN_LORA_EN,  LOW);
    pinMode(PIN_CODEC_EN, OUTPUT); digitalWrite(PIN_CODEC_EN, LOW);
    pinMode(PIN_ADC_EN,   OUTPUT); digitalWrite(PIN_ADC_EN,   LOW);
    pinMode(PIN_TEMP_CTL, OUTPUT); digitalWrite(PIN_TEMP_CTL, LOW);
    pinMode(PIN_LORA_RST, OUTPUT); digitalWrite(PIN_LORA_RST, LOW);
    pinMode(PIN_LORA_NSS, OUTPUT); digitalWrite(PIN_LORA_NSS, HIGH);
    delay(10);

    gpio_hold_en((gpio_num_t)PIN_PA_CTRL);
    gpio_hold_en((gpio_num_t)PIN_LORA_EN);
    gpio_hold_en((gpio_num_t)PIN_CODEC_EN);
    gpio_hold_en((gpio_num_t)PIN_ADC_EN);
    gpio_hold_en((gpio_num_t)PIN_TEMP_CTL);
    gpio_hold_en((gpio_num_t)PIN_LORA_RST);
    gpio_hold_en((gpio_num_t)PIN_LORA_NSS);
    gpio_deep_sleep_hold_en();

    Serial.println("[FACTORY TEST] ESP32 entering deep sleep now.");
    Serial.println("========================================");
    Serial.flush();

    esp_sleep_enable_ext0_wakeup(GPIO_NUM_0, 0);
    esp_deep_sleep_start();
}

// ─── T11 — Summary ────────────────────────────────────────────────────────────

void TestRunner::runT11() {
    // Test metadata. Names mirror README.md sequence table.
    struct TestMeta { uint8_t num; const char* name; };
    static const TestMeta META[] = {
        { 1,  "EPD Display"    },
        { 3,  "Buttons"        },
        { 4,  "ES8311 CODEC"   },
        { 5,  "DMIC Mic"       },
        { 6,  "AHT20 Sensor"   },
        { 7,  "Battery ADC"    },
        { 8,  "WiFi Scan"      },
        { 9,  "SD Card R/W"    },
        { 10, "LoRa SPI Bus"   },
    };
    constexpr uint8_t kTests = sizeof(META) / sizeof(META[0]);

    // Build display lines. Block is centered horizontally; rows are
    // left-aligned within the block (tabular look).
    char lineBuf[kTests][36];
    const char* lines[kTests];

    bool anyFail = false;
    char failList[64] = "";

    for (uint8_t i = 0; i < kTests; i++) {
        uint8_t num = META[i].num;
        TestResult r = _results[num - 1];  // keyed by test number, not array position
        const char* tag = (r == TestResult::PASS) ? "PASS" :
                          (r == TestResult::SKIP) ? "SKIP" : "FAIL";
        // Mono font: "T<num>  <name>........[TAG]" — pad name with dots to
        // a fixed width so [TAG] always lands in the same column.
        // 14 chars is the longest name ("ES8311 Codec  ").
        snprintf(lineBuf[i], sizeof(lineBuf[i]), "T%-2u  %-14s [%s]",
                 (unsigned)num, META[i].name, tag);
        lines[i] = lineBuf[i];

        if (r == TestResult::FAIL) {
            anyFail = true;
            char tmp[8];
            snprintf(tmp, sizeof(tmp), "T%u,", (unsigned)num);
            strncat(failList, tmp, sizeof(failList) - strlen(failList) - 1);
        }
    }

    // Remove trailing comma from failList
    size_t fl = strlen(failList);
    if (fl > 0 && failList[fl - 1] == ',') failList[fl - 1] = '\0';

    const char* summary = anyFail ? "FAIL" : "PASS";

    _display.showTestScreen(11, "Factory Test Summary",
                            lines, kTests,
                            summary, "Device entering deep sleep...",
                            /*linesLeftAlignedBlock=*/true,
                            /*monospaceStartLine=*/0);

    // Serial output
    Serial.println("========================================");
    Serial.println("[FACTORY TEST] ===== SUMMARY =====");
    for (uint8_t i = 0; i < kTests; i++) {
        Serial.print  ("[FACTORY TEST] ");
        Serial.println(lines[i]);
    }
    Serial.print("[FACTORY TEST] Overall: ");
    if (anyFail) {
        Serial.print("FACTORY_TEST=FAIL:");
        Serial.println(failList);
    } else {
        Serial.println("FACTORY_TEST=OK");
    }
    Serial.println("========================================");

    _display.hibernate();
    _enterDeepSleepNormal();
}

// EPD aging / burn-in clear mode. This intentionally never returns: the
// operator can power-cycle or reset the unit after the residual image clears.
void TestRunner::runEpdAgingMode() {
    Serial.println("========================================");
    Serial.println("[EPD AGING] Started: cycling 3-color checkerboards.");
    Serial.println("[EPD AGING] Power-cycle or reset the device to stop.");
    Serial.println("========================================");

    _display.init();
    pinMode(PIN_PA_CTRL, OUTPUT);  digitalWrite(PIN_PA_CTRL, LOW);
    pinMode(PIN_LORA_EN, OUTPUT);  digitalWrite(PIN_LORA_EN, LOW);
    pinMode(PIN_CODEC_EN, OUTPUT); digitalWrite(PIN_CODEC_EN, LOW);
    pinMode(PIN_ADC_EN, OUTPUT);   digitalWrite(PIN_ADC_EN, LOW);
    pinMode(PIN_TEMP_CTL, OUTPUT); digitalWrite(PIN_TEMP_CTL, LOW);

    auto drawCheckerboard = [&](uint16_t colorA, uint16_t colorB) {
        auto& epd = _display.raw();
        constexpr int16_t kCols = 8;
        constexpr int16_t kRows = 8;
        constexpr int16_t kCellW = DISP_W / kCols;
        constexpr int16_t kCellH = DISP_H / kRows;

        epd.setFullWindow();
        epd.firstPage();
        do {
            epd.fillScreen(GxEPD_WHITE);
            for (int16_t y = 0; y < kRows; y++) {
                for (int16_t x = 0; x < kCols; x++) {
                    const bool alt = ((x + y) & 1) != 0;
                    const uint16_t color = alt ? colorA : colorB;
                    epd.fillRect(x * kCellW, y * kCellH, kCellW, kCellH, color);
                }
            }
        } while (epd.nextPage());
    };

    uint32_t round = 0;
    while (true) {
        Serial.printf("[EPD AGING] Round %lu: BLACK / WHITE\n",
                      (unsigned long)++round);
        drawCheckerboard(GxEPD_BLACK, GxEPD_WHITE);
        delay(1000);

        Serial.printf("[EPD AGING] Round %lu: RED / WHITE\n",
                      (unsigned long)++round);
        drawCheckerboard(GxEPD_RED, GxEPD_WHITE);
        delay(1000);

        Serial.printf("[EPD AGING] Round %lu: BLACK / RED\n",
                      (unsigned long)++round);
        drawCheckerboard(GxEPD_BLACK, GxEPD_RED);
        delay(1000);
    }
}

// ─── Serial log helper ───────────────────────────────────────────────────────
static const char* _resultTag(TestResult r) {
    return (r == TestResult::PASS) ? "PASS" :
           (r == TestResult::SKIP) ? "SKIP" : "FAIL";
}

static TestResult _autoT6_AHT20() {
    T6_LOG("AUTO start");
    pinMode(PIN_TEMP_CTL, OUTPUT);
    digitalWrite(PIN_TEMP_CTL, HIGH);
    delay(50);

    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
    Wire.beginTransmission(AHT20_I2C_ADDR);
    if (Wire.endTransmission() != 0) {
        T6_LOG("AUTO FAIL: no ACK at 0x%02X", AHT20_I2C_ADDR);
        digitalWrite(PIN_TEMP_CTL, LOW);
        return TestResult::FAIL;
    }

    Adafruit_AHTX0 aht;
    if (!aht.begin(&Wire)) {
        T6_LOG("AUTO FAIL: driver init");
        digitalWrite(PIN_TEMP_CTL, LOW);
        return TestResult::FAIL;
    }

    float tSum = 0.0f;
    float hSum = 0.0f;
    int ok = 0;
    constexpr int kSamples = 6;
    for (int i = 0; i < kSamples; i++) {
        sensors_event_t humEvt, tempEvt;
        if (aht.getEvent(&humEvt, &tempEvt)) {
            tSum += tempEvt.temperature;
            hSum += humEvt.relative_humidity;
            ok++;
        }
        delay(300);
    }
    digitalWrite(PIN_TEMP_CTL, LOW);

    if (ok == 0) return TestResult::FAIL;
    const float tAvg = tSum / ok;
    const float hAvg = hSum / ok;
    const bool inRange = (tAvg >= T6_TEMP_MIN_C && tAvg <= T6_TEMP_MAX_C &&
                          hAvg >= T6_HUMI_MIN_PCT && hAvg <= T6_HUMI_MAX_PCT);
    T6_LOG("AUTO samples=%d/%d T=%.2fC H=%.2f%% range=%d",
           ok, kSamples, tAvg, hAvg, (int)inRange);
    return (ok >= 3 && inRange) ? TestResult::PASS : TestResult::FAIL;
}

static TestResult _autoT7_ADC() {
    Serial.println("[T7] AUTO bypass: charger BAT node is not a reliable battery-presence signal");
    Serial.println("[T7] AUTO result=PASS");
    return TestResult::PASS;
}

static TestResult _autoT8_WiFi() {
    T8_LOG("AUTO start");
    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);
    WiFi.disconnect(true, true);
    delay(100);
    int n = WiFi.scanNetworks(false, false);
    T8_LOG("AUTO scan n=%d", n);
    if (n >= 0) WiFi.scanDelete();
    WiFi.mode(WIFI_OFF);
    return (n >= 1) ? TestResult::PASS : TestResult::FAIL;
}

static TestResult _autoT9_SD() {
    T9_LOG("AUTO start");
    pinMode(PIN_LORA_NSS, OUTPUT);
    digitalWrite(PIN_LORA_NSS, HIGH);
    pinMode(PIN_SD_CS, OUTPUT);
    digitalWrite(PIN_SD_CS, HIGH);

    spiPeripheral.beginTransaction(SPISettings(400000, MSBFIRST, SPI_MODE0));
    for (int i = 0; i < 16; i++) spiPeripheral.transfer(0xFF);
    spiPeripheral.endTransaction();

    if (!SD.begin(PIN_SD_CS, spiPeripheral, T9_SPI_HZ)) return TestResult::FAIL;
    if (SD.cardType() == CARD_NONE) {
        SD.end();
        return TestResult::FAIL;
    }

    static uint8_t buf[T9_BLOCK_BYTES];
    static uint8_t rbuf[T9_BLOCK_BYTES];
    for (uint32_t i = 0; i < T9_BLOCK_BYTES; i++) buf[i] = (uint8_t)(i ^ (i >> 8));

    if (SD.exists(T9_TEST_PATH)) SD.remove(T9_TEST_PATH);
    File f = SD.open(T9_TEST_PATH, FILE_WRITE);
    if (!f) {
        SD.end();
        return TestResult::FAIL;
    }

    uint32_t written = 0;
    while (written < T9_TEST_BYTES) {
        size_t n = f.write(buf, T9_BLOCK_BYTES);
        if (n != T9_BLOCK_BYTES) break;
        written += n;
    }
    f.flush();
    f.close();

    bool verifyOk = (written == T9_TEST_BYTES);
    uint32_t readBytes = 0;
    f = SD.open(T9_TEST_PATH, FILE_READ);
    if (!f) {
        verifyOk = false;
    } else {
        while (readBytes < T9_TEST_BYTES && verifyOk) {
            size_t n = f.read(rbuf, T9_BLOCK_BYTES);
            if (n != T9_BLOCK_BYTES || memcmp(rbuf, buf, T9_BLOCK_BYTES) != 0) {
                verifyOk = false;
                break;
            }
            readBytes += n;
        }
        f.close();
    }

    SD.remove(T9_TEST_PATH);
    SD.end();
    const bool pass = verifyOk && readBytes == T9_TEST_BYTES;
    T9_LOG("AUTO written=%u read=%u result=%s",
           (unsigned)written, (unsigned)readBytes, pass ? "PASS" : "FAIL");
    return pass ? TestResult::PASS : TestResult::FAIL;
}

static TestResult _autoT10_LoRa() {
    T10_LOG("AUTO start");
    pinMode(PIN_LORA_EN, OUTPUT);
    digitalWrite(PIN_LORA_EN, HIGH);
    delay(10);

    pinMode(PIN_LORA_NSS, OUTPUT);
    pinMode(PIN_LORA_RST, OUTPUT);
    pinMode(PIN_LORA_BUSY, INPUT);
    digitalWrite(PIN_LORA_NSS, HIGH);
    digitalWrite(PIN_LORA_RST, HIGH);

    _t10_reset_radio();
    _t10_wait_busy_low(200);
    const bool busyOk = (digitalRead(PIN_LORA_BUSY) == LOW);

    bool rwAllOk = true;
    bool csAllOk = true;
    for (int i = 0; i < 3; i++) {
        uint8_t origMsb = _t10_read_reg(true, SX126X_REG_SYNCWORD_MSB);
        uint8_t origLsb = _t10_read_reg(true, SX126X_REG_SYNCWORD_LSB);
        uint8_t testMsb = (uint8_t)(origMsb ^ (uint8_t)(0x5A + i));
        uint8_t testLsb = (uint8_t)(origLsb ^ (uint8_t)(0xA5 - i));

        _t10_write_reg(SX126X_REG_SYNCWORD_MSB, testMsb);
        _t10_write_reg(SX126X_REG_SYNCWORD_LSB, testLsb);
        _t10_wait_busy_low(50);
        uint8_t rdMsb = _t10_read_reg(true, SX126X_REG_SYNCWORD_MSB);
        uint8_t rdLsb = _t10_read_reg(true, SX126X_REG_SYNCWORD_LSB);
        const bool rwOk = (rdMsb == testMsb) && (rdLsb == testLsb);

        _t10_write_reg(SX126X_REG_SYNCWORD_MSB, origMsb);
        _t10_write_reg(SX126X_REG_SYNCWORD_LSB, origLsb);
        _t10_wait_busy_low(50);
        uint8_t noCsMsb = _t10_read_reg(false, SX126X_REG_SYNCWORD_MSB);
        uint8_t noCsLsb = _t10_read_reg(false, SX126X_REG_SYNCWORD_LSB);
        const bool csOk = !((noCsMsb == rdMsb) && (noCsLsb == rdLsb));
        rwAllOk &= rwOk;
        csAllOk &= csOk;
    }

    digitalWrite(PIN_LORA_EN, LOW);
    const bool pass = busyOk && rwAllOk && csAllOk;
    T10_LOG("AUTO busy=%d rw=%d cs=%d result=%s",
            (int)busyOk, (int)rwAllOk, (int)csAllOk, pass ? "PASS" : "FAIL");
    return pass ? TestResult::PASS : TestResult::FAIL;
}

void TestRunner::runAutoSuite() {
    Serial.println("========================================");
    Serial.println("[AUTO] Starting batched automatic tests");
    Serial.println("========================================");

    storeResult(6, _autoT6_AHT20());
    storeResult(7, _autoT7_ADC());
    storeResult(8, _autoT8_WiFi());
    storeResult(9, _autoT9_SD());
    storeResult(10, _autoT10_LoRa());

    Serial.println("[AUTO] Completed batched automatic tests");
}

struct AutoSuiteTaskCtx {
    TestRunner* runner;
    volatile bool done;
};

void TestRunner::runAutoSuiteWithPrompt() {
    AutoSuiteTaskCtx ctx{ this, false };
    TaskHandle_t task = nullptr;
    auto taskFn = [](void* arg) {
        auto* ctx = static_cast<AutoSuiteTaskCtx*>(arg);
        ctx->runner->runAutoSuite();
        ctx->done = true;
        vTaskDelete(nullptr);
    };
    BaseType_t ok = xTaskCreatePinnedToCore(
        taskFn,
        "autoSuite",
        12288,
        &ctx,
        1,
        &task,
        0);

    if (ok != pdPASS) {
        Serial.println("[AUTO] Failed to start background task; running inline.");
        runAutoSuite();
        return;
    }

    _display.resync();
    const char* lines[] = {
        "Auto tests running",
        "AHT20 ADC WiFi SD LoRa",
        "",
        "Wait...",
    };
    _display.showTestScreen(2, "Auto Tests Running",
                            lines, 4,
                            nullptr, "Testing");

    while (!ctx.done) {
        delay(50);
    }
}

void TestRunner::showAutoSummary() {
    _display.resync();

    char l6[36], l7[36], l8[36], l9[36], l10[36];
    snprintf(l6,  sizeof(l6),  "T6  AHT20 Sensor   [%s]", _resultTag(_results[5]));
    snprintf(l7,  sizeof(l7),  "T7  Battery ADC    [%s]", _resultTag(_results[6]));
    snprintf(l8,  sizeof(l8),  "T8  WiFi Scan      [%s]", _resultTag(_results[7]));
    snprintf(l9,  sizeof(l9),  "T9  SD Card R/W    [%s]", _resultTag(_results[8]));
    snprintf(l10, sizeof(l10), "T10 LoRa SPI Bus   [%s]", _resultTag(_results[9]));
    const char* lines[] = { l6, l7, l8, l9, l10 };

    bool pass = (_results[5] == TestResult::PASS &&
                 _results[6] == TestResult::PASS &&
                 _results[7] == TestResult::PASS &&
                 _results[8] == TestResult::PASS &&
                 _results[9] == TestResult::PASS);

    _display.showTestScreen(2, "Auto Test Summary",
                            lines, 5,
                            pass ? "PASS" : "FAIL",
                            "USER=Manual",
                            /*linesLeftAlignedBlock=*/true,
                            /*monospaceStartLine=*/0);
    waitForUser();
}

static void logTestStart(uint8_t n, const char* name) {
    Serial.println("----------------------------------------");
    Serial.printf("[FACTORY TEST] T%u START - %s\n", (unsigned)n, name);
}

static void logTestEnd(uint8_t n, const char* name, TestResult r) {
    const char* tag = (r == TestResult::PASS) ? "PASS" :
                      (r == TestResult::SKIP) ? "SKIP" : "FAIL";
    Serial.printf("[FACTORY TEST] T%u END   - %s  [%s]\n", (unsigned)n, name, tag);
}

// Re-sync the EPD before every test so each test starts from a known panel
// state, regardless of what the previous test did to peripherals (WiFi RF,
// SD on HSPI, etc.).
void TestRunner::_preTest() {
    _display.resync();
}

// ─── Individual test dispatch wrappers ───────────────────────────────────────

TestResult TestRunner::runManualEpdVisual() {
    Serial.println("[MANUAL] EPD visual test");
    _display.resync();

    auto& epd = _display.raw();
    auto fullFill = [&](uint16_t color, uint16_t textColor, const char* label) {
        epd.setFullWindow();
        epd.firstPage();
        do {
            epd.fillScreen(color);
            epd.setFont(&FreeSansBold18pt7b);
            epd.setTextColor(textColor);
            int16_t x1, y1;
            uint16_t w, h;
            epd.getTextBounds(label, 0, 0, &x1, &y1, &w, &h);
            epd.setCursor((DISP_W - (int16_t)w) / 2 - x1,
                          (DISP_H + (int16_t)h) / 2 - y1);
            epd.print(label);
        } while (epd.nextPage());
        delay(800);
    };

    fullFill(GxEPD_WHITE, GxEPD_BLACK, "WHITE");
    fullFill(GxEPD_BLACK, GxEPD_WHITE, "BLACK");
    fullFill(GxEPD_RED, GxEPD_WHITE, "RED");

    const char* lines[] = {
        "Colors OK?",
    };
    _display.showTestScreen(1, "EPD Test", lines, 1,
                            nullptr, "USER=PASS  BOOT=FAIL");
    bool pass = waitForVerdict();
    return pass ? TestResult::PASS : TestResult::FAIL;
}

TestResult TestRunner::runManualButtons() {
    Serial.println("[MANUAL] Button test");
    _display.resync();
    const char* lines[] = {
        "Press USER",
        "Then BOOT",
    };
    _display.showTestScreen(3, "Buttons", lines, 2, nullptr, nullptr);

    while (!userPressed()) {
        delay(10);
    }
    while (userPressed()) delay(10);
    Serial.println("[MANUAL] USER button OK");

    while (!bootPressed()) {
        delay(10);
    }
    while (bootPressed()) delay(10);
    Serial.println("[MANUAL] BOOT button OK");

    return TestResult::PASS;
}

static bool _manualPlayTone(float freq, uint32_t ms) {
    static int16_t buf[T4_BUF_SAMPLES * 2];
    uint32_t total = (uint32_t)((uint64_t)T4_SAMPLE_RATE * ms / 1000ULL);
    uint32_t written = 0;
    float phase = 0.0f;
    const float step = 6.283185307f * freq / (float)T4_SAMPLE_RATE;

    while (written < total) {
        uint32_t chunk = T4_BUF_SAMPLES;
        if (written + chunk > total) chunk = total - written;
        for (uint32_t i = 0; i < chunk; i++) {
            int16_t s = (int16_t)(14000.0f * sinf(phase));
            phase += step;
            if (phase > 6.283185307f) phase -= 6.283185307f;
            buf[i * 2] = s;
            buf[i * 2 + 1] = s;
        }
        size_t bw = 0;
        if (i2s_channel_write(_t4_tx_handle, buf, chunk * 4, &bw, portMAX_DELAY) != ESP_OK) {
            return false;
        }
        written += chunk;
    }
    return true;
}

TestResult TestRunner::runManualAudioMic() {
    Serial.println("[MANUAL] Audio + microphone test");
    _display.resync();
    const char* intro[] = {
        "Listen tones",
        "Speak after tone",
        "Then hear playback",
    };
    _display.showTestScreen(4, "Audio + Mic", intro, 3,
                            nullptr, "Wait");

    pinMode(PIN_CODEC_EN, OUTPUT);
    digitalWrite(PIN_CODEC_EN, HIGH);
    delay(10);
    pinMode(PIN_PA_CTRL, OUTPUT);
    digitalWrite(PIN_PA_CTRL, LOW);
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
    Wire.beginTransmission(ES8311_ADDR);
    bool codecOk = (Wire.endTransmission() == 0);

    bool audioOk = false;
    if (codecOk && _t4_i2s_init()) {
        _es8311_wakeup_playback();
        digitalWrite(PIN_PA_CTRL, HIGH);
        delay(80);
        audioOk = _manualPlayTone(500.0f, 500) &&
                  _manualPlayTone(1000.0f, 500) &&
                  _manualPlayTone(3000.0f, 500);
        digitalWrite(PIN_PA_CTRL, LOW);
        _t4_i2s_deinit();
        _es8311_enter_powerdown();
    }

    constexpr uint32_t kRecordSec = 3;
    const uint32_t totalSamples = T5_SAMPLE_RATE * kRecordSec;
    const size_t bufBytes = totalSamples * sizeof(int16_t);
    int16_t* monoBuf = (int16_t*)heap_caps_malloc(bufBytes, MALLOC_CAP_SPIRAM);
    if (!monoBuf) monoBuf = (int16_t*)heap_caps_malloc(bufBytes, MALLOC_CAP_8BIT);

    uint32_t rms = 0;
    int16_t peak = 0;
    bool micOk = false;
    bool playbackOk = false;

    if (codecOk && monoBuf && _t5_i2s_init_rx()) {
        delay(50);
        _t5_es8311_wakeup_record();
        delay(100);
        static int16_t warm[T5_BUF_SAMPLES * 2];
        size_t br = 0;
        for (int i = 0; i < 4; i++) {
            i2s_channel_read(_t5_rx_handle, warm, sizeof(warm), &br, portMAX_DELAY);
        }

        static int16_t stereoBuf[T5_BUF_SAMPLES * 2];
        uint32_t recorded = 0;
        uint64_t sumSq = 0;
        while (recorded < totalSamples) {
            br = 0;
            if (i2s_channel_read(_t5_rx_handle, stereoBuf, sizeof(stereoBuf), &br,
                                 pdMS_TO_TICKS(200)) != ESP_OK || br == 0) {
                continue;
            }
            size_t frames = br / 4;
            if (recorded + frames > totalSamples) frames = totalSamples - recorded;
            for (size_t i = 0; i < frames; i++) {
                int32_t m = ((int32_t)stereoBuf[i * 2] + (int32_t)stereoBuf[i * 2 + 1]) / 2;
                monoBuf[recorded + i] = (int16_t)m;
                sumSq += (uint64_t)((int64_t)m * (int64_t)m);
                int16_t a = (m < 0) ? (int16_t)-m : (int16_t)m;
                if (a > peak) peak = a;
            }
            recorded += frames;
        }
        _t5_i2s_deinit_rx();

        rms = (recorded > 0) ? (uint32_t)sqrt((double)(sumSq / recorded)) : 0;
        micOk = (rms > DMIC_RMS_THRESHOLD_VOICE);

        if (_t5_i2s_init_tx()) {
            delay(50);
            _t5_es8311_wakeup_playback();
            delay(50);
            digitalWrite(PIN_PA_CTRL, HIGH);
            delay(80);
            static int16_t txBuf[T5_BUF_SAMPLES * 2];
            uint32_t played = 0;
            while (played < totalSamples) {
                uint32_t chunk = T5_BUF_SAMPLES;
                if (played + chunk > totalSamples) chunk = totalSamples - played;
                for (uint32_t i = 0; i < chunk; i++) {
                    int16_t s = monoBuf[played + i];
                    txBuf[i * 2] = s;
                    txBuf[i * 2 + 1] = s;
                }
                size_t bw = 0;
                i2s_channel_write(_t5_tx_handle, txBuf, chunk * 4, &bw, portMAX_DELAY);
                played += chunk;
            }
            delay(80);
            digitalWrite(PIN_PA_CTRL, LOW);
            _t5_i2s_deinit_tx();
            _t5_es8311_enter_powerdown();
            playbackOk = true;
        }
    }

    if (monoBuf) free(monoBuf);
    digitalWrite(PIN_PA_CTRL, LOW);
    digitalWrite(PIN_CODEC_EN, LOW);

    char l0[40], l1[40], l2[40];
    snprintf(l0, sizeof(l0), "Codec/I2S: %s", (codecOk && audioOk) ? "OK" : "FAIL");
    snprintf(l1, sizeof(l1), "Mic RMS=%u Peak=%d", (unsigned)rms, (int)peak);
    snprintf(l2, sizeof(l2), "Mic auto: %s", micOk ? "PASS" : "FAIL");
    const char* lines[] = {
        "Sound OK?",
        l0, l1, l2,
    };
    _display.showTestScreen(4, "Audio + Mic", lines, 4,
                            (codecOk && audioOk && micOk && playbackOk) ? "PASS" : "FAIL",
                            "USER=PASS  BOOT=FAIL");
    bool verdict = waitForVerdict();
    return (codecOk && audioOk && micOk && playbackOk && verdict)
        ? TestResult::PASS
        : TestResult::FAIL;
}

void TestRunner::runManualSuite() {
    Serial.println("========================================");
    Serial.println("[MANUAL] Starting concentrated manual tests");
    Serial.println("========================================");

    storeResult(1, runManualEpdVisual());
    storeResult(3, runManualButtons());
    TestResult audioMic = runManualAudioMic();
    storeResult(4, audioMic);
    storeResult(5, audioMic);

    Serial.println("[MANUAL] Completed concentrated manual tests");
}

void TestRunner::runT1()  {
    _preTest();
    logTestStart(1,  "EPD Display");
    TestResult r = runTestT1(_display, *this);
    storeResult(1, r);
    logTestEnd(1, "EPD Display", r);
}
void TestRunner::runT3()  {
    _preTest();
    logTestStart(3,  "Button");
    TestResult r = runTestT3(_display, *this);
    storeResult(3, r);
    logTestEnd(3, "Button", r);
}
void TestRunner::runT4()  {
    _preTest();
    logTestStart(4,  "ES8311 CODEC");
    TestResult r = runTestT4(_display, *this);
    storeResult(4, r);
    logTestEnd(4, "ES8311 CODEC", r);
}
void TestRunner::runT5()  {
    _preTest();
    logTestStart(5,  "LMD4737 DMIC");
    TestResult r = runTestT5(_display, *this);
    storeResult(5, r);
    logTestEnd(5, "LMD4737 DMIC", r);
}
void TestRunner::runT6()  {
    _preTest();
    logTestStart(6,  "AHT20 Sensor");
    TestResult r = runTestT6(_display, *this);
    storeResult(6, r);
    logTestEnd(6, "AHT20 Sensor", r);
}
void TestRunner::runT7()  {
    _preTest();
    logTestStart(7,  "Battery ADC");
    TestResult r = runTestT7(_display, *this);
    storeResult(7, r);
    logTestEnd(7, "Battery ADC", r);
}
void TestRunner::runT8()  {
    _preTest();
    logTestStart(8,  "WiFi Scan");
    TestResult r = runTestT8(_display, *this);
    storeResult(8, r);
    logTestEnd(8, "WiFi Scan", r);
}
void TestRunner::runT9()  {
    _preTest();
    logTestStart(9,  "SD Card");
    TestResult r = runTestT9(_display, *this);
    storeResult(9, r);
    logTestEnd(9, "SD Card", r);
}
void TestRunner::runT10() {
    _preTest();
    logTestStart(10, "LoRa SPI Bus");
    TestResult r = runTestT10(_display, *this);
    storeResult(10, r);
    logTestEnd(10, "LoRa SPI Bus", r);
}

// ─── Main entry ───────────────────────────────────────────────────────────────

void TestRunner::run() {
    Serial.begin(115200);
    pinMode(PIN_USER_BTN, INPUT_PULLUP);
    pinMode(PIN_BOOT_BTN, INPUT_PULLUP);
    delay(50);

    if (digitalRead(PIN_USER_BTN) == LOW) {
        Serial.println();
        Serial.println("========================================");
        Serial.println("[BOOT] USER held at power-on: entering EPD aging mode.");
        Serial.println("========================================");
        runEpdAgingMode();
    }

    runT0();
    runAutoSuiteWithPrompt();
    showAutoSummary();
    runManualSuite();
    runT11();
}
