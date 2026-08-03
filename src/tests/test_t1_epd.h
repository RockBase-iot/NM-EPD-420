#pragma once
// T1 鈥?EPD 3-color display test

#include "test_runner.h"
#include "config.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#if NM_EPD_420_BW
static constexpr uint16_t T1_ACCENT_COLOR = GxEPD_BLACK;
static constexpr uint16_t T1_THIRD_ROUND_COLOR = GxEPD_BLACK;
static constexpr const char* T1_THIRD_ROUND_LABEL = "BLACK (repeat)";
#else
static constexpr uint16_t T1_ACCENT_COLOR = GxEPD_RED;
static constexpr uint16_t T1_THIRD_ROUND_COLOR = GxEPD_RED;
static constexpr const char* T1_THIRD_ROUND_LABEL = "RED";
#endif

// 鈹€鈹€鈹€ BUSY-pin sanity sampler 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€
// During an EPD refresh the panel drives BUSY HIGH for several seconds.
// Polling BUSY from the main thread is unreliable because GxEPD2's
// firstPage()/nextPage() blocks inside its own _waitWhileBusy(). We spawn
// a 1 ms sampler task pinned to core 0 that just records whether it ever
// observed BUSY=HIGH and the total HIGH duration. If after a full refresh
// the sampler saw 0 ms HIGH, the BUSY net is broken (e.g. virtual solder
// joint on the MCU side).
struct _T1BusyMon {
    volatile bool      run;
    volatile bool      sawHigh;
    volatile uint32_t  highMs;
    TaskHandle_t       handle;
};
static _T1BusyMon _t1_busyMon;

static void _t1_busyMonTask(void* arg) {
    auto* m = static_cast<_T1BusyMon*>(arg);
    pinMode(PIN_EPD_BUSY, INPUT);
    while (m->run) {
        if (digitalRead(PIN_EPD_BUSY) == HIGH) {
            m->sawHigh = true;
            m->highMs += 1;
        }
        vTaskDelay(1);  // ~1 ms tick
    }
    vTaskDelete(nullptr);
}

static void _t1_busyMonStart() {
    _t1_busyMon = { true, false, 0, nullptr };
    xTaskCreatePinnedToCore(_t1_busyMonTask, "t1Busy", 2048,
                            &_t1_busyMon, 1, &_t1_busyMon.handle, 0);
}

static void _t1_busyMonStop() {
    _t1_busyMon.run = false;
    // Sampler self-deletes on next tick; give it a moment.
    delay(5);
}

// 鈹€鈹€鈹€ Helper: center-print a string at given baseline y using raw EPD 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€
static void _t1_printCentered(EpdDisplay& epd, const char* str, int16_t y) {
    int16_t  x1, y1;
    uint16_t w, h;
    epd.getTextBounds(str, 0, y, &x1, &y1, &w, &h);
    epd.setCursor((400 - (int16_t)w) / 2 - x1, y);
    epd.print(str);
}

// 鈹€鈹€鈹€ Helper: solid color fill with contrasting prompt overlay 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€
// bgColor   : fill color (GxEPD_WHITE / GxEPD_BLACK / GxEPD_RED)
// textColor : contrasting text color
// roundLabel: e.g. "Round 1/4 : Full WHITE fill"
// prompt    : e.g. "USER = OK     BOOT = FAIL"
static void _t1_colorRound(Display& disp,
                            uint16_t bgColor, uint16_t textColor,
                            const char* roundLabel, const char* prompt) {
    auto& epd = disp.raw();
    epd.setFullWindow();
    epd.firstPage();
    do {
        epd.fillScreen(bgColor);

        epd.setFont(&FreeSansBold9pt7b);
        epd.setTextColor(textColor);
        _t1_printCentered(epd, roundLabel, 36);

        // Horizontal rule near bottom
        epd.drawLine(10, 270, 390, 270, textColor);

        epd.setFont(&FreeSans9pt7b);
        _t1_printCentered(epd, prompt, 289);
    } while (epd.nextPage());
}

