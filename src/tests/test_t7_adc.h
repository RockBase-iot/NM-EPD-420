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

    // Enable battery ADC circuit.
    pinMode(PIN_ADC_EN, OUTPUT);
    digitalWrite(PIN_ADC_EN, HIGH);
    delay(20);  // allow RC network / analog switch to settle

    // 11 dB attenuation: full 0–3300 mV input range on ESP32-S3.
    analogSetAttenuation(ADC_11db);
    delay(5);

    // Average 16 samples to reduce noise.
    uint32_t sum = 0;
    for (int i = 0; i < 16; i++) {
        sum += (uint32_t)analogReadMilliVolts(PIN_BATT_ADC);
        delay(2);
    }
    uint32_t adcMv  = sum / 16;
    uint32_t battMv = adcMv * BATT_ADC_DIV;

    // Disable ADC circuit.
    digitalWrite(PIN_ADC_EN, LOW);

    Serial.printf("[T7] ADC=%u mV  Batt(x%d)=%u mV\n",
                  (unsigned)adcMv, BATT_ADC_DIV, (unsigned)battMv);

    // Auto-pass: ADC reads > 100 mV (circuit is alive and battery connected).
    bool autoPass = (adcMv > 100);

    char lAdc[40], lBatt[40], lAuto[40];
    snprintf(lAdc,  sizeof(lAdc),  "ADC  (IO%d): %4u mV", PIN_BATT_ADC, (unsigned)adcMv);
    snprintf(lBatt, sizeof(lBatt), "Batt (x%d) : %4u mV", BATT_ADC_DIV, (unsigned)battMv);
    snprintf(lAuto, sizeof(lAuto), "Auto: %s", autoPass ? "PASS" : "FAIL (no voltage?)");

    const char* lines[] = { lAdc, lBatt, "", lAuto };
    disp.showTestScreen(7, "Battery ADC",
                        lines, 4,
                        autoPass ? "PASS" : "FAIL",
                        "USER=PASS  BOOT=FAIL");

    bool verdict = runner.waitForVerdict();
    Serial.printf("[T7] END auto=%s op=%s\n",
                  autoPass ? "PASS" : "FAIL", verdict ? "PASS" : "FAIL");
    return (autoPass && verdict) ? TestResult::PASS : TestResult::FAIL;
}

