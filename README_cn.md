# NM-EPD-420

基于 ESP32-S3 的 4.2 英寸三色墨水屏开发板 —— **产线工厂测试固件**。

固件按固定顺序（T0…T11）依次激活并测试板上每一个外设，每一项测试都会在墨水屏上
渲染一张专用画面，由操作员通过 **USER** 与 **BOOT** 两个按键给出“通过/失败”判定。
最后在汇总页（T11）以表格形式列出每一项的 PASS / FAIL / SKIP 结果。

> English version: [README.md](README.md)

---

## 1. 这个固件做什么

* 上电后自动初始化所有外设驱动
* 顺序运行 10 项硬件测试，每项之间阻塞等待操作员判定（`USER = OK/PASS`，`BOOT = FAIL`）
* 在三色（黑/白/红）墨水屏上为每项测试渲染独立画面
* 通过 ES8311 + 板载喇叭播放扫频纯音 + 经典旋律（贝多芬《欢乐颂》）
* 通过 LMD4737 PDM 麦克录音并计算 RMS 值，验证录音通路
* 探测 I²C 设备、扫描 Wi-Fi、挂载 SD 卡、初始化 LoRa 模组等
* 在墨水屏汇总页和串口同时输出结果，串口结尾给出可被产线 MES 解析的
  `FACTORY_TEST=OK` 或 `FACTORY_TEST=FAIL:T4,T9` 字样

---

## 2. 测试序列

| Test | 项目              | 描述                                          | 画面 |
|------|-------------------|-----------------------------------------------|------|
| T0   | 系统启动          | 串口/EPD 初始化、欢迎页、等待 USER 按键         | ![T0](image/T0.png) |
| T1   | EPD 屏显          | 全白/全黑/全红 + 文本渲染示例                 | — |
| T3   | 按键              | USER 与 BOOT 按键检测                           | — |
| T4   | ES8311 编解码     | 500/1k/2k/3k Hz 扫频 + 《欢乐颂》旋律         | ![T4](image/T4.png) |
| T5   | DMIC 麦克         | 录音 + 喙叭回环 + RMS 阀值                    | ![T5](image/T5.png) |
| T6   | AHT20 温湿度      | I²C 读取温湿度                                | ![T6](image/T6.png) |
| T7   | 电池 ADC          | IO3 采样电池分压（IO43 使能）                  | — |
| T8   | Wi-Fi 扫描        | 2.4 GHz AP 扫描，期望 ≥ 1 个网络              | ![T8](image/T8.png) |
| T9   | SD 卡读写         | FSPI 挂载 + 写入再回读校验                    | ![T9](image/T9.png) |
| T10  | LoRa SPI 总线     | 复位 LoRa，检查 BUSY 拉低                     | — |
| T11  | 汇总              | 各项 PASS/FAIL/SKIP 表 + EPD 休眠 + 深度睡眠       | ![T11](image/T11.png) |

完整一轮测试约 3 分钟，主要时间花在墨水屏的全刷上（三色屏每张约 10 秒）。

---

## 3. 操作流程

```
                  ┌───────────────────────────┐
   上 电   ───►  │  T0  欢迎页               │  按 USER
                  └───────────────────────────┘
                             │
                             ▼
                  ┌───────────────────────────┐
                  │  T1 … T10                 │
                  │  每一项：                 │
                  │    显示画面               │
                  │    执行硬件动作           │
                  │    USER = PASS / OK       │
                  │    BOOT = FAIL            │
                  └───────────────────────────┘
                             │
                             ▼
                  ┌───────────────────────────┐
                  │  T11  汇总页              │  EPD 休眠 → ESP32 深度睡眠
                  └───────────────────────────┘
```

按键采用软件去抖（5 次 × 10 ms 采样），且只有当 EPD `BUSY=LOW` 时才接收按键，
避免操作员在墨水屏刷新过程中按键，导致按键事件被下一项测试错误吞掉。

