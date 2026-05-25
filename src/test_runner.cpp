#include "test_runner.h"
#include "config.h"
#include "spi_buses.h"
#include <esp_sleep.h>
#include <driver/gpio.h>

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
        // 14 chars is the longest name ("WS2812 RGB LED").
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

    // Hibernate the EPD panel so it retains the summary image at minimum power.
    _display.hibernate();

    // Power off all peripherals and enter ESP32 deep sleep.
    // GPIO states are latched so module enable pins remain LOW during sleep.
    _enterDeepSleep();
}

// ─── Serial log helper ───────────────────────────────────────────────────────
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
    runT0();   // Init + welcome; advances only after USER key
    _state = TestState::T1_EPD;  // kick off test sequence

    while (_state != TestState::DONE) {
        switch (_state) {
            case TestState::T1_EPD:     runT1();  _state = TestState::T3_BUTTON;  break;
            case TestState::T3_BUTTON:  runT3();  _state = TestState::T4_CODEC;    break;
            case TestState::T4_CODEC:   runT4();  _state = TestState::T5_DMIC;     break;
            case TestState::T5_DMIC:    runT5();  _state = TestState::T6_AHT20;    break;
            case TestState::T6_AHT20:   runT6();  _state = TestState::T7_ADC;      break;
            case TestState::T7_ADC:     runT7();  _state = TestState::T8_WIFI;     break;
            case TestState::T8_WIFI:    runT8();  _state = TestState::T9_SD;       break;
            case TestState::T9_SD:      runT9();  _state = TestState::T10_LORA;    break;
            case TestState::T10_LORA:   runT10(); _state = TestState::T11_SUMMARY; break;
            case TestState::T11_SUMMARY: runT11(); _state = TestState::DONE;       break;
            default:                    _state = TestState::DONE;                  break;
        }
    }
}