// 鈹€鈹€鈹€ Helper: text demo screen (Round 4) 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€
static void _t1_textDemo(Display& disp) {
    auto& epd = disp.raw();
    epd.setFullWindow();
    epd.firstPage();
    do {
        epd.fillScreen(GxEPD_WHITE);

        // 鈹€鈹€ Title row 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€
        epd.setFont(&FreeSansBold18pt7b);
        epd.setTextColor(T1_ACCENT_COLOR);
        _t1_printCentered(epd, "T1", 36);

        epd.setFont(&FreeSans9pt7b);
        epd.setTextColor(GxEPD_BLACK);
        _t1_printCentered(epd, "EPD Text Rendering Demo", 56);

        epd.drawLine(10, 68, 390, 68, GxEPD_BLACK);

        // 鈹€鈹€ Font / color samples 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€
        // Bold BLACK 鈥?large
        epd.setFont(&FreeSansBold18pt7b);
        epd.setTextColor(GxEPD_BLACK);
        _t1_printCentered(epd, "Bold Black Large", 110);

        // Normal BLACK 鈥?small
        epd.setFont(&FreeSans9pt7b);
        epd.setTextColor(GxEPD_BLACK);
        _t1_printCentered(epd, "Normal black small  0123456789", 140);

        // Bold RED 鈥?large
        epd.setFont(&FreeSansBold18pt7b);
        epd.setTextColor(T1_ACCENT_COLOR);
    #if NM_EPD_420_BW
        _t1_printCentered(epd, "Bold black (accent) large", 180);
    #else
        _t1_printCentered(epd, "Bold Red Large", 180);
    #endif

        // Normal RED 鈥?small
        epd.setFont(&FreeSans9pt7b);
        epd.setTextColor(T1_ACCENT_COLOR);
    #if NM_EPD_420_BW
        _t1_printCentered(epd, "Normal black small  !@#$%^&*()", 210);
    #else
        _t1_printCentered(epd, "Normal red small  !@#$%^&*()", 210);
    #endif

        // 鈹€鈹€ Verdict prompt 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€
        epd.drawLine(10, 245, 390, 245, GxEPD_BLACK);
        epd.setFont(&FreeSansBold9pt7b);
        epd.setTextColor(GxEPD_BLACK);
    #if NM_EPD_420_BW
        _t1_printCentered(epd, "All text clear and contrast acceptable?", 265);
    #else
        _t1_printCentered(epd, "All text clear, bold/normal & colors correct?", 265);
    #endif
        epd.setFont(&FreeSans9pt7b);
        _t1_printCentered(epd, "USER = PASS       BOOT = FAIL", 285);
    } while (epd.nextPage());
}

