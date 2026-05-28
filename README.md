# NM-Display-420

ESP32-S3 based 4.2-inch E-ink display board — **factory test firmware**.

This firmware exercises every on-board peripheral in a fixed sequence (T0…T11),
shows a result page on the EPD for every step, and lets the operator confirm /
reject each step with the **USER** and **BOOT** buttons. At the end a one-page
summary screen lists every test as PASS / FAIL / SKIP.

> 中文版本: [README_cn.md](README_cn.md)

---

## 1. What this firmware does

* Boots the board, initialises every peripheral driver
* Runs 10 hardware tests one after another, blocking between tests for an
  operator verdict (`USER = OK/PASS`, `BOOT = FAIL`)
* Renders a dedicated screen for every test on the 3-color (B/W/R) e-paper
  panel
* Plays audio tones and a recognisable melody (Beethoven *Ode to Joy*) through
  the ES8311 codec + onboard speaker
* Records a short voice clip from the LMD4737 DMIC and computes RMS to verify
  the microphone path
* Probes I²C, scans Wi-Fi, mounts an SD card, talks to the LoRa modem on a
  shared SPI bus, etc.
* Prints a structured summary on the EPD and a machine-parseable
  `FACTORY_TEST=OK` / `FACTORY_TEST=FAIL:T4,T9` line on the serial port for
  production-line MES integration

---

## 2. Test sequence

| Test | Item            | Description                                  | Screen |
|------|-----------------|----------------------------------------------|--------|
| T0   | System startup  | Serial / EPD init, welcome screen, wait USER | ![T0](image/T0.png) |
| T1   | EPD display     | White / Black / Red fill + text demo         | — |
| T3   | Buttons         | USER key and BOOT key press detection          | — |
| T4   | ES8311 codec    | Sweep 500/1k/2k/3k Hz + *Ode to Joy* melody  | ![T4](image/T4.png) |
| T5   | DMIC mic        | Voice record + speaker loopback + RMS check  | ![T5](image/T5.png) |
| T6   | AHT20 sensor    | Temperature & humidity over I²C              | ![T6](image/T6.png) |
| T7   | Battery ADC     | Battery divider voltage on IO3 (enable IO43) | — |
| T8   | Wi-Fi scan      | 2.4 GHz AP scan, expect ≥ 1 network          | ![T8](image/T8.png) |
| T9   | SD card R/W     | FSPI mount + write / read-back verify        | ![T9](image/T9.png) |
| T10  | LoRa SPI bus    | Reset modem, check BUSY low                  | — |
| T11  | Summary         | Per-item PASS/FAIL/SKIP table + deep sleep   | ![T11](image/T11.png) |

A complete run typically takes ~3 min, dominated by EPD full-refresh time
(~10 s per page on a 3-color panel).

---

## 3. Operator workflow

```
                  ┌───────────────────────────┐
   power on  ───► │  T0  Welcome              │  press USER
                  └───────────────────────────┘
                             │
                             ▼
                  ┌───────────────────────────┐
                  │  T1 … T10                 │
                  │  for each test:           │
                  │    show screen            │
                  │    run hardware           │
                  │    USER = PASS / OK       │
                  │    BOOT = FAIL            │
                  └───────────────────────────┘
                             │
                             ▼
                  ┌───────────────────────────┐
                  │  T11  Summary             │  EPD hibernate → ESP32 deep sleep
                  └───────────────────────────┘
```

Buttons are debounced (5 × 10 ms samples) and gated against EPD `BUSY` so a
press during a refresh cannot be consumed as a verdict for the next test.

---

## 4. Hardware

### 4.1 Block diagram

```
                      ┌───────────────────────────────────┐
                      │             ESP32-S3              │
                      │  (16 MB Flash, PSRAM, BLE/Wi-Fi)  │
                      └───────────────────────────────────┘
        SPI2 (FSPI) ──┐    SPI3 (HSPI) ──┐    I²C ──┐    I²S ──┐
                      ▼                  ▼          ▼          ▼
              ┌──────────────┐    ┌──────────┐  ┌──────┐  ┌────────┐
              │ EPD GDEY042  │    │ µSD card │  │AHT20 │  │ ES8311 │ → PA → SPK
              │   Z98 (3C)   │    │  + LoRa  │  └──────┘  │  codec │
              │  400×300 4.2"│    │  modem   │            │        │ ← DMIC LMD4737
              └──────────────┘    └──────────┘            └────────┘

  ┌────────┐  ┌────────┐  ┌──────────────┐
  │  USER  │  │  BOOT  │  │ Battery ADC  │
  │ button │  │ button │  │  IO43+IO3    │
  └────────┘  └────────┘  └──────────────┘
```

### 4.2 Bill of components

