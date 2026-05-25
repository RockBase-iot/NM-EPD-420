#pragma once

#include <Arduino.h>
#include "ui/display_helper.h"

// ─── Test result type ─────────────────────────────────────────────────────────
enum class TestResult : uint8_t {
    PASS,
    FAIL,
    SKIP
};

// ─── Test state machine ───────────────────────────────────────────────────────
enum class TestState : uint8_t {
    T0_WELCOME = 0,
    T1_EPD,
    T3_BUTTON,
    T4_CODEC,
    T5_DMIC,
    T6_AHT20,
    T7_ADC,
    T8_WIFI,
    T9_SD,
    T10_LORA,
    T11_SUMMARY,
    DONE
};

// ─── TestRunner ───────────────────────────────────────────────────────────────
// Executes the full factory-test sequence. Call run() from setup().
// Each test is a blocking function; the watchdog is disabled before entry.

class TestRunner {
public:
    void run();

    // ── Button helpers (public for use from test files) ──────────────────────
    static bool userPressed();
    static bool bootPressed();

    // ── Block until USER key is pressed (used by auto-pass tests) ───────────
    static void waitForUser();

    // ── Block until USER or BOOT pressed, return which one ──────────────
    // Returns true = USER (PASS), false = BOOT (FAIL)
    static bool waitForVerdict();

    Display& display() { return _display; }

private:
    Display    _display;
    TestResult _results[10] = {};   // T1~T10, index = testNum-1
    TestState  _state       = TestState::T0_WELCOME;

    // ── Per-test run methods ─────────────────────────────────────────────────
    void runT0();
    void runT1();
    void runT3();
    void runT4();
    void runT5();
    void runT6();
    void runT7();
    void runT8();
    void runT9();
    void runT10();
    void runT11();

    // Re-sync EPD before each test (recover from peripheral side-effects).
    void _preTest();

    // ── Result storage helper ────────────────────────────────────────────────
    void storeResult(uint8_t testNum, TestResult r) {
        if (testNum >= 1 && testNum <= 10) {
            _results[testNum - 1] = r;
        }
    }
};
