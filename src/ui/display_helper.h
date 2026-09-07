#pragma once

#include <Arduino.h>
#include "config.h"
#include <SPI.h>
#ifndef ENABLE_GxEPD2_GFX
#define ENABLE_GxEPD2_GFX 1
#endif
#if NM_EPD_075_3C
#include <GxEPD2_3C.h>
#include <epd3c/GxEPD2_750c_Z08.h>
#elif NM_EPD_420_4C
#include <GxEPD2_4C.h>
#include <epd4c/GxEPD2_420c_GDEY0420F51.h>
#elif NM_EPD_420_BW
#include <GxEPD2_BW.h>
#include <other/GxEPD2_420_GYE042A87.h>
#else
#include <GxEPD2_3C.h>
#include <gdey3c/GxEPD2_420c_GDEY042Z98.h>
#endif
#include <GxEPD2_GFX.h>
#include <driver/gpio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSansBold9pt7b.h>
#include <Fonts/FreeSansBold18pt7b.h>
#include <Fonts/FreeMono9pt7b.h>

#if !NM_EPD_420_BW && !NM_EPD_420_4C && !NM_EPD_075_3C
#include "epd_uc8179_420c.h"
#endif

#if NM_EPD_075_3C
using Panel75Display = GxEPD2_3C<GxEPD2_750c_Z08, EPD_PAGE_HEIGHT>;
#elif NM_EPD_420_4C
using FourColorDisplay = GxEPD2_4C<GxEPD2_420c_GDEY0420F51, EPD_PAGE_HEIGHT>;
#elif NM_EPD_420_BW
using BwDisplay      = GxEPD2_BW<GxEPD2_420_GYE042A87, EPD_PAGE_HEIGHT>;
#else
using Ssd1683Display = GxEPD2_3C<GxEPD2_420c_GDEY042Z98, EPD_PAGE_HEIGHT>;
using Uc8179Display  = GxEPD2_3C<GxEPD2_420c_NM_UC8179, EPD_PAGE_HEIGHT>;
#endif
using EpdDisplay     = GxEPD2_GFX;

#if NM_EPD_075_3C
static constexpr int16_t DISP_W = 800;
static constexpr int16_t DISP_H = 480;
#else
static constexpr int16_t DISP_W = 400;
static constexpr int16_t DISP_H = 300;
#endif

class Display {
public:
    static constexpr uint8_t MAX_LINES = 10;
    inline static uint8_t s_busyActiveLevel = HIGH;

    void init() {
        SPI.begin(PIN_EPD_SCK, PIN_EPD_MISO, PIN_EPD_MOSI, PIN_EPD_CS);
        _selectDriver();
        _initActive(true);
    }

    void resync() {
        _initActive(true);
    }

    void showWelcome() {
#if NM_EPD_420_BW || NM_EPD_420_4C || NM_EPD_075_3C
        _renderWelcomeInternal(false);
        return;
#else
        const bool validateBusy = (EPD_DRIVER_MODE == 2) && !_autoValidated;
        if (_renderWelcomeInternal(validateBusy)) {
            _autoValidated = true;
            return;
        }
#if EPD_DRIVER_MODE == 2
        Serial.printf("[EPDDetect] validation failed for %s, retry with %s\n",
                      _isUc8179 ? "UC8179" : "SSD1683",
                      _isUc8179 ? "SSD1683" : "UC8179");
        _isUc8179 = !_isUc8179;
        _active = _isUc8179 ? static_cast<EpdDisplay*>(&_uc8179)
                            : static_cast<EpdDisplay*>(&_ssd1683);
        _initActive(true);
        _autoValidated = true;
        _renderWelcomeInternal(false);
#endif
    #endif
    }

