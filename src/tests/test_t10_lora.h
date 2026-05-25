#pragma once
// T10 - LoRa SPI wiring connectivity test (HA-RA62 / SX1262)
// Uses shared peripheral SPI bus (CLK=9, MOSI=10, MISO=11), NSS=8, RST=12,
// BUSY=13. This test verifies pin-level connectivity only:
//   1) CS asserted: fixed register read/write must work
//   2) CS deasserted: same read transaction must NOT return the same data
// No RF Tx/Rx logic is tested.

#include "test_runner.h"
#include "config.h"
#include "spi_buses.h"
#include <SPI.h>

#define T10_DEBUG 1
#if T10_DEBUG
#define T10_LOG(fmt, ...) Serial.printf("[T10] " fmt "\n", ##__VA_ARGS__)
#else
#define T10_LOG(fmt, ...)
#endif

// SX126x opcodes used for register access.
static constexpr uint8_t  SX126X_OP_WRITE_REGISTER = 0x0D;
static constexpr uint8_t  SX126X_OP_READ_REGISTER  = 0x1D;
// LoRa syncword registers (safe to write/read and restore).
static constexpr uint16_t SX126X_REG_SYNCWORD_MSB  = 0x0740;
static constexpr uint16_t SX126X_REG_SYNCWORD_LSB  = 0x0741;

static void _t10_wait_busy_low(uint32_t timeoutMs = 200) {
    uint32_t t0 = millis();
    while (digitalRead(PIN_LORA_BUSY) == HIGH && (millis() - t0) < timeoutMs) {
        delay(1);
    }
}

static void _t10_reset_radio() {
    digitalWrite(PIN_LORA_RST, LOW);
    delay(2);
    digitalWrite(PIN_LORA_RST, HIGH);
    delay(5);
    _t10_wait_busy_low(300);
}

static uint8_t _t10_read_reg(bool assertCs, uint16_t addr) {
    if (assertCs) digitalWrite(PIN_LORA_NSS, LOW);
    spiPeripheral.transfer(SX126X_OP_READ_REGISTER);
    spiPeripheral.transfer((uint8_t)(addr >> 8));
    spiPeripheral.transfer((uint8_t)(addr & 0xFF));
    spiPeripheral.transfer(0x00);  // required dummy byte
    uint8_t v = spiPeripheral.transfer(0x00);
    if (assertCs) digitalWrite(PIN_LORA_NSS, HIGH);
    return v;
}

static void _t10_write_reg(uint16_t addr, uint8_t value) {
    digitalWrite(PIN_LORA_NSS, LOW);
    spiPeripheral.transfer(SX126X_OP_WRITE_REGISTER);
    spiPeripheral.transfer((uint8_t)(addr >> 8));
    spiPeripheral.transfer((uint8_t)(addr & 0xFF));
    spiPeripheral.transfer(value);
    digitalWrite(PIN_LORA_NSS, HIGH);
}

