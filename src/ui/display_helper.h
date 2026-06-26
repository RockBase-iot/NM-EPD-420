#pragma once

#include <Arduino.h>
#include <SPI.h>
#ifndef ENABLE_GxEPD2_GFX
#define ENABLE_GxEPD2_GFX 1
#endif
#include <GxEPD2_3C.h>
#include <GxEPD2_GFX.h>
#include <gdey3c/GxEPD2_420c_GDEY042Z98.h>
#include <driver/gpio.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSansBold9pt7b.h>
#include <Fonts/FreeSansBold18pt7b.h>
#include <Fonts/FreeMono9pt7b.h>

#include "config.h"
#include "epd_uc8179_420c.h"

using Ssd1683Display = GxEPD2_3C<GxEPD2_420c_GDEY042Z98, EPD_PAGE_HEIGHT>;
using Uc8179Display  = GxEPD2_3C<GxEPD2_420c_NM_UC8179, EPD_PAGE_HEIGHT>;
using EpdDisplay     = GxEPD2_GFX;

static constexpr int16_t DISP_W = 400;
static constexpr int16_t DISP_H = 300;

class Display {
public:
    static constexpr uint8_t MAX_LINES = 10;

    void init() {
        SPI.begin(PIN_EPD_SCK, PIN_EPD_MISO, PIN_EPD_MOSI, PIN_EPD_CS);
        _selectDriver();
        _initActive(true);
    }

    void resync() {
        _initActive(true);
    }

    void showWelcome() {
        auto& epd = raw();
        epd.setFullWindow();
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
        epd.firstPage();
        do {
            epd.fillScreen(GxEPD_WHITE);
            _drawTestContent(testNum, title, lines, lineCount, result, prompt,
                             linesLeftAlignedBlock, monospaceStartLine);
        } while (epd.nextPage());
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
    bool isUc8179() const { return _isUc8179; }

private:
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

    void _initActive(bool initialPowerOn) {
        _active->init(115200, initialPowerOn, 2, false);
        _active->epd2.selectFastFullUpdate(_isUc8179 ? false : (EPD_FAST_FULL_UPDATE != 0));
        _active->setRotation(0);
    }

    void _selectDriver() {
        gpio_hold_dis((gpio_num_t)PIN_EPD_RST);
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
        Serial.flush();
    }

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
        epd.setTextColor(GxEPD_RED);
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
            epd.setTextColor(GxEPD_RED);
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
                        epd.setTextColor(GxEPD_RED);
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
            epd.setTextColor(isGood ? GxEPD_BLACK : GxEPD_RED);
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