    void showTestScreen(
        uint8_t     testNum,
        const char* title,
        const char* const lines[] = nullptr,
        uint8_t     lineCount     = 0,
        const char* result        = nullptr,
        const char* prompt        = nullptr,
        bool        linesLeftAlignedBlock = false,
        uint8_t     monospaceStartLine    = 255)
    {
        auto& epd = raw();
        epd.setFullWindow();
        const uint32_t refreshStartedAt = millis();
        epd.firstPage();
        do {
            epd.fillScreen(GxEPD_WHITE);
            _drawTestContent(testNum, title, lines, lineCount, result, prompt,
                             linesLeftAlignedBlock, monospaceStartLine);
        } while (epd.nextPage());
        _logRefreshTime("test screen", refreshStartedAt);
    }

    void showTestRunning(uint8_t testNum, const char* title,
                         const char* const lines[] = nullptr, uint8_t lineCount = 0)
    {
        showTestScreen(testNum, title, lines, lineCount, nullptr, nullptr);
    }

    void showTestResult(uint8_t testNum, const char* title,
                        const char* const lines[], uint8_t lineCount,
                        bool pass, const char* prompt = "USER=Next")
    {
        showTestScreen(testNum, title, lines, lineCount,
                       pass ? "PASS" : "FAIL", prompt);
    }

    void hibernate() { raw().hibernate(); }

    EpdDisplay& raw() { return *_active; }
#if NM_EPD_420_BW || NM_EPD_420_4C || NM_EPD_075_3C
    bool isUc8179() const { return false; }
#else
    bool isUc8179() const { return _isUc8179; }
#endif
    static bool isPanelBusy() { return digitalRead(PIN_EPD_BUSY) == s_busyActiveLevel; }

private:
    static void _logRefreshTime(const char* screenName, uint32_t refreshStartedAt) {
#if NM_EPD_420_4C
        Serial.printf("[EPD] %s refresh: %lu ms\n", screenName,
                      (unsigned long)(millis() - refreshStartedAt));
#else
        (void)screenName;
        (void)refreshStartedAt;
#endif
    }

#if NM_EPD_075_3C
    GxEPD2_750c_Z08 _panel75Driver{
        PIN_EPD_CS, PIN_EPD_DC, PIN_EPD_RST, PIN_EPD_BUSY
    };
    Panel75Display _panel75{_panel75Driver};
    EpdDisplay*    _active = &_panel75;
    bool           _autoValidated = true;
#elif NM_EPD_420_4C
    GxEPD2_420c_GDEY0420F51 _fourColorDriver{
        PIN_EPD_CS, PIN_EPD_DC, PIN_EPD_RST, PIN_EPD_BUSY
    };
    FourColorDisplay _fourColor{_fourColorDriver};
    EpdDisplay*      _active = &_fourColor;
    bool             _autoValidated = true;
#elif NM_EPD_420_BW
    GxEPD2_420_GYE042A87 _bwDriver{
        PIN_EPD_CS, PIN_EPD_DC, PIN_EPD_RST, PIN_EPD_BUSY
    };
    BwDisplay   _bw{_bwDriver};
    EpdDisplay* _active = &_bw;
    bool        _autoValidated = true;
#else
    GxEPD2_420c_GDEY042Z98 _ssd1683Driver{
        PIN_EPD_CS, PIN_EPD_DC, PIN_EPD_RST, PIN_EPD_BUSY
    };
    GxEPD2_420c_NM_UC8179 _uc8179Driver{
        PIN_EPD_CS, PIN_EPD_DC, PIN_EPD_RST, PIN_EPD_BUSY
    };
    Ssd1683Display _ssd1683{_ssd1683Driver};
    Uc8179Display  _uc8179{_uc8179Driver};
    EpdDisplay*    _active = &_ssd1683;
    bool           _isUc8179 = false;
    bool           _autoValidated = false;
#endif

    struct BusyProbe {
        volatile bool run = false;
        volatile uint32_t activeMs = 0;
        uint8_t activeLevel = HIGH;
    };

    static void _busyProbeTask(void* arg) {
        auto* probe = static_cast<BusyProbe*>(arg);
        pinMode(PIN_EPD_BUSY, INPUT);
        while (probe->run) {
            if (digitalRead(PIN_EPD_BUSY) == probe->activeLevel) {
                probe->activeMs = probe->activeMs + 1;
            }
            vTaskDelay(1);
        }
        vTaskDelete(nullptr);
    }