// 鈹€鈹€鈹€ T1 implementation 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€
inline TestResult runTestT1(Display& disp, TestRunner& runner) {

    Serial.println("[T1] EPD Display Test started");
#if NM_EPD_420_BW
    Serial.println("[T1] Round 1: WHITE fill  Round 2: BLACK fill  Round 3: BLACK fill(repeat)  Round 4: Text demo");
#else
    Serial.println("[T1] Round 1: WHITE fill  Round 2: BLACK fill  Round 3: RED fill  Round 4: Text demo");
#endif

    // 鈹€鈹€ Round 1: WHITE 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€
    // We piggyback a BUSY-pin self-check on this round: a 1 ms sampler
    // task records whether GPIO6 ever goes HIGH during the refresh. If
    // not, the BUSY net is broken (typical cause: virtual solder joint
    // on the MCU side) and the rest of T1 would run blind, so we fail
    // immediately with an explicit message.
    Serial.println("[T1] Round 1/4 - Filling screen WHITE ...");
    _t1_busyMonStart();
    _t1_colorRound(disp, GxEPD_WHITE, GxEPD_BLACK,
                   "Round 1/4 : Full WHITE fill",
                   "USER = OK       BOOT = FAIL");
    _t1_busyMonStop();
    Serial.printf("[T1] BUSY self-check: sawHigh=%d  highMs=%lu\n",
                  (int)_t1_busyMon.sawHigh,
                  (unsigned long)_t1_busyMon.highMs);
    if (!_t1_busyMon.sawHigh) {
        Serial.println("[T1] FAIL - BUSY pin (GPIO6) never went HIGH during refresh");
        Serial.println("[T1]        check MCU-side BUSY connection / solder joint");
        static const char* failMsg[] = {
            "EPD BUSY pin not toggling",
            "Check GPIO6 connection",
            "Press USER to continue"
        };
        disp.showTestScreen(1, "EPD Display Test", failMsg, 3,
                            "FAIL", "USER=Next test");
        Serial.println("[T1] >>> waiting for USER to continue");
        runner.waitForUser();
        Serial.println("[T1] <<< USER received, moving on");
        return TestResult::FAIL;
    }
    Serial.println("[T1] WHITE fill shown. Screen should be all white.");
    Serial.println("[T1] >>> waiting for verdict (USER=OK  BOOT=FAIL)");
    if (!runner.waitForVerdict()) {
        Serial.println("[T1] <<< verdict received: FAIL");
        Serial.println("[T1] FAIL - WHITE fill rejected by operator");
        static const char* failMsg[] = { "WHITE fill FAILED", "Press USER button to continue" };
        disp.showTestScreen(1, "EPD Display Test", failMsg, 2, "FAIL", "USER=Next test");
        Serial.println("[T1] >>> waiting for USER to continue");
        runner.waitForUser();
        Serial.println("[T1] <<< USER received, moving on");
        return TestResult::FAIL;
    }
    Serial.println("[T1] <<< verdict received: OK");
    Serial.println("[T1] Round 1 WHITE: OK");

    // 鈹€鈹€ Round 2: BLACK 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€
    Serial.println("[T1] Round 2/4 - Filling screen BLACK ...");
    _t1_colorRound(disp, GxEPD_BLACK, GxEPD_WHITE,
                   "Round 2/4 : Full BLACK fill",
                   "USER = OK       BOOT = FAIL");
    Serial.println("[T1] BLACK fill shown. Screen should be all black.");
    Serial.println("[T1] >>> waiting for verdict (USER=OK  BOOT=FAIL)");
    if (!runner.waitForVerdict()) {
        Serial.println("[T1] <<< verdict received: FAIL");
        Serial.println("[T1] FAIL - BLACK fill rejected by operator");
        static const char* failMsg[] = { "BLACK fill FAILED", "Press USER button to continue" };
        disp.showTestScreen(1, "EPD Display Test", failMsg, 2, "FAIL", "USER=Next test");
        Serial.println("[T1] >>> waiting for USER to continue");
        runner.waitForUser();
        Serial.println("[T1] <<< USER received, moving on");
        return TestResult::FAIL;
    }
    Serial.println("[T1] <<< verdict received: OK");
    Serial.println("[T1] Round 2 BLACK: OK");

    // 鈹€鈹€ Round 3: RED 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€
    Serial.printf("[T1] Round 3/4 - Filling screen %s ...\n", T1_THIRD_ROUND_LABEL);
#if NM_EPD_420_BW
    _t1_colorRound(disp, T1_THIRD_ROUND_COLOR, GxEPD_WHITE,
                   "Round 3/4 : Full BLACK fill (repeat)",
                   "USER = OK       BOOT = FAIL");
#else
    _t1_colorRound(disp, T1_THIRD_ROUND_COLOR, GxEPD_BLACK,
                   "Round 3/4 : Full RED fill",
                   "USER = OK       BOOT = FAIL");
#endif
#if NM_EPD_420_BW
    Serial.println("[T1] BLACK fill shown (repeat). Screen should be all black.");
#else
    Serial.println("[T1] RED fill shown. Screen should be all red.");
#endif
    Serial.println("[T1] >>> waiting for verdict (USER=OK  BOOT=FAIL)");
    if (!runner.waitForVerdict()) {
        Serial.println("[T1] <<< verdict received: FAIL");
#if NM_EPD_420_BW
        Serial.println("[T1] FAIL - BLACK fill(repeat) rejected by operator");
        static const char* failMsg[] = { "BLACK fill(repeat) FAILED", "Press USER button to continue" };
#else
        Serial.println("[T1] FAIL - RED fill rejected by operator");
        static const char* failMsg[] = { "RED fill FAILED", "Press USER button to continue" };
#endif
        disp.showTestScreen(1, "EPD Display Test", failMsg, 2, "FAIL", "USER=Next test");
        Serial.println("[T1] >>> waiting for USER to continue");
        runner.waitForUser();
        Serial.println("[T1] <<< USER received, moving on");
        return TestResult::FAIL;
    }
    Serial.println("[T1] <<< verdict received: OK");
#if NM_EPD_420_BW
    Serial.println("[T1] Round 3 BLACK(repeat): OK");
#else
    Serial.println("[T1] Round 3 RED: OK");
#endif

    // 鈹€鈹€ Round 4: Text rendering 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€
    Serial.println("[T1] Round 4/4 - Text rendering demo screen ...");
    _t1_textDemo(disp);
#if NM_EPD_420_BW
    Serial.println("[T1] Text demo shown: Bold/Normal in black/white mode.");
#else
    Serial.println("[T1] Text demo shown: Bold/Normal x Black/Red.");
#endif
    Serial.println("[T1] >>> waiting for verdict (USER=PASS  BOOT=FAIL)");
    bool pass = runner.waitForVerdict();
    Serial.printf("[T1] <<< verdict received: %s\n", pass ? "PASS" : "FAIL");
    return pass ? TestResult::PASS : TestResult::FAIL;
}