---

## 4. 硬件资源

### 4.1 系统框图

```
                      ┌───────────────────────────────────┐
                      │             ESP32-S3              │
                      │  (16 MB Flash, PSRAM, BLE/Wi-Fi)  │
                      └───────────────────────────────────┘
        SPI2 (FSPI) ──┐    SPI3 (HSPI) ──┐    I²C ──┐    I²S ──┐
                      ▼                  ▼          ▼          ▼
              ┌──────────────┐    ┌──────────┐  ┌──────┐  ┌────────┐
              │ EPD GDEY042  │    │ µSD card │  │AHT20 │  │ ES8311 │ → PA → 喇叭
              │   Z98 (3C)   │    │  + LoRa  │  └──────┘  │  codec │
              │  400×300 4.2"│    │  modem   │            │        │ ← DMIC LMD4737
              └──────────────┘    └──────────┘            └────────┘

  ┌────────┐  ┌────────┐  ┌────────────┐
  │  USER  │  │  BOOT  │  │  电池 ADC  │
  │ 按键   │  │ 按键   │  │ IO43+IO3  │
  └────────┘  └────────┘  └────────────┘
```

### 4.2 主要器件清单

| 模块         | 型号                          | 接口            | 备注                               |
|--------------|-------------------------------|-----------------|------------------------------------|
| 主控         | **ESP32-S3** (qio_opi PSRAM)  | —               | 16 MB Flash，双核 240 MHz          |
| 墨水屏       | **GDEY042Z98** 4.2" 400×300   | SPI (FSPI)      | 三色 黑/白/红，GxEPD2 驱动         |
| 编解码       | **ES8311**                    | I²C 0x18 + I²S  | DAC → 外部功放 → 8 Ω 喇叭          |
| 麦克         | **LMD4737** PDM DMIC          | I²S (DMIC 模式) | 采样率 16 kHz                      |
| 温湿度       | **AHT20**                     | I²C 0x38        | 通过 `PIN_TEMP_CTL` 上电使能       |
| SD 卡        | µSD                           | SPI (HSPI)      | 与 LoRa 共享总线                   |
| LoRa 模组    | **SX126x** 系列               | SPI (HSPI)      | CS / RST / BUSY 引脚               |
| 按键         | USER, BOOT                      | GPIO            | 低有效，外部上拉                   |
| 音频功放     | 外部 D 类                     | EN GPIO         | `PIN_PA_CTRL` 拉高使能             |

### 4.3 ESP32-S3 GPIO 定义

| 分组            | 信号             | GPIO | 方向 | 备注                                      |
|-----------------|------------------|------|------|-------------------------------------------|
| EPD (FSPI)      | SCK              | 2    | OUT  |                                           |
|                 | MOSI             | 1    | OUT  |                                           |
|                 | MISO             | —    | —    | 屏端 NC（只写）                           |
|                 | CS               | 46   | OUT  |                                           |
|                 | DC               | 4    | OUT  |                                           |
|                 | RST              | 5    | OUT  |                                           |
|                 | BUSY             | 6    | IN   | 刷新过程中为 HIGH                         |
| SD + LoRa (HSPI)| SCK              | 9    | OUT  | 共享总线                                  |
|                 | MOSI             | 10   | OUT  |                                           |
|                 | MISO             | 11   | IN   |                                           |
|                 | SD CS            | 7    | OUT  |                                           |
|                 | LoRa NSS         | 8    | OUT  |                                           |
|                 | LoRa RST         | 12   | OUT  |                                           |
|                 | LoRa BUSY        | 13   | IN   |                                           |
|                 | LoRa DIO1        | 14   | IN   | 本固件未使用                              |
| I²S (ES8311)    | MCLK             | 21   | OUT  | 4.096 MHz (256 × 16 kHz)                  |
|                 | BCLK             | 15   | OUT  |                                           |
|                 | LRCK / WS        | 17   | OUT  |                                           |
|                 | DOUT (ESP→DAC)   | 18   | OUT  | ES8311 DSDIN                              |
|                 | DIN  (ADC→ESP)   | 16   | IN   | ES8311 ASDOUT                             |
| I²C             | SDA              | 39   | I/O  | AHT20 + ES8311 共享总线                   |
|                 | SCL              | 38   | OUT  |                                           |
|                 | TEMP_CTL         | 40   | OUT  | AHT20 上电控制（HIGH 使能）               |
| 音频            | PA_CTRL          | 41   | OUT  | 外部功放使能                              |
| 用户 I/O        | USER 按键        | 45   | IN   | 低有效，外部上拉                          |
|                 | BOOT 按键        | 0    | IN   | 低有效，RTC GPIO，外部上拉                |
| 模块使能        | LoRa EN          | 47   | OUT  | LoRa 模块上电使能（HIGH 使能）           |
|                 | Codec EN         | 44   | OUT  | ES8311 上电使能（HIGH 使能）             |
|                 | ADC EN           | 43   | OUT  | 电池 ADC 电路使能（HIGH 使能）           |
| 电池 ADC       | BATT_ADC         | 3    | IN   | 电池分压采样输入                          |