    void _primeControlPins() {
        pinMode(PIN_EPD_CS, OUTPUT);
        digitalWrite(PIN_EPD_CS, HIGH);
        pinMode(PIN_EPD_DC, OUTPUT);
        digitalWrite(PIN_EPD_DC, HIGH);
        pinMode(PIN_EPD_RST, OUTPUT);
        digitalWrite(PIN_EPD_RST, HIGH);
        pinMode(PIN_EPD_BUSY, INPUT_PULLUP);
    }

    void _initActive(bool initialPowerOn) {
        _primeControlPins();
        _active->init(115200, initialPowerOn, 2, false);
    #if NM_EPD_420_BW
        _active->epd2.selectFastFullUpdate(EPD_FAST_FULL_UPDATE != 0);
    #elif !NM_EPD_420_4C && !NM_EPD_075_3C
        _active->epd2.selectFastFullUpdate(_isUc8179 ? false : (EPD_FAST_FULL_UPDATE != 0));
    #endif
        _active->setRotation(0);
    }

    void _selectDriver() {
        gpio_hold_dis((gpio_num_t)PIN_EPD_RST);
    #if NM_EPD_075_3C
        _active = static_cast<EpdDisplay*>(&_panel75);
        s_busyActiveLevel = LOW;
        _autoValidated = true;
        Serial.println("[EPDDetect] mode=3C_750c_Z08_GD7965");
    #elif NM_EPD_420_4C
        _active = static_cast<EpdDisplay*>(&_fourColor);
        s_busyActiveLevel = LOW;
        _autoValidated = true;
        Serial.println("[EPDDetect] mode=4C_GDEY0420F51_HX8717");
    #elif NM_EPD_420_BW
        _active = static_cast<EpdDisplay*>(&_bw);
        s_busyActiveLevel = HIGH;
        _autoValidated = true;
        Serial.println("[EPDDetect] mode=BW_GYE042A87");
    #else
#if EPD_DRIVER_MODE == 2
        _isUc8179 = _detectIsUc8179();
        Serial.printf("[EPDDetect] mode=AUTO select=%s\n", _isUc8179 ? "UC8179" : "SSD1683");
#elif EPD_DRIVER_MODE == 1
        _isUc8179 = true;
        Serial.println("[EPDDetect] mode=FORCE_UC8179");
#else
        _isUc8179 = false;
        Serial.println("[EPDDetect] mode=FORCE_SSD1683");
#endif
        _active = _isUc8179 ? static_cast<EpdDisplay*>(&_uc8179)
                            : static_cast<EpdDisplay*>(&_ssd1683);
        s_busyActiveLevel = _isUc8179 ? LOW : HIGH;
        _autoValidated = false;
#endif
        Serial.flush();
    }

#if !NM_EPD_420_BW && !NM_EPD_420_4C && !NM_EPD_075_3C
    bool _detectIsUc8179() {
        pinMode(PIN_EPD_BUSY, INPUT_PULLUP);
        delay(2);

        pinMode(PIN_EPD_RST, OUTPUT);
        digitalWrite(PIN_EPD_RST, HIGH);
        delay(5);
        digitalWrite(PIN_EPD_RST, LOW);
        delay(10);
        digitalWrite(PIN_EPD_RST, HIGH);
        pinMode(PIN_EPD_BUSY, INPUT_PULLUP);

        uint8_t lowCount = 0;
        uint8_t highCount = 0;
        int16_t firstLowAt = -1;
        for (uint8_t i = 0; i < 80; ++i) {
            if (digitalRead(PIN_EPD_BUSY) == LOW) {
                ++lowCount;
                if (firstLowAt < 0) firstLowAt = i;
            } else {
                ++highCount;
            }
            delay(1);
        }

        const bool isUc8179 = (lowCount < 12);
        Serial.printf("[EPDDetect] BUSY low=%u high=%u firstLow=%d => %s\n",
                      lowCount, highCount, firstLowAt,
                      isUc8179 ? "UC8179" : "SSD1683");
        return isUc8179;
    }
#endif