| Block        | Part                          | Interface       | Notes                              |
|--------------|-------------------------------|-----------------|------------------------------------|
| MCU          | **ESP32-S3** (qio_opi PSRAM)  | —               | 16 MB flash, dual-core 240 MHz     |
| E-paper      | **GDEY042Z98** 4.2" 400×300   | SPI (FSPI)      | 3-color B/W/R, GxEPD2 driver       |
| Codec        | **ES8311**                    | I²C 0x18 + I²S  | DAC out → external PA → 8 Ω SPK    |
| Mic          | **LMD4737** PDM DMIC          | I²S (DMIC mode) | Sample rate 16 kHz                 |
| T/RH sensor  | **AHT20**                     | I²C 0x38        | Power-gated via `PIN_TEMP_CTL`     |
| SD card      | µSD                           | SPI (HSPI)      | Shared bus with LoRa               |
| LoRa modem   | **SX126x** family             | SPI (HSPI)      | CS / RST / BUSY GPIOs              |
| Buttons      | USER, BOOT                      | GPIO            | Active LOW, external pull-up       |
| Audio amp    | External Class-D              | EN GPIO         | Enabled by `PIN_PA_CTRL` HIGH      |

### 4.3 ESP32-S3 GPIO map

| Group        | Signal           | GPIO | Direction | Notes                                   |
|--------------|------------------|------|-----------|-----------------------------------------|
| EPD (FSPI)   | SCK              | 2    | OUT       |                                         |
|              | MOSI             | 1    | OUT       |                                         |
|              | MISO             | —    | —         | Panel pin NC (write-only)               |
|              | CS               | 46   | OUT       |                                         |
|              | DC               | 4    | OUT       |                                         |
|              | RST              | 5    | OUT       |                                         |
|              | BUSY             | 6    | IN        | HIGH while refresh in progress          |
| SD + LoRa (HSPI) | SCK          | 9    | OUT       | Shared bus                              |
|              | MOSI             | 10   | OUT       |                                         |
|              | MISO             | 11   | IN        |                                         |
|              | SD CS            | 7    | OUT       |                                         |
|              | LoRa NSS         | 8    | OUT       |                                         |
|              | LoRa RST         | 12   | OUT       |                                         |
|              | LoRa BUSY        | 13   | IN        |                                         |
|              | LoRa DIO1        | 14   | IN        | Not used by this firmware               |
| I²S (ES8311) | MCLK             | 21   | OUT       | 4.096 MHz (256 × 16 kHz)                |
|              | BCLK             | 15   | OUT       |                                         |
|              | LRCK / WS        | 17   | OUT       |                                         |
|              | DOUT (ESP→DAC)   | 18   | OUT       | DSDIN on ES8311                         |
|              | DIN  (ADC→ESP)   | 16   | IN        | ASDOUT on ES8311                        |
| I²C          | SDA              | 39   | I/O       | AHT20 + ES8311 shared bus               |
|              | SCL              | 38   | OUT       |                                         |
|              | TEMP_CTL         | 40   | OUT       | AHT20 power gate (HIGH = on)            |
| Audio        | PA_CTRL          | 41   | OUT       | External amplifier enable               |
| User I/O     | USER button      | 45   | IN        | Active LOW, external pull-up            |
|              | BOOT button      | 0    | IN        | Active LOW, RTC GPIO, external pull-up  |
| Module EN    | LoRa EN          | 47   | OUT       | LoRa module power enable (HIGH = on)    |
|              | Codec EN         | 44   | OUT       | ES8311 power enable (HIGH = on)         |
|              | ADC EN           | 43   | OUT       | Battery ADC circuit enable (HIGH = on)  |
| Battery ADC  | BATT_ADC         | 3    | IN        | Battery voltage sense (resistor divider)|

> Authoritative source: [src/config.h](src/config.h)

---

## 5. Software architecture

```
src/
├── main.cpp              ← Arduino entry; disables task WDT, calls runner.run()
├── test_runner.{h,cpp}   ← T0/T11, button helpers, EPD pre-test resync, dispatch
├── config.h              ← board pin map + feature switches
├── spi_buses.h           ← shared HSPI bus init for SD + LoRa
├── ui/
│   └── display_helper.h  ← EPD wrapper + showWelcome / showTestScreen
└── tests/
    ├── test_t1_epd.h     ← header-only test implementations
    ├── …
    └── test_t10_lora.h
```

Design notes:

* **Single-threaded, blocking.** The factory line is human-paced; using
  `delay()` and synchronous SPI/I²S keeps the code linear and easy to debug.
* **Tests are header-only.** Each `test_tN_*.h` exposes
  `TestResult runTestTN(Display&, TestRunner&)` and is included once from
  `test_runner.cpp`. No virtual dispatch.
* **EPD resync between tests.** `TestRunner::_preTest()` re-runs
  `_epd.init(..., initial_power_on=true, ...)` so peripheral tests that touch
  shared resources (Wi-Fi RF cal, HSPI for SD, …) cannot leave the EPD in a
  desynced state.
* **Button gating against EPD BUSY.** All button reads are debounced (5 × 10 ms)
  and rejected while `PIN_EPD_BUSY` is HIGH, so a press during the ~10 s
  full-window refresh cannot leak into the next verdict.

### Frameworks & libraries