> 引脚定义以代码为准：[src/config.h](src/config.h)

---

## 5. 软件架构

```
src/
├── main.cpp              ← Arduino 入口；关闭任务看门狗，调用 runner.run()
├── test_runner.{h,cpp}   ← T0/T11、按键封装、EPD 测试前 resync、调度
├── config.h              ← 引脚映射 + 功能开关
├── spi_buses.h           ← SD + LoRa 共享 HSPI 总线初始化
├── ui/
│   └── display_helper.h  ← EPD 包装类 + showWelcome / showTestScreen
└── tests/
    ├── test_t1_epd.h     ← 各测试项 header-only 实现
    ├── …
    └── test_t10_lora.h
```

设计要点：

* **单线程阻塞执行**：产线节奏由人工把控，使用 `delay()` + 同步 SPI/I²S 让代码
  路径线性，便于现场调试。
* **测试项 header-only 实现**：每个 `test_tN_*.h` 只暴露
  `TestResult runTestTN(Display&, TestRunner&)`，由 `test_runner.cpp` 一次性
  include。无虚函数、无动态分发。
* **测试前 EPD 重同步**：`TestRunner::_preTest()` 在每项测试前执行
  `_epd.init(..., initial_power_on=true, ...)`，避免 Wi-Fi 射频校准、SD 上的
  HSPI 操作等共享资源测试结束后让墨水屏驱动失步。
* **按键与 EPD BUSY 互锁**：所有按键采样都做 5×10 ms 去抖，且 `PIN_EPD_BUSY=HIGH`
  时直接判定为"未按下"，避免在墨水屏 ~10 s 全刷期间残留按键被错误吞入下一项判定。

### 框架与依赖

| 层               | 版本                                            |
|------------------|-------------------------------------------------|
| 构建系统         | **PlatformIO**                                  |
| Platform         | `pioarduino/platform-espressif32` **54.03.21**  |
| Framework        | Arduino-ESP32 **3.2.x**（基于 ESP-IDF 5.4）     |
| 语言             | C++17                                           |

第三方库（详见 [platformio.ini](platformio.ini)）：

| 库                                 | 版本    | 用途       |
|------------------------------------|---------|------------|
| `zinggjm/GxEPD2`                   | 1.6.8   | EPD 驱动   |
| `adafruit/Adafruit AHTX0`          | 2.0.5   | T6 温湿度  |
| `adafruit/Adafruit BusIO`          | 1.17.4  | 间接依赖   |
| `adafruit/Adafruit Unified Sensor` | 1.1.15  | 间接依赖   |
| `SPI`, `Wire`, `WiFi`, `SD`        | 3.2.1   | 自带       |
| `Adafruit GFX Library`             | 1.12.6  | 间接依赖   |

