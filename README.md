# dnx RTOS

## Overview
The dnx RTOS is a general purpose operating system based on the FreeRTOS
kernel. The dnx layer is modeled on well-known Unix architecture. A destination
of the system are small microcontrollers supported by the FreeRTOS kernel,
especially 32-bit. The system is easy scalable to the user's needs, the user
can write own drivers, virtual devices, programs and so on. The program layer
is mostly compatible with the C standard. You can say that the dnx RTOS is a
FreeRTOS kernel distribution.

This repository contains only the operating system (kernel). Programs and
libraries are not part of the kernel – they are built as a separate image
called "application set" (appset). The appset, common libraries, board support
packages, configuration and build tools can be found in the
[dnx-rtos-workspace](https://github.com/devdnl/dnx-rtos-workspace) repository,
where this repository is included as the `kernel` git submodule. The workspace is
the recommended way to use the kernel.

## The minimal hardware requirements
There are minimal hardware requirements that allows to start system but with
very minimalistic software base:

- 64 KiB Flash memory,
- 20 KiB RAM memory.

The hardware requirements depends on the user's configuration.

## The system features
- Unix-like architecture: devices are visible as files in `/dev`, file systems
  are mounted in a single directory tree (VFS layer), pipes, FIFOs and standard
  streams,
- processes and threads; possibility to run many copies of the same program,
- simple porting of PC programs for the dnx (due to the C standard compatibility),
- programs are stored in separate application set image(s) linked independently
  of the kernel; the kernel finds programs through the appset table,
- the first process (`initd` by default) is started by the kernel and is
  responsible for initializing drivers, mounting file systems and starting
  user programs,
- simple drivers layer; drivers are initialized and released at runtime,
- interrupts are not masked by the system (the RTOS feature),
- dynamic memory allocation with simple garbage collector for programs (memory,
  files and other resources are released when a program exits),
- heap and process consistency check; a crashed program is killed instead of
  rebooting the entire system, kernel panic report is kept over
  reset,
- optional shared memory,
- system log (`dmesg`),
- multi-core support (SMP, RP2040),
- supports many CPU architectures (the FreeRTOS feature),
- modular design, everything is configurable by text-mode configuration tool.

## Supported architectures
| Architecture | Core              | Example microcontrollers        |
|--------------|-------------------|---------------------------------|
| `stm32f1`    | Cortex-M3         | STM32F103                       |
| `stm32f3`    | Cortex-M4F        | STM32F303                       |
| `stm32f4`    | Cortex-M4F        | STM32F429                       |
| `stm32f7`    | Cortex-M7         | STM32F746, STM32F756            |
| `stm32h7`    | Cortex-M7         | STM32H747, STM32H753            |
| `rpcm0`      | Cortex-M0+ (dual) | RP2040 (Raspberry Pi Pico)      |

## Drivers
Architecture independent (`noarch`) drivers are available on every platform.

| Driver   | Description                          | Architectures |
|----------|--------------------------------------|---------------|
| ADC      | analog-digital converter             | stm32f1 |
| AFM      | alternative function manager         | stm32f1, stm32f4, stm32f7, stm32h7 |
| CAN      | CAN bus                              | stm32f1, stm32f3, stm32f4, stm32f7, stm32h7 |
| CLK      | clock configuration                  | all |
| CRC      | CRC calculation unit                 | stm32f1, stm32f4 |
| DCI      | digital camera interface             | stm32f4 |
| DMA      | direct memory access                 | stm32f1, stm32f3, stm32f4, stm32f7, stm32h7 |
| EMI      | external memory interface (SDRAM)    | stm32f4, stm32f7, stm32h7 |
| ETH      | Ethernet MAC                         | stm32f1, stm32f4, stm32f7, stm32h7 |
| GPIO     | general purpose I/O                  | all |
| I2C      | I2C bus                              | stm32f1, stm32f3, stm32f4, stm32f7, stm32h7 |
| IRQ      | external interrupts                  | stm32f1, stm32f3, stm32f4, stm32f7 |
| LTDC     | LCD-TFT display controller           | stm32f7, stm32h7 |
| NVM      | internal flash memory                | stm32f1, stm32f4, stm32f7, stm32h7 |
| PWM      | pulse width modulation               | stm32f1, stm32f3, stm32f4, stm32f7 |
| QSPI     | Quad-SPI flash memory                | stm32f4, stm32h7 |
| RTC      | real time clock                      | stm32f1, stm32f4, stm32f7, stm32h7 |
| SDMMC    | SD card interface (SDIO)             | stm32f4, stm32f7, stm32h7 |
| SND      | sound interface (SAI)                | stm32f4 |
| SPI      | SPI bus                              | stm32f1, stm32f4, stm32f7, stm32h7 |
| TIMENC   | timer based encoder                  | stm32f7 |
| UART     | serial port                          | all |
| USBD     | USB device                           | stm32f1 |
| USBH     | USB host (mass storage)              | stm32f4 |
| WDG      | watchdog                             | stm32f1, stm32f4, stm32f7 |
| DHT11    | DHT11 humidity/temperature sensor    | noarch |
| ENC28J60 | SPI Ethernet controller              | noarch |
| FBRIDGE  | file bridge                          | noarch |
| I2CEE    | I2C EEPROM                           | noarch |
| LOOP     | loop device                          | noarch |
| MEMBLOCK | memory region as block device        | noarch |
| PART     | partitions (MBR) of block device     | noarch |
| SDSPI    | SD card over SPI                     | noarch |
| SPIEE    | SPI EEPROM                           | noarch |
| TTY      | terminal (virtual terminals)         | noarch |
| URANDOM  | pseudo random number source          | noarch |
| ZERO     | zero source                          | noarch |

## File systems
- **ramfs** – file system in RAM (usually root file system),
- **procfs** – information about processes and system,
- **romfs** – read-only file system stored in FPKG container (e.g. in flash
  memory, created by `tools/mkfpkg.py`),
- **fatfs** – FAT12/16/32 and exFAT (based on ChaN FatFs R0.14),
- **ext4fs** – EXT2/3/4 (based on lwext4),
- **eefs** – file system for EEPROM memories,
- **flatfs** – simple, damage resistant file system for flash memories,
- pipes and FIFOs.

## Network
- **inet** – TCP/IP stack (based on lwIP),
- **cannet** – network over CAN bus,
- **sipc** – simple serial protocol for point-to-point communication,
- **uds** – Unix domain sockets.

Network is accessed by programs via sockets API.

## The project configuration
The kernel is normally configured and built from the
[dnx-rtos-workspace](https://github.com/devdnl/dnx-rtos-workspace) repository
(`make config-app-kernel`, `make build-app-kernel`), where configuration is
stored outside the kernel directory and passed by the `CONFIG_DIR` variable.

To configure the kernel standalone (configuration in `./config`), type in the
terminal:
```
make config
```
The configuration tool is a text-mode (`dialog` based) application written in
Lua (`tools/configtool-cli`). Configuration is stored in header files in the
`config` directory.

To compile the project, type in the terminal:
```
make
```
or with configuration from other directory:
```
make CONFIG_DIR=<path-to-config>
```
Output files (`dnx.elf`, `dnx.hex`, `dnx.bin`) are placed in the
`build/output` directory.

Other targets:
- `make clean` – remove build files,
- `make doc` – create Doxygen documentation,
- `make check`, `make quickcheck` – static code analysis by using cppcheck,
- `./tools/tests.sh` – build the kernel for all test configurations
  (`tools/test_bsp`); the current configuration is restored afterwards,
- `make help` – list of all targets.

Required tools: GNU make, `arm-none-eabi-` toolchain, Python 3, Lua 5.4 or newer
with LuaFileSystem (`lfs`) and `dialog` (configuration tool). All of them are
included in the Docker image provided by the workspace.

## Adding drivers and file systems
Drivers and file systems are added to the build automatically – each driver is
a directory in `src/drivers` and each file system is a directory in `src/fs`.
Templates of a new driver and a new file system can be created by scripts:
```
./tools/adddriver.sh <driver-name> <architecture> [brief] [author] [email]
./tools/addfilesystem.sh <filesystem-name> [brief] [author] [email]
```

## Folder structure
- `build`  - the project's binaries (created after compilation)
- `config` - the project's default configuration
- `doc`    - documentation (coding style, license, Doxygen configuration)
- `src`    - the project's sources:
  - `config`  - configuration headers used by sources,
  - `cpu`     - CPU ports (startup, CPU control, CMSIS),
  - `drivers` - drivers,
  - `fs`      - file systems and VFS,
  - `include` - kernel headers,
  - `init`    - kernel entry point,
  - `kernel`  - FreeRTOS kernel, dnx ports, process management, syscalls,
    kernel panic, kworker,
  - `lib`     - kernel internal libraries,
  - `mm`      - memory management (heap, shared memory),
  - `net`     - network layer (inet, cannet, sipc, uds)
- `tools`  - the project's tools (configuration tool, driver and file system
  templates, FPKG image builder, test BSPs, etc.)

## License
The dnx RTOS is distributed under the GNU General Public License with exception
that allows to distribute combined work that includes dnx RTOS without being
obliged to provide the source code for proprietary components. See
`doc/license.txt` for details. Third party software (FreeRTOS, lwIP, FatFs,
lwext4, CMSIS, etc.) is licensed separately.
