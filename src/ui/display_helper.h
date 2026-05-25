#pragma once

#include <Arduino.h>
#include <SPI.h>
#include <GxEPD2_3C.h>
#include <gdey3c/GxEPD2_420c_GDEY042Z98.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSansBold9pt7b.h>
#include <Fonts/FreeSansBold18pt7b.h>
#include <Fonts/FreeMono9pt7b.h>

#include "config.h"

// ─── EPD type alias ──────────────────────────────────────────────────────────
using EpdDisplay = GxEPD2_3C<
    GxEPD2_420c_GDEY042Z98,
    EPD_PAGE_HEIGHT
>;

// ─── Screen layout constants ──────────────────────────────────────────────────
static constexpr int16_t DISP_W = 400;
static constexpr int16_t DISP_H = 300;

// ─── Display helper ───────────────────────────────────────────────────────────
// Wraps GxEPD2 and provides factory-test–specific drawing helpers.
// All draw* functions execute a full-window refresh and block until done.

class Display {
public:
    // ── Init ──────────────────────────────────────────────────────────────────
    void init() {
        SPI.begin(PIN_EPD_SCK, PIN_EPD_MISO, PIN_EPD_MOSI, PIN_EPD_CS);
        _epd.init(115200, /*initial_power_on=*/true, /*reset_duration=*/2, /*pulldown_rst_mode=*/false);
        _epd.epd2.selectFastFullUpdate(EPD_FAST_FULL_UPDATE != 0);
        _epd.setRotation(0);  // 400 wide × 300 tall
    }

    // Re-arm the panel before a new test. We pass initial_power_on=true so
    // GxEPD2 issues a real HW reset pulse on RST and re-runs the full init
    // sequence — without this, the library's internal state can desync after
    // a peripheral test (WiFi RF cal, SD on HSPI, ...) and the next
    // showTestScreen returns almost instantly without driving the panel.
    void resync() {
        _epd.init(115200, /*initial_power_on=*/true, /*reset_duration=*/2, /*pulldown_rst_mode=*/false);
        _epd.epd2.selectFastFullUpdate(EPD_FAST_FULL_UPDATE != 0);
        _epd.setRotation(0);
    }

    // ── T0: Welcome screen ────────────────────────────────────────────────────
    void showWelcome() {
        _epd.setFullWindow();
        _epd.firstPage();
        do {
            _epd.fillScreen(GxEPD_WHITE);

            // Board name — large, centered
            _epd.setFont(&FreeSansBold18pt7b);
            _epd.setTextColor(GxEPD_BLACK);
            _printCentered(BOARD_NAME, 68);

            // FW version — normal, centered
            _epd.setFont(&FreeSans9pt7b);
            char fwLine[40];
            snprintf(fwLine, sizeof(fwLine), "Factory Test FW %s", FW_VERSION);
            _printCentered(fwLine, 95);

            // Divider
            _epd.drawLine(10, 112, DISP_W - 10, 112, GxEPD_BLACK);

            // Instructions — centered
            _epd.setFont(&FreeSansBold9pt7b);
            _printCentered("Press USER button to Start", 170);

        } while (_epd.nextPage());
    }

    // ── Standard test screen ─────────────────────────────────────────────────
    // Renders a complete test screen. Pass nullptr for result/prompt to omit.
    //   testNum   : 1‥11
    //   title     : e.g. "EPD Display Test"
    //   lines[]   : up to MAX_LINES content lines (nullptr entries are skipped)
    //   lineCount : number of entries in lines[]
    //   result    : "PASS" | "FAIL" | "SKIP" | nullptr
    //   prompt    : e.g. "USER=Next" | "USER=PASS  BOOT=FAIL" | nullptr

    static constexpr uint8_t MAX_LINES = 10;

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
        _epd.setFullWindow();
        _epd.firstPage();
        do {
            _epd.fillScreen(GxEPD_WHITE);
            _drawTestContent(testNum, title, lines, lineCount, result, prompt,
                             linesLeftAlignedBlock, monospaceStartLine);
        } while (_epd.nextPage());
    }

    // ── Convenience wrappers ─────────────────────────────────────────────────

    // Show test header + "In progress..." without result/prompt
    void showTestRunning(uint8_t testNum, const char* title,
                         const char* const lines[] = nullptr, uint8_t lineCount = 0)
    {
        showTestScreen(testNum, title, lines, lineCount, nullptr, nullptr);
    }

    // Show final result + prompt after a test completes
    void showTestResult(uint8_t testNum, const char* title,
                        const char* const lines[], uint8_t lineCount,
                        bool pass, const char* prompt = "USER=Next")
    {
        showTestScreen(testNum, title, lines, lineCount,
                       pass ? "PASS" : "FAIL", prompt);
    }

    // ── Hibernate EPD (retains image, minimises power draw) ──────────────────
    void hibernate() { _epd.hibernate(); }

    // ── Raw access ────────────────────────────────────────────────────────────
    EpdDisplay& raw() { return _epd; }