直接使用的 ESP-IDF 组件：`driver/i2s_std.h`（新的 channel-based I²S API，
解决了 ESP32-S3 上老 `i2s.h` 驱动的 MCLK/BCLK 分频错误）、`esp_task_wdt.h`。

---

## 6. 编译与下载

环境要求：PlatformIO Core（命令行）或 VS Code + PlatformIO 插件。

```powershell
# 拉代码后，仓库根目录：
$env:IDF_GITHUB_ASSETS = "dl.espressif.cn/github_assets"   # 可选，国内镜像
pio run                                                    # 编译
pio run --target upload --upload-port COM38                # 烧录
pio device monitor --baud 115200                           # 串口
```

或使用 VS Code 任务：

* **Build (nm-epd-420)**
* **Upload (nm-epd-420)**

首次编译会下载 ESP-IDF 工具链（数百 MB）到 `%USERPROFILE%\.platformio`，
后续编译约 25 秒。

测试运行时串口典型输出：

```
[FACTORY TEST] Board: NM-EPD-420
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

## 7. 移植到其他板子

如果是把这套固件搬到一块外设构成相近的 ESP32-S3 板上，最常需要改的内容是：

1. **`src/config.h`** —— 把每一个 `PIN_*` 宏按你板子的原理图改一遍。这是引脚分配
   的唯一 single source of truth。
2. **`platformio.ini`** —— 如果电气特性不同，把 `board = lilygo-t-display-s3`
   改成对应 board，否则保持不变即可。
3. **EPD 驱动** —— 如果不是 GDEY042Z98（4.2" 三色），修改
   [src/ui/display_helper.h](src/ui/display_helper.h) 的别名：
   ```cpp
   #include <gdey3c/GxEPD2_<your-panel>.h>
   using EpdDisplay = GxEPD2_3C< GxEPD2_<your-panel>, … >;
   ```
   黑白屏改用 `GxEPD2_BW`。
4. **Codec** —— [src/tests/test_t4_codec.h](src/tests/test_t4_codec.h) 中
   `_es8311_init_playback()` 直接写 ES8311 寄存器序列。如果不是 ES8311 则
   整体替换该函数；DAC 数字音量寄存器为 `0x32`，决定整体响度。
5. **麦克风** —— T5 通过 ES8311 DMIC 模式驱动 LMD4737。如果换其他麦，需要
   重写 [src/tests/test_t5_dmic.h](src/tests/test_t5_dmic.h) 的 I²S RX 配置。
6. **功能开关** —— `BATTERY_ADC_AVAILABLE`、`DMIC_RMS_THRESHOLD_*` 在
   `config.h` 里按板子调整。
7. **SPI 总线分配** —— EPD 用默认 `SPI`（FSPI / SPI2）；SD + LoRa 共享的第
   二条总线在 [src/spi_buses.h](src/spi_buses.h) 中初始化。

移植后的快速验证：

* 编译烧录后看串口的 `T1 BUSY self-check: sawHigh=1`。如果是 `sawHigh=0`，
  几乎可以肯定是 BUSY 引脚硬件问题（开路/虚焊/接错 GPIO）。
* 用 T1 第 1 轮（全白）和第 4 轮（文本示例）确认 EPD 驱动型号与 `setRotation`
  正确。
* 如果发现测试间 EPD 失步，检查 `Display::resync()` 在
  [src/ui/display_helper.h](src/ui/display_helper.h) 里的实现。

---

## 8. 仓库目录

```
NM-EPD-420/
├── README.md            ← 英文版
├── README_cn.md         ← 本文件
├── platformio.ini
├── factory_test_plan.md
├── docs/                ← 设计/调试笔记
├── image/               ← 本 README 引用的截屏
│   ├── T0.png  T2.png  T4.png  T5.png  T6.png  T8.png  T9.png  T11.png
└── src/                 ← 全部固件源码
```