inline TestResult runTestT10(Display& disp, TestRunner& runner) {
    T10_LOG("START");

    // Enable LoRa module.
    pinMode(PIN_LORA_EN, OUTPUT);
    digitalWrite(PIN_LORA_EN, HIGH);
    delay(10);  // allow module to power up

    pinMode(PIN_LORA_NSS, OUTPUT);
    pinMode(PIN_LORA_RST, OUTPUT);
    pinMode(PIN_LORA_BUSY, INPUT);
    digitalWrite(PIN_LORA_NSS, HIGH);
    digitalWrite(PIN_LORA_RST, HIGH);

    _t10_reset_radio();
    _t10_wait_busy_low(200);
    bool busyOk = (digitalRead(PIN_LORA_BUSY) == LOW);
    T10_LOG("BUSY after reset=%d", (int)digitalRead(PIN_LORA_BUSY));

        constexpr int kRounds = 3;
        bool rwAllOk = true;
        bool csAllOk = true;

        for (int i = 0; i < kRounds; i++) {
        // Read original fixed registers, then write/read back toggled values.
        uint8_t origMsb = _t10_read_reg(true, SX126X_REG_SYNCWORD_MSB);
        uint8_t origLsb = _t10_read_reg(true, SX126X_REG_SYNCWORD_LSB);
        uint8_t testMsb = (uint8_t)(origMsb ^ (uint8_t)(0x5A + i));
        uint8_t testLsb = (uint8_t)(origLsb ^ (uint8_t)(0xA5 - i));

        _t10_write_reg(SX126X_REG_SYNCWORD_MSB, testMsb);
        _t10_write_reg(SX126X_REG_SYNCWORD_LSB, testLsb);
        _t10_wait_busy_low(50);

        uint8_t rdMsb = _t10_read_reg(true, SX126X_REG_SYNCWORD_MSB);
        uint8_t rdLsb = _t10_read_reg(true, SX126X_REG_SYNCWORD_LSB);
        bool csReadWriteOk = (rdMsb == testMsb) && (rdLsb == testLsb);

        // Restore original values so test is non-destructive.
        _t10_write_reg(SX126X_REG_SYNCWORD_MSB, origMsb);
        _t10_write_reg(SX126X_REG_SYNCWORD_LSB, origLsb);
        _t10_wait_busy_low(50);

        // Same READ_REGISTER frame but with CS kept HIGH: should not behave like
        // a valid transaction to the LoRa chip.
        uint8_t noCsMsb = _t10_read_reg(false, SX126X_REG_SYNCWORD_MSB);
        uint8_t noCsLsb = _t10_read_reg(false, SX126X_REG_SYNCWORD_LSB);
        bool csGateOk = !((noCsMsb == rdMsb) && (noCsLsb == rdLsb));

        rwAllOk &= csReadWriteOk;
        csAllOk &= csGateOk;

        T10_LOG("r%d orig=%02X%02X test=%02X%02X rd=%02X%02X noCS=%02X%02X rw=%d cs=%d",
            i + 1, origMsb, origLsb, testMsb, testLsb,
            rdMsb, rdLsb, noCsMsb, noCsLsb,
            (int)csReadWriteOk, (int)csGateOk);
        }

        // ── Pin-level result mapping ─────────────────────────────────────
        // rwAllOk: all 3 rounds of SPI register write-then-readback passed
        //          → covers CLK / MOSI / MISO / NSS(asserted)
        // csAllOk: deasserted NSS produced different (invalid) data
        //          → covers NSS CS-gate function
        // busyOk:  BUSY went LOW after RST pulse → covers RST + BUSY pins
        bool pinClk  = rwAllOk;
        bool pinMosi = rwAllOk;
        bool pinMiso = rwAllOk;
        bool pinNss  = rwAllOk && csAllOk;
        bool pinRst  = busyOk;
        bool pinBusy = busyOk;
        bool pass    = pinClk && pinNss && pinRst;

        // Monospace pin table
        char lHdr[40], lClk[40], lMosi[40], lMiso[40], lNss[40], lRst[40], lBusy[40];
        snprintf(lHdr,  sizeof(lHdr),  "Pin             Result");
        snprintf(lClk,  sizeof(lClk),  "CLK   (GPIO%-2d)  %s", PIN_SD_CLK,  pinClk  ? "OK" : "FAIL");
        snprintf(lMosi, sizeof(lMosi), "MOSI  (GPIO%-2d)  %s", PIN_SD_MOSI, pinMosi ? "OK" : "FAIL");
        snprintf(lMiso, sizeof(lMiso), "MISO  (GPIO%-2d)  %s", PIN_SD_MISO, pinMiso ? "OK" : "FAIL");
        snprintf(lNss,  sizeof(lNss),  "NSS   (GPIO%-2d)   %s", PIN_LORA_NSS,  pinNss  ? "OK" : "FAIL");
        snprintf(lRst,  sizeof(lRst),  "RST   (GPIO%-2d)  %s", PIN_LORA_RST,  pinRst  ? "OK" : "FAIL");
        snprintf(lBusy, sizeof(lBusy), "BUSY  (GPIO%-2d)  %s", PIN_LORA_BUSY, pinBusy ? "OK" : "FAIL");

    const char* lines[] = { lHdr, lClk, lMosi, lMiso, lNss, lRst, lBusy };
    disp.showTestScreen(10, "LoRa SPI Bus Test",
                        lines, 7,
                        pass ? "PASS" : "FAIL",
                        "USER=PASS  BOOT=FAIL",
                        /*linesLeftAlignedBlock=*/true,
                        /*monospaceStartLine=*/0);

    bool verdict = runner.waitForVerdict();
    T10_LOG("END auto=%s op=%s", pass ? "PASS" : "FAIL", verdict ? "PASS" : "FAIL");

    // Disable LoRa module when done.
    digitalWrite(PIN_LORA_EN, LOW);

    return (pass && verdict) ? TestResult::PASS : TestResult::FAIL;
}