    static constexpr uint16_t _accentColor() {
#if NM_EPD_420_BW
        return GxEPD_BLACK;
#elif NM_EPD_420_4C
    return GxEPD_YELLOW;
#else
        return GxEPD_RED;
#endif
    }

    bool _renderWelcomeInternal(bool validateBusy) {
        auto& epd = raw();
        BusyProbe probe;
        TaskHandle_t probeTask = nullptr;

        if (validateBusy) {
            probe.run = true;
#if NM_EPD_420_BW
            probe.activeLevel = HIGH;
#elif NM_EPD_420_4C || NM_EPD_075_3C
            probe.activeLevel = LOW;
#else
            probe.activeLevel = _isUc8179 ? LOW : HIGH;
#endif
            xTaskCreatePinnedToCore(_busyProbeTask, "epdBusy", 2048,
                                    &probe, 1, &probeTask, 0);
        }

        epd.setFullWindow();
    const uint32_t refreshStartedAt = millis();
        epd.firstPage();
        do {
            epd.fillScreen(GxEPD_WHITE);
            epd.setFont(&FreeSansBold18pt7b);
            epd.setTextColor(GxEPD_BLACK);
            _printCentered(BOARD_NAME, 68);

            epd.setFont(&FreeSans9pt7b);
            char fwLine[40];
            snprintf(fwLine, sizeof(fwLine), "Factory Test FW %s", FW_VERSION);
            _printCentered(fwLine, 95);

            epd.drawLine(10, 112, DISP_W - 10, 112, GxEPD_BLACK);

            epd.setFont(&FreeSansBold9pt7b);
            _printCentered("Press USER button to Start", 170);
        } while (epd.nextPage());
        _logRefreshTime("welcome screen", refreshStartedAt);

        if (!validateBusy) {
            return true;
        }

        probe.run = false;
        delay(5);
    #if NM_EPD_420_BW
        Serial.printf("[EPDDetect] validate driver=BW_GYE042A87 activeMs=%lu level=HIGH\n",
                  (unsigned long)probe.activeMs);
    #elif NM_EPD_420_4C
        Serial.printf("[EPDDetect] validate driver=4C_GDEY0420F51 activeMs=%lu level=LOW\n",
                  (unsigned long)probe.activeMs);
    #elif NM_EPD_075_3C
        Serial.printf("[EPDDetect] validate driver=3C_750c_Z08 activeMs=%lu level=LOW\n",
                  (unsigned long)probe.activeMs);
    #else
        Serial.printf("[EPDDetect] validate driver=%s activeMs=%lu level=%s\n",
                      _isUc8179 ? "UC8179" : "SSD1683",
                      (unsigned long)probe.activeMs,
                      _isUc8179 ? "LOW" : "HIGH");
    #endif
        return probe.activeMs >= 20;
    }

    void _printCentered(const char* str, int16_t y) {
        auto& epd = raw();
        int16_t  x1, y1;
        uint16_t w, h;
        epd.getTextBounds(str, 0, y, &x1, &y1, &w, &h);
        int16_t cx = (DISP_W - (int16_t)w) / 2 - x1;
        epd.setCursor(cx, y);
        epd.print(str);
    }