private:
    GxEPD2_420c_GDEY042Z98 _driver{
        PIN_EPD_CS, PIN_EPD_DC, PIN_EPD_RST, PIN_EPD_BUSY
    };
    EpdDisplay _epd{_driver};

    // ── Helper: print text horizontally centered at baseline y ───────────────
    void _printCentered(const char* str, int16_t y) {
        int16_t  x1, y1;
        uint16_t w, h;
        _epd.getTextBounds(str, 0, y, &x1, &y1, &w, &h);
        int16_t cx = (DISP_W - (int16_t)w) / 2 - x1;
        _epd.setCursor(cx, y);
        _epd.print(str);
    }

    // ── Internal layout renderer (called inside firstPage/nextPage loop) ──────
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
        char idx[5];
        snprintf(idx, sizeof(idx), "T%u", (unsigned)testNum);

        // Header: "Tx  <Title>" composed and centered together
        char header[64];
        snprintf(header, sizeof(header), "%s  %s", idx, title);

        // Test index part — large red (draw "Tx" first to measure width)
        _epd.setFont(&FreeSansBold18pt7b);
        _epd.setTextColor(GxEPD_RED);
        // Measure full header width at large font to find starting x, then
        // split into two colored segments centered as one unit.
        {
            int16_t  x1, y1; uint16_t tw, th;
            // Measure "Tx" width
            char idxSpace[8];
            snprintf(idxSpace, sizeof(idxSpace), "%s  ", idx);
            _epd.getTextBounds(idxSpace, 0, 36, &x1, &y1, &tw, &th);
            int16_t idxW = (int16_t)tw;

            // Measure title width at small font
            _epd.setFont(&FreeSans9pt7b);
            _epd.getTextBounds(title, 0, 36, &x1, &y1, &tw, &th);
            int16_t titleW = (int16_t)tw;

            // Total width = idxW + titleW; center start x
            int16_t totalW = idxW + titleW;
            int16_t startX = (DISP_W - totalW) / 2;

            // Draw "Tx" in large red
            _epd.setFont(&FreeSansBold18pt7b);
            _epd.setTextColor(GxEPD_RED);
            _epd.setCursor(startX, 36);
            _epd.print(idx);

            // Draw title in normal black immediately after
            _epd.setFont(&FreeSans9pt7b);
            _epd.setTextColor(GxEPD_BLACK);
            _epd.setCursor(startX + idxW, 36);
            _epd.print(title);
        }

        // Header divider
        _epd.drawLine(10, 48, DISP_W - 10, 48, GxEPD_BLACK);

        // Content lines — start Y and row spacing adapt to row count so
        // up to MAX_LINES rows still fit above the result divider (y=244).
        // Default: each line individually centered.
        // If linesLeftAlignedBlock=true: compute the widest line, then start
        // every line at the same X so the *block* is centered while each
        // line is left-aligned (looks tidy for tabular summaries).
        // monospaceStartLine: lines with index >= this value use FreeMono9pt7b
        // (so columns align by character position). 0 = all monospace,
        // 255 = all proportional.
        _epd.setTextColor(GxEPD_BLACK);
        const uint8_t count = (lineCount > MAX_LINES) ? MAX_LINES : lineCount;
        const int16_t rowDy = (count > 7) ? 17 : 22;
        const int16_t y0    = (count > 7) ? 70 : 74;

        auto fontFor = [&](uint8_t i) -> const GFXfont* {
            return (i >= monospaceStartLine) ? &FreeMono9pt7b
                                             : &FreeSans9pt7b;
        };

        if (linesLeftAlignedBlock) {
            // Compute widest among the monospace block (lines >= monospaceStartLine).
            int16_t maxW = 0;
            int16_t maxX1 = 0;
            for (uint8_t i = 0; i < count; i++) {
                if (i < monospaceStartLine) continue;  // header lines drawn separately
                if (!lines || !lines[i] || lines[i][0] == '\0') continue;
                _epd.setFont(fontFor(i));
                int16_t  x1, y1; uint16_t w, h;
                _epd.getTextBounds(lines[i], 0, 0, &x1, &y1, &w, &h);
                if ((int16_t)w > maxW) { maxW = (int16_t)w; maxX1 = x1; }
            }
            int16_t startX = (DISP_W - maxW) / 2 - maxX1;
            for (uint8_t i = 0; i < count; i++) {
                if (!lines || !lines[i] || lines[i][0] == '\0') continue;
                _epd.setFont(fontFor(i));
                if (i < monospaceStartLine) {
                    // Header line — keep centered with its proportional font.
                    _epd.setTextColor(GxEPD_BLACK);
                    _printCentered(lines[i], y0 + i * rowDy);
                } else {
                    // If the line ends with "[FAIL]", split-render so the
                    // FAIL tag stands out in red while the rest stays black.
                    const char* line = lines[i];
                    const char* failTag = strstr(line, "[FAIL]");
                    _epd.setCursor(startX, y0 + i * rowDy);
                    if (failTag) {
                        _epd.setTextColor(GxEPD_BLACK);
                        for (const char* p = line; p < failTag; p++) _epd.print(*p);
                        _epd.setTextColor(GxEPD_RED);
                        _epd.print("[FAIL]");
                        _epd.setTextColor(GxEPD_BLACK);
                        _epd.print(failTag + 6);
                    } else {
                        _epd.setTextColor(GxEPD_BLACK);
                        _epd.print(line);
                    }
                }
            }
        } else {
            for (uint8_t i = 0; i < count; i++) {
                if (lines && lines[i] && lines[i][0] != '\0') {
                    _epd.setFont(fontFor(i));
                    _printCentered(lines[i], y0 + i * rowDy);
                }
            }
        }

        // Result / prompt area
        if (result) {
            _epd.drawLine(10, 244, DISP_W - 10, 244, GxEPD_BLACK);

            bool isGood = (strncmp(result, "PASS", 4) == 0 ||
                           strncmp(result, "SKIP", 4) == 0);
            _epd.setTextColor(isGood ? GxEPD_BLACK : GxEPD_RED);
            _epd.setFont(&FreeSansBold9pt7b);
            char resultLine[24];
            snprintf(resultLine, sizeof(resultLine), "[ %s ]", result);
            _printCentered(resultLine, 264);
        }

        if (prompt) {
            _epd.setFont(&FreeSans9pt7b);
            _epd.setTextColor(GxEPD_BLACK);
            _printCentered(prompt, 285);
        }
    }
};
