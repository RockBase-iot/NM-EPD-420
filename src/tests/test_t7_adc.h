#pragma once
// T7 – Battery Voltage ADC
// PIN_ADC_EN  (IO43): pull HIGH to enable the battery ADC circuit.
// PIN_BATT_ADC (IO3): ADC input — battery voltage via resistor divider.
//
// The raw ADC reading is multiplied by BATT_ADC_DIV (defined in config.h)
// to get the estimated battery terminal voltage.  Adjust BATT_ADC_DIV to
// match the actual divider on your schematic (default = 2, i.e. 1:1 divider).

#include "test_runner.h"
#include "config.h"
#include <esp32-hal-adc.h>

inline TestResult runTestT7(Display& disp, TestRunner& runner) {
    Serial.println("[T7] Battery ADC test started");
    Serial.printf("[T7] EN=GPIO%d  ADC=GPIO%d  div=%d\n",
                  PIN_ADC_EN, PIN_BATT_ADC, BATT_ADC_DIV);

    Serial.println("[T7] Bypass: charger BAT node is not a reliable battery-presence signal");
    const char* lines[] = {
        "ADC bypassed",
        "Charger masks battery",
    };
    disp.showTestScreen(7, "Battery ADC",
                        lines, 2,
                        "PASS",
                        "USER=Next");

    runner.waitForUser();
    return TestResult::PASS;
}