    void _drawTestContent(
        uint8_t     testNum,
        const char* title,
        const char* const lines[],
        uint8_t     lineCount,
        const char* result,
        const char* prompt,
        bool        linesLeftAlignedBlock = false,
        uint8_t     monospaceStartLine    = 255)
    {
        auto& epd = raw();

        char idx[5];
        snprintf(idx, sizeof(idx), "T%u", (unsigned)testNum);

        epd.setFont(&FreeSansBold18pt7b);
        epd.setTextColor(_accentColor());
        {
            int16_t  x1, y1;
            uint16_t tw, th;
            char idxSpace[8];
            snprintf(idxSpace, sizeof(idxSpace), "%s  ", idx);
            epd.getTextBounds(idxSpace, 0, 36, &x1, &y1, &tw, &th);
            int16_t idxW = (int16_t)tw;

            epd.setFont(&FreeSans9pt7b);
            epd.getTextBounds(title, 0, 36, &x1, &y1, &tw, &th);
            int16_t titleW = (int16_t)tw;

            int16_t totalW = idxW + titleW;
            int16_t startX = (DISP_W - totalW) / 2;

            epd.setFont(&FreeSansBold18pt7b);
            epd.setTextColor(_accentColor());
            epd.setCursor(startX, 36);
            epd.print(idx);

            epd.setFont(&FreeSans9pt7b);
            epd.setTextColor(GxEPD_BLACK);
            epd.setCursor(startX + idxW, 36);
            epd.print(title);
        }

        epd.drawLine(10, 48, DISP_W - 10, 48, GxEPD_BLACK);

        epd.setTextColor(GxEPD_BLACK);
        const uint8_t count = (lineCount > MAX_LINES) ? MAX_LINES : lineCount;
        const int16_t rowDy = (count > 7) ? 17 : 22;
        const int16_t y0    = (count > 7) ? 70 : 74;

        auto fontFor = [&](uint8_t i) -> const GFXfont* {
            return (i >= monospaceStartLine) ? &FreeMono9pt7b : &FreeSans9pt7b;
        };

        if (linesLeftAlignedBlock) {
            int16_t maxW = 0;
            int16_t maxX1 = 0;
            for (uint8_t i = 0; i < count; i++) {
                if (i < monospaceStartLine) continue;
                if (!lines || !lines[i] || lines[i][0] == '\0') continue;
                epd.setFont(fontFor(i));
                int16_t  x1, y1;
                uint16_t w, h;
                epd.getTextBounds(lines[i], 0, 0, &x1, &y1, &w, &h);
                if ((int16_t)w > maxW) {
                    maxW = (int16_t)w;
                    maxX1 = x1;
                }
            }
            int16_t startX = (DISP_W - maxW) / 2 - maxX1;
            for (uint8_t i = 0; i < count; i++) {
                if (!lines || !lines[i] || lines[i][0] == '\0') continue;
                epd.setFont(fontFor(i));
                if (i < monospaceStartLine) {
                    epd.setTextColor(GxEPD_BLACK);
                    _printCentered(lines[i], y0 + i * rowDy);
                } else {
                    const char* line = lines[i];
                    const char* failTag = strstr(line, "[FAIL]");
                    epd.setCursor(startX, y0 + i * rowDy);
                    if (failTag) {
                        epd.setTextColor(GxEPD_BLACK);
                        for (const char* p = line; p < failTag; p++) epd.print(*p);
                        epd.setTextColor(_accentColor());
                        epd.print("[FAIL]");
                        epd.setTextColor(GxEPD_BLACK);
                        epd.print(failTag + 6);
                    } else {
                        epd.setTextColor(GxEPD_BLACK);
                        epd.print(line);
                    }
                }
            }
        } else {
            for (uint8_t i = 0; i < count; i++) {
                if (lines && lines[i] && lines[i][0] != '\0') {
                    epd.setFont(fontFor(i));
                    _printCentered(lines[i], y0 + i * rowDy);
                }
            }
        }

        if (result) {
            epd.drawLine(10, 244, DISP_W - 10, 244, GxEPD_BLACK);
            bool isGood = (strncmp(result, "PASS", 4) == 0 ||
                           strncmp(result, "SKIP", 4) == 0);
            epd.setTextColor(isGood ? GxEPD_BLACK : _accentColor());
            epd.setFont(&FreeSansBold9pt7b);
            char resultLine[24];
            snprintf(resultLine, sizeof(resultLine), "[ %s ]", result);
            _printCentered(resultLine, 264);
        }

        if (prompt) {
            epd.setFont(&FreeSans9pt7b);
            epd.setTextColor(GxEPD_BLACK);
            _printCentered(prompt, 285);
        }
    }
};
