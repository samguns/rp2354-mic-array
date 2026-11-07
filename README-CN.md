# RP2354A 麦克风阵列 — Zephyr

English: [README.md](README.md)

定制 RP2354A 板上有四只同步的 IM72D128V01 PDM 麦克风。Zephyr、它的模块和 SDK 都在本仓库的 `zephyrproject/` 里。开源硬件与原理图工程见 [嘉立创EDA 专业版](https://pro.lceda.cn/editor#id=b3406129141d4149bf3fd8f035bb6d27)。构建目标是 `rpi_pico2/rp2350a/m33`；`app.overlay` 设置这颗芯片的 2 MiB Flash、引脚连接和 USB 功能。参见 [RP2354A 板](docs/rp2354a-board.md)。

## USB

一个全速复合设备，VID:PID `2fe3:0006`，产品名 `Pico 2 Mic Array UAC2`，制造商 `Pico Array Prototype`，配置名 `Audio, shell and telemetry`：

| 功能 | USB 接口 | Windows 名称 |
|---|---|---|
| 诊断 shell | CDC 0 / data 1 | Mic Array Shell |
| MIC/2 遥测与控制 | CDC 2 / data 3 | Mic Array Telemetry |
| 四通道 48 kHz PCM16 | UAC2 麦克风 | Pico 2 Mic Array UAC2 |

COM 口由电脑分配。`mic_app` 根据 VID、PID 和接口选择遥测口，再用 MIC/2 握手确认。UAC2 与两个串口各自独立。录四路原始通道时使用 WASAPI 独占模式；Windows 共享混音器给出的是立体声。参见 [CDC 遥测](docs/cdc-telemetry-service.md)。

没有已保存的音频记录时，USB 输出就是四路麦克风。波束成形、降噪和方向估计都关闭。已保存的记录会在采集开始前恢复降噪模式、哼声滤波、输入通道、波束模式、方位角、假定仰角，以及学到的 129 个频点噪声谱。方向估计不在这份记录里，启用之前保持关闭。保存方式是 `mic_app` 的 Save & restart，或 MIC/2 命令 `SETTINGS_SAVE`，并且只在 UAC 空闲时执行。Flash 末尾 8 KiB 放两条交替的 4 KiB 记录，带世代号和 CRC32。普通 UF2 更新会留下这个分区；整片擦除会清掉它。参见 [持久化音频设置](docs/persistent-audio-settings.md)。

`build/zephyr/zephyr_final.map` 中的镜像占用固件分区 2,088,960 字节里的 144,916 字节，以及 532,480 字节 RAM 里的 502,472 字节，剩余 30,008 字节。`CONFIG_DMA=n`，因为本应用独占 DMA 控制器。`CONFIG_ASSERT=n` 和 `CONFIG_SPIN_VALIDATE=n` 使调试用的内核检查不进入实时构建。采集序号检查、USB 健康计数、配置范围检查和设置区 CRC 保持开启。

## 信号路径

PIO0 的一个状态机在 GP6 上输出时钟，并同时采样 GP7–GP10。每只麦克风的 SELECT 都接地，因此 CLOCK 为低时数据脚被驱动。程序在十个 PIO 周期的第五个周期采样，位于这个低半周的后段，GPIO 输入同步器保持使能。每个 FIFO 字包含八个按时间排列的 4 比特采样，最早的一组在 bit 0..3，顺序为 U1、U2、U3、U4。

标称 150 MHz 系统时钟下，PIO 分频比是 1250/256：PDM 为 3.072 MHz，按 64 抽取后为每秒 48,000 帧 PCM。频率跟随 12 MHz 晶振。小数分频会带来周期之间的抖动。

两个硬件级联的 DMA 通道填入 24 个缓冲区。每个目的地是 1024 字、4 KiB、按 4 KiB 对齐的区域，写地址在其中回绕。重新装载来迟时，故障处理停掉数据流之前，写操作不会越过这块缓冲。一块数据是 128 个 PCM 帧，即 2.667 ms。缓冲池覆盖 64 ms。丢块、错过重新装载、DMA 总线错误或 PIO RX FIFO 停滞，都会停止采集、使 `faults` 加一，并使进行中的 RAM 录音失效。shell 仍然可用。重新采集需要重启。

抽取：

1. CIC4，抽取 16，用字节查找表做成精确的 61 抽头卷积。Core 1 处理新到的块。
2. 31 抽头半带 FIR，抽取 2，到 96 kHz。Core 0。
3. 63 抽头半带 FIR，抽取 2，到 48 kHz。Core 0。
4. 48 kHz 上的三抽头 CIC 补偿。Core 0。

Core 1 是经 ROM FIFO 握手启动的裸 RP2350 工作核。它不运行 Zephyr，不进中断，也不访问 DMA 或 USB。排队的波束成形和 GCC-PHAT 窗也在这里运行。Core 0 负责调度、FIR、降噪、USB，以及 DMA 块的生命周期。两个核重叠一个块，增加 2.667 ms。原始块要等两个核都完成才释放。热路径上的滤波器代码放在 SRAM。

采集丢弃开头 48 个块，即 128 ms，等待麦克风和 CIC 建立。之后每一块这样使用：

- 方向估计读取波束成形之前的原始四通道 PCM。启动时关闭。一次分析是四个 2048 点的窗，共 8192 帧（170.7 ms）。手动波束成形会跳过下一帧。被拒绝的帧没有角度，估计结果也不会移动波束。参见 [板载方向](docs/onboard-direction.md)。
- 波束成形在 core 1 上运行。Off 让四路原样通过。Broadside 对阵面法向求和。Manual 在下面的 25 mm 十字上做 21 抽头分数延时，公共延时 14 个样本，切换时交叉淡化 20 ms。改变波束会清掉已学习的噪声剖面。参见 [手动波束成形](docs/manual-beamforming.md)。
- 启用降噪时，core 0 在波束成形之后运行，并把单声道结果复制到全部四个 UAC 槽位。Off 保持四路独立。Filter 模式是 80 Hz 高通，以及可选的 50 或 60 Hz 陷波，只处理选定的一路。Suppress 模式是 20 Hz 高通、同样的陷波，以及在 256 点 FFT 的 129 个频点上学习并抑制稳态噪声。Suppress 再增加一个 128 样本的 hop，即 2.667 ms。学习需要两秒；这段时间里不要发出希望保留的声音。参见 [降噪](docs/noise-reduction.md)。

UAC2 使用 2048 帧环形缓冲，开始输出前保留 32 ms（1536 帧）。全速包长为 47、48 或 49 帧，用来跟随主机时钟。UAC 传输的是经过波束成形和降噪的数据流。CDC 快照是波束成形之前的 PCM。

```text
nr config off|filter|suppress [hum=0|50|60] [channel=1..4]
nr learn
nr stats
bf config off|broadside|manual <azimuth-deg> [elevation-deg=0]
bf stats
doa config on|off
doa stats
```

执行 `nr config` 或 `uacpattern` 之前先关闭 UAC。UAC 录音过程中可以改变波束。`bf stats` 打印模式、十分之一度的方位角和仰角、修订号、等待与淡化计数，以及各通道以毫秒为单位的延时。

## 阵列几何

声孔位于 25 mm 的十字上。声孔朝外、USB-C 在右上时：

| 通道 | 麦克风 | GPIO | X、Y（mm） | 位置 |
|---|---|---|---|---|
| 1 | U1 | GP7 | (0, 25) | 上 |
| 2 | U2 | GP8 | (−25, 0) | 左 |
| 3 | U3 | GP9 | (0, −25) | 下 |
| 4 | U4 | GP10 | (25, 0) | 右 |

方位角 0° 在右，90° 在上，180° 在左，270° 在下。仰角 0 在板平面内；90 指向声孔正面的法向，也就是 broadside 方向。仰角是转向时假定的角度。波束成形、方向估计和 `mic_app` 使用同一坐标系。坐标和图见 [阵列几何](docs/array-geometry.md)、[array-geometry.svg](docs/array-geometry.svg) 和 [array-geometry.json](docs/array-geometry.json)。

```powershell
python tools/extract_geometry.py
```

## 接线

GPIO 编号不是 QFN 封装引脚号。来源：`src/board_pins.h` 与 [嘉立创EDA 专业版开源硬件工程](https://pro.lceda.cn/editor#id=b3406129141d4149bf3fd8f035bb6d27)。

| 网络 | GPIO | U5 QFN-60 引脚 | 去向 |
|---|---|---|---|
| SCLK | GP6 | 9 | U1–U4 CLOCK |
| DATA1 | GP7 | 10 | U1，PCM 通道 1 |
| DATA2 | GP8 | 12 | U2，PCM 通道 2 |
| DATA3 | GP9 | 13 | U3，PCM 通道 3 |
| DATA4 | GP10 | 14 | U4，PCM 通道 4 |
| DIN | GP11 | 15 | U7 引脚 3，外部功放 |
| BCLK | GP12 | 16 | U7 引脚 4 |
| LRC | GP13 | 17 | U7 引脚 5 |

U7 的 1、2 脚是该功放的 VBUS 和 GND。U6 是 ME6217C33M5G 3.3 V 稳压器。USB D−/D+ 接到 U5 封装引脚 51 和 52。晶振为 12 MHz。GP0–GP5 空闲；overlay 关闭了 `uart0`、`i2c0`、`i2c1` 和 `adc`。

Flash 是封装内的 2 MiB。固件占用 `0x000000–0x1FDFFF`。`audio-settings` 分区是 `0x1FE000–0x1FFFFF`。

## 构建与烧录

Zephyr、它的模块和 SDK 都留在本仓库的 `zephyrproject/`。`setup.ps1` 和 `setup.sh` 做同样的事：检出 Zephyr `27150c9dd348690a6b41d3992dd5d3d1458fdc66`，按该清单更新模块，应用 `patches/zephyr-udc-rpi-pico.patch`，并安装带 Arm 工具链的 Zephyr SDK 1.0.1。这个补丁确认 USB 帧起始，并避免把已排队的控制传输报成端点错误。`zephyrproject/` 不进入 git。镜像为 `build/zephyr/zephyr.uf2`，CMake 缓存留在 `build/.cache`。

### Windows

安装 Git、Python 3、CMake、Ninja 和 7-Zip，然后安装 west：

```powershell
python -m pip install west
```

`setup.ps1` 下载 `zephyr-sdk-1.0.1_windows-x86_64_minimal.7z`，并运行 `setup.cmd /t arm-zephyr-eabi /h /c`。

```powershell
.\setup.ps1
.\build.ps1
.\build.ps1 -Pristine
```

按住 BOOTSEL 再连接板子，然后把 UF2 复制到出现的大容量存储卷。只复制到这个卷。本固件运行后，在 shell 里执行 `boot` 会重启进入 BOOTSEL，大容量存储和 picoboot 都开启。新镜像枚举之前，串口断开。

```powershell
python tools/flash.py <shell-port>
```

脚本检查 UF2 头，发送 `boot`，并复制到唯一一个 `INFO_UF2.TXT` 含有 `Board-ID: RP2350` 的盘。这样的盘多于一个时，脚本停止。随后它等待同一个 USB 序列号出现在 shell 接口，也就是 Windows 的 `MI_00`。COM 号可能会变。

### Ubuntu

先安装主机软件包，再安装 west。`~/.local/bin` 需要在 `PATH` 里。

```bash
sudo apt install --no-install-recommends git cmake ninja-build gperf ccache dfu-util device-tree-compiler wget curl python3-dev python3-pip python3-setuptools python3-wheel xz-utils file make gcc g++
python3 -m pip install --user west
bash setup.sh
bash build.sh
bash build.sh --pristine
```

`setup.sh` 根据 `uname -m` 下载对应的最小归档，即 `zephyr-sdk-1.0.1_linux-x86_64_minimal.tar.xz` 或 `zephyr-sdk-1.0.1_linux-aarch64_minimal.tar.xz`。然后运行 `zephyrproject/zephyr-sdk-1.0.1/setup.sh -t arm-zephyr-eabi -h -c`。

按住 BOOTSEL 再连接板子，把 `build/zephyr/zephyr.uf2` 复制到 RP2350 的大容量存储卷。`tools/flash.py` 是 Windows 辅助脚本。shell 是一个 CDC ACM 设备；主机工具里原来填写 COM 口的地方，改为对应的 `/dev/ttyACM*` 节点。

## Shell 与录音

CDC 不使用波特率；终端设成 115200 即可。不接终端，采集也会开始。预热结束后，`mic stats` 显示 `ready=1`，`blocks` 持续增加，并且 `dropped=0 faults=0`。`max_process_us` 落在 2.667 ms 的块间隔之内。峰值是最近一块里各通道 PCM16 的绝对值，不是声压级。主机脚本打开端口之前，先关掉终端。

```text
mic stats
mic record 1000
mic status
mic dump
mic raw
uacstats
uacpattern on
doa capture
doa dump
```

`mic record` 接受 1..333 ms，共用诊断缓冲最多放 16,000 帧。主机工具先录音，再读取十六进制转储，并检查头和长度。文本传输没有额外的校验和。已有录音进行时，再次开始会被拒绝。丢块会使该次录音变成 `INVALID`。

```powershell
python -m pip install pyserial
python tools/record.py <shell-port> capture.wav --ms 250
python tools/raw_pdm.py <shell-port> --output build-tests/raw-pdm
```

`tools/raw_pdm.py` 统计 GP6 边沿，导出四路 PDM 共 10.67 ms 以及固件 PCM，再用另一套按位 CIC 和整数 FIR 重建 PCM。`uacpattern` 用诊断斜坡代替麦克风；把录音当作麦克风音频之前先关掉它。`doa capture` 保留一帧原始数据，供 `doa dump` 导出。

连续四通道录音走 UAC2：

```powershell
python tools/uac_wasapi.py --list
python tools/uac_wasapi.py build-tests/continuous-48k.wav --seconds 60
```

WAV 是独占模式下的 48 kHz PCM16。配套 JSON 记录 Windows 数据包标志和位置间隙。未完成的录音留成 `.partial.wav`。

不接板子的主机检查：

```powershell
New-Item -ItemType Directory -Force build-tests | Out-Null
gcc -std=c11 -O2 -Wall -Wextra -Werror -Isrc tests/filter_test.c src/pdm_filter.c -lm -o build-tests/filter_test.exe
.\build-tests\filter_test.exe
python -m unittest discover -s tests -p "test_*.py" -v
```

C 测试把查找表 CIC 与标量参考比较，包括溢出和块边界，再检查通道隔离、极性、直流增益和合成的 sigma-delta 正弦。Python 测试覆盖主机工具。它们不测量引脚时序或片上吞吐。

## 桌面查看器

[`mic_app`](mic_app/README.md) 是 Tauri、Vue 和 TypeScript 应用。在 `mic_app` 里执行 `npm run desktop:build` 之后才会有 `mic_app/release/mic_app.exe`，这个可执行文件不属于源码树。它只打开 Mic Array Telemetry。在这里可以启用方向估计，选择 Off、Broadside 或 Manual，设置方位角和假定仰角，应用波束，并重新学习噪声。Save & restart 写入音频记录并重新连接。仿真画出同一界面，不打开端口。应用连接期间，Audacity 或 `tools/uac_wasapi.py` 可以同时录 UAC。应用使用板子时，让 shell 端口保持关闭。

麦克风：[Infineon IM72D128](https://www.infineon.com/part/IM72D128)。
芯片：[Raspberry Pi RP2350](https://www.raspberrypi.com/documentation/microcontrollers/microcontroller-chips.html)。

## 许可证

本仓库中的固件、主机工具、桌面应用、硬件图和文档使用 MIT 许可证。见 [LICENSE](LICENSE)。

Zephyr 和 Zephyr SDK 是 Apache-2.0。安装脚本会把它们下载到 `zephyrproject/`，该目录不属于本仓库。`patches/zephyr-udc-rpi-pico.patch` 修改的是 Zephyr 的 RP2350 USB 设备驱动，这个补丁文件仍遵循 Apache-2.0。`sdk-benchmark` 使用的 Pico SDK 是工作区里的 `hal_rpi_pico` 模块（BSD-3-Clause）。为 `mic_app` 安装的软件包保留各自的许可证。

`U6.pdf` 和 `speaker.pdf` 是厂商文件，不在这里分发。