| Layer               | Version                                        |
|---------------------|------------------------------------------------|
| Build system        | **PlatformIO**                                 |
| Platform            | `pioarduino/platform-espressif32` **54.03.21** |
| Framework           | Arduino-ESP32 **3.2.x** (on top of ESP-IDF 5.4)|
| Language            | C++17                                          |

Library deps (see [platformio.ini](platformio.ini)):

| Library                            | Version | Used by   |
|------------------------------------|---------|-----------|
| `zinggjm/GxEPD2`                   | 1.6.8   | EPD       |
| `adafruit/Adafruit AHTX0`          | 2.0.5   | T6 sensor |
| `adafruit/Adafruit BusIO`          | 1.17.4  | (dep)     |
| `adafruit/Adafruit Unified Sensor` | 1.1.15  | (dep)     |
| `SPI`, `Wire`, `WiFi`, `SD`        | 3.2.1   | bundled   |
| `Adafruit GFX Library`             | 1.12.6  | (dep)     |

ESP-IDF components used directly: `driver/i2s_std.h` (channel-based new I²S
API — fixes MCLK/BCLK divider issues on ESP32-S3 that the legacy `i2s.h`
driver had), `esp_task_wdt.h`.

---

## 6. Build & flash

Prerequisites: PlatformIO Core (or VS Code + PlatformIO extension).

```powershell
# clone, then in repo root:
$env:IDF_GITHUB_ASSETS = "dl.espressif.cn/github_assets"   # optional, China mirror
pio run                                                    # build
pio run --target upload --upload-port COM38                # flash
pio device monitor --baud 115200                           # serial console
```

Or one-shot via VS Code tasks:

* **Build (nm-display-420)**
* **Upload (nm-display-420)**

The first build downloads the ESP-IDF toolchain (~hundreds of MB) into
`%USERPROFILE%\.platformio`. Subsequent builds take ~25 s.

Serial output during a run:

```
[FACTORY TEST] Board: NM Display 4.2 Inch
[FACTORY TEST] FW: v1.3.10
[FACTORY TEST] T0 - System startup OK
…
[FACTORY TEST] T1 START - EPD Display
[T1] Round 1/4 - Filling screen WHITE ...
[T1] BUSY self-check: sawHigh=1  highMs=2873
…
[FACTORY TEST] ===== SUMMARY =====
[FACTORY TEST] T1   EPD Display    [PASS]
…
[FACTORY TEST] Overall: FACTORY_TEST=OK
```

---

## 7. Porting to a different board

If you are reusing this firmware on another ESP32-S3 carrier with a similar
peripheral set, the most likely changes are:

1. **`src/config.h`** — change every `PIN_*` macro to match your schematic.
   That is the single source of truth for pin assignment.
2. **`platformio.ini`** — change `board = lilygo-t-display-s3` to the
   appropriate board (or stay with this one if your carrier is electrically
   compatible).
3. **EPD driver** — if the panel is not GDEY042Z98 (4.2" 3-color), update the
   alias in [src/ui/display_helper.h](src/ui/display_helper.h):
   ```cpp
   #include <gdey3c/GxEPD2_<your-panel>.h>
   using EpdDisplay = GxEPD2_3C< GxEPD2_<your-panel>, … >;
   ```
   For B/W panels switch to `GxEPD2_BW`.
4. **Codec** — `_es8311_init_playback()` in
   [src/tests/test_t4_codec.h](src/tests/test_t4_codec.h) writes the ES8311
   register sequence directly. If your codec is different, replace this
   function. The DAC volume register `0x32` controls loudness.
5. **Mic** — T5 talks to ES8311 in DMIC mode (clock for an LMD4737 chip).
   If you use a different microphone, retarget the I²S RX setup in
   [src/tests/test_t5_dmic.h](src/tests/test_t5_dmic.h).
6. **Feature switches** — toggle `BATTERY_ADC_AVAILABLE` and the DMIC RMS
   thresholds in `config.h` to suit your board.
7. **SPI bus assignment** — the EPD uses the default `SPI` (FSPI / SPI2);
   SD + LoRa share the second bus initialised in [src/spi_buses.h](src/spi_buses.h).

After porting:

* Build, flash, watch the serial log for `T1 BUSY self-check: sawHigh=1`.
  If that prints `sawHigh=0`, you have a BUSY-pin wiring problem (open net,
  cold solder joint, wrong GPIO).
* Use T1 *Round 1 (WHITE)* / *Round 4 (text demo)* to validate the EPD
  driver instance and rotation.
* If pre-test EPD resync misbehaves on your board, look at
  `Display::resync()` in [src/ui/display_helper.h](src/ui/display_helper.h).

---

## 8. Repository layout

```
NM-Display-420/
├── README.md            ← this file
├── README_cn.md         ← 中文版
├── platformio.ini
├── factory_test_plan.md
├── docs/                ← misc design notes
├── image/               ← screen captures shown in this README
│   ├── T0.png  T4.png  T5.png  T6.png  T8.png  T9.png  T11.png
└── src/                 ← all firmware sources
```
