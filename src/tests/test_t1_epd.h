#pragma once
// T1 - EPD display test

#include "test_runner.h"
#include "config.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#if NM_EPD_420_BW
static constexpr uint16_t T1_ACCENT_COLOR = GxEPD_BLACK;
static constexpr uint16_t T1_THIRD_ROUND_COLOR = GxEPD_BLACK;
static constexpr const char* T1_THIRD_ROUND_LABEL = "BLACK (repeat)";
static constexpr uint8_t T1_ROUND_COUNT = 4;
#elif NM_EPD_420_4C
static constexpr uint16_t T1_ACCENT_COLOR = GxEPD_YELLOW;
static constexpr uint16_t T1_THIRD_ROUND_COLOR = GxEPD_RED;
static constexpr const char* T1_THIRD_ROUND_LABEL = "RED";
static constexpr uint8_t T1_ROUND_COUNT = 5;
#else
static constexpr uint16_t T1_ACCENT_COLOR = GxEPD_RED;
static constexpr uint16_t T1_THIRD_ROUND_COLOR = GxEPD_RED;
static constexpr const char* T1_THIRD_ROUND_LABEL = "RED";
static constexpr uint8_t T1_ROUND_COUNT = 4;
#endif

#if NM_EPD_420_4C
static constexpr uint32_t T1_REFRESH_OBSERVATION_MS = 5000;
#endif

