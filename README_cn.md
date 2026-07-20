# NM-EPD-420

**NM-EPD-420** 是一款基于 **ESP32-S3** 的 4.2 英寸三色电子墨水屏开发板，集成 Wi-Fi / BLE、LoRa、音频编解码、温湿度传感、SD 卡、电池管理等常用外设，适合作为低功耗信息牌、 weather station、Meshtastic 终端、Dashboard 等项目的硬件载体。

> 本仓库包含的固件是 **工厂测试固件**，仅用于帮助产线或开发者在拿到板子后快速验证所有硬件功能是否正常。详细的测试序列、操作流程和编译方法见 [第 4 章](#4-工厂测试固件)。
>
> English version: [README.md](README.md)

---

## 1. 开发板概览

NM-EPD-420 把构建电子墨水屏项目所需的核心资源集成到一块板上：

* **主控**：ESP32-S3（16 MB Flash，PSRAM，双核 240 MHz，支持 2.4 GHz Wi-Fi 与 BLE 5）
* **显示屏**：4.2" 400×300 三色电子墨水屏（黑 / 白 / 红），型号 **GDEY042Z98**
* **音频**：ES8311 音频编解码 + 外部 D 类功放 + 板载喇叭，LMD4737 PDM 数字麦克风
* **环境传感**：AHT20 温湿度传感器（带独立电源开关）
* **无线扩展**：预留 SX126x 系列 LoRa 模组接口（与 SD 卡共享 SPI 总线）（可选项）
* **存储与接口**：µSD 卡槽、USER / BOOT 按键、电池 ADC、JST 1.25 2-Pin 电池接口
* **低功耗设计**：各外设模组拥有独立使能引脚，可整体断电后进入 ESP32 深度睡眠

你可以直接运行下方已适配的开源项目，也可以根据本章给出的引脚定义，把 NM-EPD-420 当作一块通用 ESP32-S3 载板，快速启动自己的应用。

---

## 2. 已支持的开源项目

以下项目已经在 NM-EPD-420 上完成适配，直接拉取对应分支并编译即可运行：

| 项目 | 简介 | 适配仓库/分支 |
|------|------|---------------|
| **Meshtastic** | 基于 LoRa 的离网 mesh 通信，可在 4.2" 墨水屏上显示节点信息、消息和传感器数据（HT-RA62模组，SX1262） | [RockBase-iot/meshtastic-firmware@`nm-epd-420`](https://github.com/RockBase-iot/meshtastic-firmware/tree/nm-epd-420) |
| **TRMNL-Firmware** | TRMNL 电子墨水屏内容框架，支持定时从服务器拉取图片/内容并显示 | [RockBase-iot/trmnl-firmware@`nm-epd-420`](https://github.com/RockBase-iot/trmnl-firmware/tree/nm-epd-420) |
| **Biscuit** | 面向电子墨水屏设备的多功能工具/娱乐固件 | [RockBase-iot/biscuit@`master`](https://github.com/RockBase-iot/biscuit/tree/master) |
| **ESP32-weather-epd** | 低功耗天气站，从 OpenWeatherMap 获取天气并在墨水屏展示 | [RockBase-iot/esp32-weather-epd@`main`](https://github.com/RockBase-iot/esp32-weather-epd/tree/main) |
| **ESP32-Dashboard** | 多功能电子墨水屏 Dashboard：天气、空气质量、室内温湿度、Web 配网等 | [RockBase-iot/ESP32-Dashboard@`main`](https://github.com/RockBase-iot/ESP32-Dashboard/tree/main) |

---

## 3. 硬件资源与引脚定义

### 3.1 系统框图

```
                      ┌───────────────────────────────────┐
                      │             ESP32-S3              │
                      │  (16 MB Flash, PSRAM, BLE/Wi-Fi)  │
                      └───────────────────────────────────┘
        SPI2 (FSPI) ──┐    SPI3 (HSPI) ──┐    I²C ──┐    I²S ──┐
                      ▼                  ▼          ▼          ▼
              ┌──────────────┐    ┌──────────┐  ┌──────┐  ┌────────┐
              │ EPD GDEY042  │    │ µSD card │  │AHT20 │  │ ES8311 │ → PA → 喇叭
              │   Z98 (3C)   │    │  + LoRa  │  └──────┘  │  codec │ ← DMIC LMD4737
              │  400×300 4.2"│    │  modem   │            │        │
              └──────────────┘    └──────────┘            └────────┘

  ┌────────┐  ┌────────┐  ┌────────────┐
  │  USER  │  │  BOOT  │  │  电池 ADC  │
  │ 按键   │  │ 按键   │  │ IO43+IO3  │
  └────────┘  └────────┘  └────────────┘
```

### 3.2 主要器件清单

| 模块         | 型号                          | 接口            | 备注                                |
|--------------|-------------------------------|-----------------|-------------------------------------|
| 主控         | **ESP32-S3** (qio_opi PSRAM)  | —               | 16 MB Flash，双核 240 MHz           |
| 墨水屏       | **GDEY042Z98** 4.2" 400×300   | SPI (FSPI)      | 三色 黑/白/红，GxEPD2 驱动          |
| 编解码       | **ES8311**                    | I²C 0x18 + I²S  | DAC → 外部功放 → 8 Ω 喇叭           |
| 麦克风       | **LMD4737** PDM DMIC          | I²S (DMIC 模式) | 采样率 16 kHz                       |
| 温湿度       | **AHT20**                     | I²C 0x38        | 通过 `PIN_TEMP_CTL` 上电使能        |
| SD 卡        | µSD                           | SPI (HSPI)      | 与 LoRa 共享总线                    |
| LoRa 模组    | HT-RA62 **SX1262** 系列               | SPI (HSPI)      | CS / RST / BUSY / DIO1 引脚         |
| 按键         | USER, BOOT                    | GPIO            | 低有效，外部上拉                    |
| 音频功放     | 外部 D 类                     | EN GPIO         | `PIN_PA_CTRL` 拉高使能              |

### 3.3 ESP32-S3 GPIO 定义

> 以下引脚定义以代码为唯一依据：[src/config.h](src/config.h)。

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
|                 | LoRa DIO1        | 14   | IN   | 默认仅在特定 LoRa 应用中使用              |
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
| 模块使能        | LoRa EN          | 47   | OUT  | LoRa 模块上电使能（HIGH 使能）            |
|                 | Codec EN         | 44   | OUT  | ES8311 上电使能（HIGH 使能）              |
|                 | ADC EN           | 43   | OUT  | 电池 ADC 电路使能（HIGH 使能）            |
| 电池 ADC        | BATT_ADC         | 3    | IN   | 电池分压采样输入                          |

### 3.4 配件与电源

* **3D 外壳**：STL 文件位于 [docs/case](docs/case)，包含按键、顶板、背盖等组件，可供3D打印机直接打印。
* **电池**：推荐使用 3.7 V 锂聚合物电池，容量 ≥ 500 mAh，带保护板。PCB 预留 JST 1.25 PH 2-Pin 插座（红线正、黑线负），推荐尺寸 603030。[Buy in AliExpress JST 1.25 2Pin 603030 600mAh](https://www.aliexpress.com/item/32853151195.html)

---

## 4. 工厂测试固件

本仓库中的固件按固定顺序（T0…T11）依次激活并测试板上每一个外设。每一项测试都会在墨水屏上渲染一张专用画面，由操作员通过 **USER** 与 **BOOT** 两个按键给出“通过 / 失败”判定，最后在汇总页（T11）以表格形式列出每一项的 PASS / FAIL / SKIP 结果。

### 4.1 测试序列

| Test | 项目              | 描述                                          | 画面 |
|------|-------------------|-----------------------------------------------|------|
| T0   | 系统启动          | 串口 / EPD 初始化、欢迎页、等待 USER 按键     | ![T0](image/T0.png) |
| T1   | EPD 屏显          | 全白 / 全黑 / 全红 + 文本渲染示例             | — |
| T3   | 按键              | USER 与 BOOT 按键检测                         | — |
| T4   | ES8311 编解码     | 500/1k/2k/3k Hz 扫频 + 《欢乐颂》旋律         | ![T4](image/T4.png) |
| T5   | DMIC 麦克风       | 录音 + 喇叭回环 + RMS 阈值                    | ![T5](image/T5.png) |
| T6   | AHT20 温湿度      | I²C 读取温湿度                                | ![T6](image/T6.png) |
| T7   | 电池 ADC          | IO3 采样电池分压（IO43 使能）                 | — |
| T8   | Wi-Fi 扫描        | 2.4 GHz AP 扫描，期望 ≥ 1 个网络              | ![T8](image/T8.png) |
| T9   | SD 卡读写         | HSPI 挂载 + 写入再回读校验                    | ![T9](image/T9.png) |
| T10  | LoRa SPI 总线     | 复位 LoRa，检查 BUSY 拉低                     | — |
| T11  | 汇总              | 各项 PASS/FAIL/SKIP 表 + EPD 休眠 + 深度睡眠  | ![T11](image/T11.png) |

完整一轮测试约 3 分钟，主要时间花在墨水屏的全刷上（三色屏每张约 10 秒）。

### 4.2 操作流程

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

按键采用软件去抖（5 次 × 10 ms 采样），且只有当 EPD `BUSY=LOW` 时才接收按键，避免操作员在墨水屏刷新过程中按键，导致按键事件被下一项测试错误吞掉。

### 4.3 编译与下载

环境要求：PlatformIO Core（命令行）或 VS Code + PlatformIO 插件。

```powershell
# 在仓库根目录执行
$env:IDF_GITHUB_ASSETS = "dl.espressif.cn/github_assets"   # 可选，国内镜像
pio run                                                    # 编译
pio run --target upload --upload-port COM38                # 烧录
pio device monitor --baud 115200                           # 串口
```

首次编译会下载 ESP-IDF 工具链（数百 MB）到 `%USERPROFILE%\.platformio`，后续编译约 25 秒。

测试运行时串口典型输出：

```
[FACTORY TEST] Board: NM-EPD-420
[FACTORY TEST] FW: v1.4.01
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

## 5. 基于 NM-EPD-420 进行二次开发

这张开发板本质上是一块外设齐全的 ESP32-S3 载板。你可以：

1. 从 [第 2 章](#2-已支持的开源项目) 挑选一个已适配项目，直接拉取对应分支编译；
2. 新建自己的 PlatformIO / Arduino 工程，把下方引脚配置复制到项目的 `config.h` 或 `platformio.ini`；
3. 按需初始化 SPI / I²C / I²S 总线，注意 **EPD 独占 FSPI，SD + LoRa 共享 HSPI**；
4. 使用各外设前，先拉高对应的模块使能引脚，使用完毕拉低以节省功耗；
5. 调用 `esp_deep_sleep_start()` 等 API 进入低功耗模式。

### 5.1 常用外设初始化要点

* **墨水屏**：推荐 `zinggjm/GxEPD2` 库，使用 `GxEPD2_420c_GDEY042Z98` 驱动；CS=46, DC=4, RST=5, BUSY=6。
* **AHT20**：使用 `Adafruit AHTX0`；I²C SDA=39, SCL=38；读取前将 `PIN_TEMP_CTL(40)` 置高。
* **ES8311 / 喇叭 / 麦克风**：I²C 地址 0x18，I²S 引脚见上表；播放前拉高 `PIN_CODEC_EN(44)` 和 `PIN_PA_CTRL(41)`。
* **SD 卡**：使用 `SD` 库 + HSPI（SCK=9, MOSI=10, MISO=11, CS=7）。
* **LoRa**：SX126x 系列，HSPI 共享；CS=8, RST=12, BUSY=13, DIO1=14；使用前拉高 `PIN_LORA_EN(47)`。
* **电池 ADC**：采样前拉高 `PIN_ADC_EN(43)`，从 `PIN_BATT_ADC(3)` 读取，分压比为 2:1。

### 5.2 软件架构（测试固件）

如果你需要修改或扩展本仓库的工厂测试固件，源码结构如下：

```
src/
├── main.cpp              ← Arduino 入口；关闭任务看门狗，调用 runner.run()
├── test_runner.{h,cpp}   ← T0/T11、按键封装、EPD 测试前 resync、调度
├── config.h              ← 引脚映射 + 功能开关（二次开发的第一手资料）
├── spi_buses.h           ← SD + LoRa 共享 HSPI 总线初始化
├── ui/
│   └── display_helper.h  ← EPD 包装类 + showWelcome / showTestScreen
└── tests/
    ├── test_t1_epd.h     ← 各测试项 header-only 实现
    ├── …
    └── test_t10_lora.h
```

设计要点：

* **单线程阻塞执行**：产线节奏由人工把控，使用 `delay()` + 同步 SPI/I²S 让代码路径线性，便于现场调试。
* **测试项 header-only 实现**：每个 `test_tN_*.h` 只暴露 `TestResult runTestTN(Display&, TestRunner&)`，由 `test_runner.cpp` 一次性 include。无虚函数、无动态分发。
* **测试前 EPD 重同步**：`TestRunner::_preTest()` 在每项测试前执行 `_epd.init(..., initial_power_on=true, ...)`，避免 Wi-Fi 射频校准、SD 上的 HSPI 操作等共享资源测试结束后让墨水屏驱动失步。
* **按键与 EPD BUSY 互锁**：所有按键采样都做 5×10 ms 去抖，且 `PIN_EPD_BUSY=HIGH` 时直接判定为“未按下”，避免在墨水屏 ~10 s 全刷期间残留按键被错误吞入下一项判定。

### 5.3 框架与依赖

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

---

## 6. 仓库目录

```
NM-EPD-420/
├── README.md            ← 英文版
├── README_cn.md         ← 本文件
├── platformio.ini       ← PlatformIO 构建配置
├── docs/                ← 设计/调试笔记、3D 外壳文件
├── image/               ← 本 README 引用的测试界面截图
│   ├── T0.png  T2.png  T4.png  T5.png  T6.png  T8.png  T9.png  T11.png
└── src/                 ← 全部固件源码
```

---

## 7. 购买渠道

NM-EPD-420 仍在测试完善中，预计 2026 年 8 月正式上架。届时可通过以下渠道购买：

* [Amazon RockBase IoT](https://www.amazon.com/gp/product/B0H1QCHMW6)
* [RockBase IoT Store](https://www.aliexpress.com/store/1105401362)
* [RockBase Shop](https://rockbase.shop)
* [NMTech Global Store](https://www.aliexpress.com/store/1104265822)
