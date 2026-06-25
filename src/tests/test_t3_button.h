#pragma once
// T3 鈥?Button test (USER key GPIO45, BOOT key GPIO0)

#include "test_runner.h"

// Wait for a single button to be pressed; no timeout in factory mode.
static bool _t3_waitButton(bool (*isPressed)()) {
    while (!isPressed()) {
        delay(10);
    }
    delay(50);  // debounce
    while (isPressed()) { delay(10); }  // wait for release
    return true;
}

inline TestResult runTestT3(Display& disp, TestRunner& runner) {
    Serial.println("[T3] Button Test started");
    Serial.println("[T3] USER=GPIO45  BOOT=GPIO0  waiting indefinitely");

    bool apOk   = false;
    bool bootOk = false;

    // 鈹€鈹€ Phase 1: USER key 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€
    {
        const char* lines[] = {
            "Test each button when prompted.",
            "",
            ">>> Press USER key (GPIO45) now <<<",
            "",
            "BOOT key: [ waiting ]",
        };
        disp.showTestScreen(3, "Button Test", lines, 5, nullptr, nullptr);
        Serial.println("[T3] Waiting for USER key...");

        apOk = _t3_waitButton(TestRunner::userPressed);
        Serial.print("[T3] USER key: ");
        Serial.println(apOk ? "OK" : "FAIL");
    }

    // 鈹€鈹€ Phase 2: BOOT key 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€
    {
        char apStatus[32];
        snprintf(apStatus, sizeof(apStatus), "USER key (GPIO45): [%s]", apOk ? " OK  " : "FAIL ");
        const char* lines[] = {
            apStatus,
            "",
            ">>> Press BOOT key (GPIO0) now <<<",
        };
        disp.showTestScreen(3, "Button Test", lines, 3, nullptr, nullptr);
        Serial.println("[T3] Waiting for BOOT key...");

        bootOk = _t3_waitButton(TestRunner::bootPressed);
        Serial.print("[T3] BOOT key: ");
        Serial.println(bootOk ? "OK" : "FAIL");
    }

    // 鈹€鈹€ Result 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€
    bool pass = apOk && bootOk;

    char apLine[32], bootLine[32];
    snprintf(apLine,   sizeof(apLine),   "USER key (GPIO45): [%s]", apOk   ? " OK  " : "FAIL ");
    snprintf(bootLine, sizeof(bootLine), "BOOT key (GPIO0):  [%s]", bootOk ? " OK  " : "FAIL ");
    const char* resultLines[] = { apLine, bootLine };

    disp.showTestScreen(3, "Button Test",
                        resultLines, 2,
                        pass ? "PASS" : "FAIL",
                        "USER=Next");

    if (pass) {
        Serial.println("[T3] PASS");
    } else {
        Serial.println("[T3] FAIL - one or more buttons timed out");
    }

    runner.waitForUser();
    return pass ? TestResult::PASS : TestResult::FAIL;
}