// During an EPD refresh the panel drives BUSY at its driver-specific active level.
// Polling BUSY from the main thread is unreliable because GxEPD2's
// firstPage()/nextPage() blocks inside its own _waitWhileBusy(). We spawn
// a 1 ms sampler task pinned to core 0 that just records whether it ever
// observed BUSY active and the total active duration. If after a full refresh
// the sampler saw 0 ms active, the BUSY net is broken (e.g. virtual solder
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
        if (Display::isPanelBusy()) {
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

// Helper: center-print a string at a given baseline Y using raw EPD.
static void _t1_printCentered(EpdDisplay& epd, const char* str, int16_t y) {
    int16_t  x1, y1;
    uint16_t w, h;
    epd.getTextBounds(str, 0, y, &x1, &y1, &w, &h);
    epd.setCursor((400 - (int16_t)w) / 2 - x1, y);
    epd.print(str);
}

// Helper: solid color fill with contrasting prompt overlay.
// bgColor   : fill color (GxEPD_WHITE / GxEPD_BLACK / GxEPD_RED)
// textColor : contrasting text color
// roundLabel: e.g. "Round 1/4 : Full WHITE fill"
// prompt    : e.g. "USER = OK     BOOT = FAIL"
static void _t1_colorRound(Display& disp,
                            uint16_t bgColor, uint16_t textColor,
                            const char* roundLabel, const char* prompt) {
    auto& epd = disp.raw();
    epd.setFullWindow();
    const uint32_t refreshStartedAt = millis();
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
#if NM_EPD_420_4C
    Serial.printf("[T1] %s refresh: %lu ms\n", roundLabel,
                  (unsigned long)(millis() - refreshStartedAt));
    Serial.printf("[T1] Holding image for %lu ms before verdict\n",
                  (unsigned long)T1_REFRESH_OBSERVATION_MS);
    delay(T1_REFRESH_OBSERVATION_MS);
#endif
}

// Helper: text demo screen (final round).
static void _t1_textDemo(Display& disp) {
    auto& epd = disp.raw();
    epd.setFullWindow();
    const uint32_t refreshStartedAt = millis();
    epd.firstPage();
    do {
        epd.fillScreen(GxEPD_WHITE);

        // Title row
        epd.setFont(&FreeSansBold18pt7b);
        epd.setTextColor(T1_ACCENT_COLOR);
        _t1_printCentered(epd, "T1", 36);

        epd.setFont(&FreeSans9pt7b);
        epd.setTextColor(GxEPD_BLACK);
        _t1_printCentered(epd, "EPD Text Rendering Demo", 56);

        epd.drawLine(10, 68, 390, 68, GxEPD_BLACK);

        // Font / color samples
        // Bold BLACK - large
        epd.setFont(&FreeSansBold18pt7b);
        epd.setTextColor(GxEPD_BLACK);
        _t1_printCentered(epd, "Bold Black Large", 110);

        // Normal BLACK - small
        epd.setFont(&FreeSans9pt7b);
        epd.setTextColor(GxEPD_BLACK);
        _t1_printCentered(epd, "Normal black small  0123456789", 140);

        // Colored large sample
        epd.setFont(&FreeSansBold18pt7b);
    #if NM_EPD_420_BW
        epd.setTextColor(T1_ACCENT_COLOR);
        _t1_printCentered(epd, "Bold black (accent) large", 180);
    #elif NM_EPD_420_4C
        epd.setTextColor(GxEPD_RED);
        _t1_printCentered(epd, "Bold red large", 180);
    #else
        epd.setTextColor(T1_ACCENT_COLOR);
        _t1_printCentered(epd, "Bold Red Large", 180);
    #endif

        // Colored small sample
        epd.setFont(&FreeSans9pt7b);
    #if NM_EPD_420_BW
        epd.setTextColor(T1_ACCENT_COLOR);
        _t1_printCentered(epd, "Normal black small  !@#$%^&*()", 210);
    #elif NM_EPD_420_4C
        epd.setTextColor(GxEPD_YELLOW);
        _t1_printCentered(epd, "Normal yellow small  !@#$%^&*()", 210);
    #else
        epd.setTextColor(T1_ACCENT_COLOR);
        _t1_printCentered(epd, "Normal red small  !@#$%^&*()", 210);
    #endif

        // Verdict prompt
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
#if NM_EPD_420_4C
    Serial.printf("[T1] Text demo refresh: %lu ms\n",
                  (unsigned long)(millis() - refreshStartedAt));
    Serial.printf("[T1] Holding image for %lu ms before verdict\n",
                  (unsigned long)T1_REFRESH_OBSERVATION_MS);
    delay(T1_REFRESH_OBSERVATION_MS);
#endif
}

// T1 implementation
inline TestResult runTestT1(Display& disp, TestRunner& runner) {

    Serial.println("[T1] EPD Display Test started");
#if NM_EPD_420_BW
    Serial.println("[T1] Round 1: WHITE fill  Round 2: BLACK fill  Round 3: BLACK fill(repeat)  Round 4: Text demo");
#elif NM_EPD_420_4C
    Serial.println("[T1] Round 1: WHITE fill  Round 2: BLACK fill  Round 3: RED fill  Round 4: YELLOW fill  Round 5: Text demo");
#else
    Serial.println("[T1] Round 1: WHITE fill  Round 2: BLACK fill  Round 3: RED fill  Round 4: Text demo");
#endif

    // Round 1: WHITE
    // We piggyback a BUSY-pin self-check on this round: a 1 ms sampler
    // task records whether GPIO6 ever goes active during the refresh. If
    // not, the BUSY net is broken (typical cause: virtual solder joint
    // on the MCU side) and the rest of T1 would run blind, so we fail
    // immediately with an explicit message.
    Serial.printf("[T1] Round 1/%u - Filling screen WHITE ...\n", T1_ROUND_COUNT);
    _t1_busyMonStart();
#if NM_EPD_420_4C
    _t1_colorRound(disp, GxEPD_WHITE, GxEPD_BLACK,
                   "Round 1/5 : Full WHITE fill",
                   "USER = OK       BOOT = FAIL");
#else
    _t1_colorRound(disp, GxEPD_WHITE, GxEPD_BLACK,
                   "Round 1/4 : Full WHITE fill",
                   "USER = OK       BOOT = FAIL");
#endif
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

    // Round 2: BLACK
    Serial.printf("[T1] Round 2/%u - Filling screen BLACK ...\n", T1_ROUND_COUNT);
#if NM_EPD_420_4C
    _t1_colorRound(disp, GxEPD_BLACK, GxEPD_WHITE,
                   "Round 2/5 : Full BLACK fill",
                   "USER = OK       BOOT = FAIL");
#else
    _t1_colorRound(disp, GxEPD_BLACK, GxEPD_WHITE,
                   "Round 2/4 : Full BLACK fill",
                   "USER = OK       BOOT = FAIL");
#endif
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

    // Round 3: RED
    Serial.printf("[T1] Round 3/%u - Filling screen %s ...\n", T1_ROUND_COUNT, T1_THIRD_ROUND_LABEL);
#if NM_EPD_420_BW
    _t1_colorRound(disp, T1_THIRD_ROUND_COLOR, GxEPD_WHITE,
                   "Round 3/4 : Full BLACK fill (repeat)",
                   "USER = OK       BOOT = FAIL");
#elif NM_EPD_420_4C
    _t1_colorRound(disp, T1_THIRD_ROUND_COLOR, GxEPD_BLACK,
                   "Round 3/5 : Full RED fill",
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

#if NM_EPD_420_4C
    Serial.println("[T1] Round 4/5 - Filling screen YELLOW ...");
    _t1_colorRound(disp, GxEPD_YELLOW, GxEPD_BLACK,
                   "Round 4/5 : Full YELLOW fill",
                   "USER = OK       BOOT = FAIL");
    Serial.println("[T1] YELLOW fill shown. Screen should be all yellow.");
    Serial.println("[T1] >>> waiting for verdict (USER=OK  BOOT=FAIL)");
    if (!runner.waitForVerdict()) {
        Serial.println("[T1] <<< verdict received: FAIL");
        Serial.println("[T1] FAIL - YELLOW fill rejected by operator");
        static const char* failMsg[] = { "YELLOW fill FAILED", "Press USER button to continue" };
        disp.showTestScreen(1, "EPD Display Test", failMsg, 2, "FAIL", "USER=Next test");
        Serial.println("[T1] >>> waiting for USER to continue");
        runner.waitForUser();
        Serial.println("[T1] <<< USER received, moving on");
        return TestResult::FAIL;
    }
    Serial.println("[T1] <<< verdict received: OK");
    Serial.println("[T1] Round 4 YELLOW: OK");
#endif

    // Final round: text rendering
#if NM_EPD_420_4C
    Serial.println("[T1] Round 5/5 - Text rendering demo screen ...");
#else
    Serial.println("[T1] Round 4/4 - Text rendering demo screen ...");
#endif
    _t1_textDemo(disp);
#if NM_EPD_420_BW
    Serial.println("[T1] Text demo shown: Bold/Normal in black/white mode.");
#elif NM_EPD_420_4C
    Serial.println("[T1] Text demo shown: Black, red, and yellow samples.");
#else
    Serial.println("[T1] Text demo shown: Bold/Normal x Black/Red.");
#endif
    Serial.println("[T1] >>> waiting for verdict (USER=PASS  BOOT=FAIL)");
    bool pass = runner.waitForVerdict();
    Serial.printf("[T1] <<< verdict received: %s\n", pass ? "PASS" : "FAIL");
    return pass ? TestResult::PASS : TestResult::FAIL;
}
