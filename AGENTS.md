# AGENTS.md — dnx RTOS kernel

Guidance for AI coding agents working in this repository. Read it before
changing code, configuration or build scripts.

## What this repository is

`dnx-rtos-kernel` is the kernel of **dnx RTOS**, a Unix-like OS layer on top
of FreeRTOS for 32-bit microcontrollers (STM32 F1/F3/F4/F7/H7 and RP2040, which
is `rpcm0`). The repository contains only the kernel: CPU ports, drivers, file
systems, network, memory management, process management and syscalls.

Programs and user-space libraries (including the dnx libc) are **not** here.
They live in the [dnx-rtos-workspace](https://github.com/devdnl/dnx-rtos-workspace)
repository, which includes this repository as the `kernel` git submodule and is
the normal way to build it. There the same kernel sources are built twice, as
the application kernel (`config/app-kernel`) and as the bootloader kernel
(`config/btl-kernel`).

Development happens on the `develop` branch.

## Directory map

- `src/init/main.c` — kernel entry point (`main()`, `low_level_init_*`).
- `src/kernel/` — core: `process.c` (processes, threads, appset table),
  `syscall.c` (syscall table and version string), `kworker.c`, `kpanic.c`,
  `printk.c`, `sysapi.c`, `time.c`, `khooks.c`, `kwrapper.c`.
  - `src/kernel/FreeRTOS/` — FreeRTOS sources and `FreeRTOSConfig.h` (third party).
  - `src/kernel/dnx-ports/` — dnx-specific FreeRTOS ports (RP2040 SMP).
- `src/cpu/<arch>/` — CPU ports: startup, CPU control, vendor headers.
  `src/cpu/lib/CMSIS` is third party.
- `src/drivers/<name>/` — drivers. Each has `<name>_ioctl.h`, a `Makefile` and
  per-arch subdirectories (`noarch/`, `stm32f4/`, `stm32fx/`, …).
  `src/drivers/adddriver.sh` is the build-time driver registration generator.
- `src/fs/<name>/` — file systems (ramfs, procfs, romfs, fatfs, ext4fs, eefs,
  flatfs) plus `vfs.c`, `pipe.c`, `fsctrl.c`. `src/fs/addfs.sh` is the
  registration generator.
- `src/net/` — `netm.c` and network families: `inet` (lwIP, third party under
  `inet/lwip`), `cannet`, `sipc`, `uds`.
- `src/mm/` — heap, memory manager, shared memory.
- `src/lib/`, `src/include/lib/` — kernel-internal libraries (printf family,
  llist, crc32, stropt, …). These are not the user-space libc.
- `src/include/` — kernel headers (`drivers/driver.h`, `fs/fs.h`,
  `kernel/sysapi.h`, `kernel/sysfunc.h`, `kernel/errno.h`, …).
- `config/` — default kernel configuration (used only for standalone builds).
- `tools/` — `configtool-cli` (Lua TUI configurator), `configtool.sh`,
  `adddriver.sh` / `addfilesystem.sh` (templates for new modules), `mkfpkg.py`,
  `syncdir.py`, `rungens.sh`, `apply_git_hooks.sh`, `doxygen.sh`, `test_bsp/`.
- `doc/` — `coding_style.txt`, `license.txt`, Doxygen configuration.
- `build/` — output (git-ignored). Never edit or commit it.

## Build

Requirements: `arm-none-eabi-gcc` (10.3-2021.10 in the reference Docker image),
GNU make, Python 3, Lua 5.4 or newer with LuaFileSystem (`lfs`) and `dialog`
(configtool). The supported
environment is the Docker image from the workspace (`tools/Dockerfile` there).

```sh
make                          # build with ./config
make CONFIG_DIR=<path>        # build with configuration from another directory
make clean
make doc                      # Doxygen
make check                    # cppcheck static analysis
make config                   # interactive configtool (needs a terminal, do not run unattended)
```

From the workspace, the kernel is built by `make build-app-kernel` or
`make build-btl-kernel`. Each of these cleans the kernel first and then builds
it with `CONFIG_DIR=../config/<name>`.

Build outputs:
- `build/output/dnx.elf`, `dnx.hex`, `dnx.bin`.
- Generated driver and file system registration in `build/drivers/` and
  `build/fs/`.
- `build/build/defs.h` and `vars.h`, which hold the commit hash, branch name
  and build time.
- `build/ioctl/` — copies of all `<driver>_ioctl.h` files plus
  `ioctl_requests.h`. The workspace appset compiles against these.

Generator scripts listed in `GENERATOR` (e.g. `src/cpu/rpcm0/prepare.sh`) run
before compilation. Never hand-edit generated files.

If the toolchain is not available in your environment, do not install a
different GCC or modify the Makefiles to work around it. Say that the build
could not be verified.

`tools/tests.sh` builds the kernel for every configuration in
`tools/test_bsp/` (stm32f1, stm32f3, stm32f4, stm32f7, stm32h7, rpcm0 and
drvset variants `stm32h7+stm32h7-drvset`, `rpcm0+drvset`: test configuration
with overlay BSP imported after it) and restores the current `config/` at the
end. Use it to check changes in shared
code across architectures. The configtool (`tools/configtool.sh`) needs `lua`
with `lfs` and the `dialog` command (the import fails without them).

Each test BSP contains only the files of its own architecture (plus `noarch`)
and enables every driver available for its CPU (peripheral list `uC.PERIPH`
in `config/arch/arch_flags.h` and all `noarch` drivers except `HOSTDISK`),
all file systems and network stacks, and UART1 in DMA mode. When flags are
added or a BSP is outdated, regenerate it: import the BSP into a clean copy of
`config/`, update flags, export it with
`tools/configtool.sh export=<file>:<dir>/,config` and remove sections of other
architectures (`tools/optimize_bsp.lua <file> <arch>` in the workspace).

## Configuration files (important)

Files under `config/**` (`config.h`, `*_flags.h`) are **hybrid files read by
both the C preprocessor and make**:

- Each flag is named `__FLAG_NAME__` and is defined twice: as a C `#define`
  and as a make assignment inside a `#/* … #*/` block. Keep both in sync.
- Boolean flags use `_YES_` / `_NO_`.
- Comment blocks `#/*-- … --*/` contain configtool (Lua) metadata such as
  `this:AddWidget(...)` and `this:SetToolTip(...)`. Do not reformat or remove
  them.
- Prefer changing configuration through configtool. When editing by hand,
  change only flag values and preserve the file layout.
- Do not run `clang-format` on configuration files.
- The default `config/` targets `stm32f1`. In the workspace, configuration
  lives in the workspace `config/<name>/`, not here.
- Layout:
  - `config/arch/arch_flags.h` holds module enables (`__ENABLE_<DRV>__`) between
    `MODULE LIST` markers.
  - `config/arch/<arch>/<drv>_flags.h` and `config/arch/noarch/` hold driver
    options.
  - `config/filesystems/`, `config/network/`, `config/os/` and
    `config/project/` hold the remaining settings.
- Linker memory regions are defined in `custom_linker_script.h` and
  `custom_linker_script.ld`.

## Coding style

- Formatting is defined by `.clang-format` (WebKit base, **8-space indent**,
  **120 columns**, braces always inserted, `PointerAlignment: Right`, includes
  not sorted, macros such as `foreach`, `scope` and `mutex_scope` treated as
  loops).
- Run `clang-format -i <file>` on C/C++ files you change. Do not reformat
  files you did not otherwise touch. Never format third-party code (FreeRTOS,
  lwIP, FatFs, lwext4, CMSIS, vendor headers).
- C18 for C, C++11 for C++ (no RTTI, no exceptions), built with `-Os`,
  `-Wall -Wextra` and many `-Werror=…` flags. Code must compile warning-free.
- Follow the existing file template:
  - a header block with `File`, `Author`, `Brief` and the license text,
  - then the `/*=====…` section banners (include files, local macros, local
    types, prototypes, local objects, exported objects, function definitions).
  - The easiest way to get this right is to copy an existing module
    (`src/drivers/zero`, `src/fs/ramfs`) or use the `tools/add*.sh` templates.
- Every function has a Doxygen comment framed with `//====…` lines
  (`@brief`, `@param[in]/[out]`, `@return`).
- License: kernel files are GPL with the dnx RTOS exception (see
  `doc/license.txt`). Keep the license of the file you edit. Ask before
  choosing a license for a new file if unclear.
- Code, comments and commit messages are in English. Commit messages are short
  sentences ending with a period, e.g. `Fix in the VFS.`.
- The commit-msg hook installed by `tools/apply_git_hooks.sh` (run by `make`)
  prefixes messages with the branch name (`develop: …`). Do not add the prefix
  yourself.

## Kernel conventions

- Kernel functions return an errno value (`ESUCC` on success, see
  `src/include/kernel/errno.h`), not -1/0.
- Drivers and file systems use only the kernel API: `drivers/driver.h`
  (drivers) or `fs/fs.h` (file systems). Both include the common API
  `kernel/sysapi.h`:
  - memory: `sys_malloc`, `sys_zalloc`, `sys_malloc2`, `sys_zalloc2`, `sys_free`,
  - synchronization: `sys_mutex_*`, `sys_semaphore_*`, `sys_queue_*`,
    `sys_flag_*`,
  - time: `sys_sleep_ms`, `sys_sleep_us`, `sys_get_uptime_ms`,
  - locking: `sys_critical_section_begin/end` (interrupts masked),
    `sys_scheduler_lock/unlock` (no context switch), `sys_spinlock_*`
    (`kspinlock_t` embedded in driver data, for short non-waiting code instead
    of mutex; `_irq` variant for data shared with interrupts),
  - caches: `sys_dcache_clean/invalidate/flush` (DMA buffers),
    `sys_icache_invalidate`, `sys_cache_sync_code` (new code in memory); no-op
    on CPUs without cache,
  - file access: `sys_fopen`/`sys_fread`/`sys_fwrite`/`sys_fclose`/`sys_ioctl`,
  - text: `sys_strfmt` (scnprintf semantics), `sys_sscanf`, `sys_opt_*`
    (option strings), `printk`.
- Fixed kernel entry points are the last vector table entries
  (`kernel/kvectors.h`): 255 syscall entry (used by libc,
  `__LIBC_DNX_RTOS_ADDRESS__` is the kernel vector table address), 254 kernel
  symbol table. Vector tables always have 256 entries.
- Kernel symbol table (`kernel/ksymtab.h`, `src/kernel/ksymtab.c`, enabled by
  `__OS_ENABLE_DRVSET__`): function addresses in flash for drivers built
  separately (drvset). Its pointer is stored in vector table entry 254.
  drvset links `src/drvset/ksymstub.S` (one stub per symbol, no RAM). When a
  function is added to the module API, append it at the end of
  `_KSYMTAB_LIST`; never remove or reorder entries; increment
  `_KSYMTAB_VERSION` when an exported function changes incompatibly.
- drvset partition (`drivers/drvset.h`, `src/drivers/drvset.c`): header
  `_drvset_header_t` at `__OS_DRVSET_FLASH_ADDRESS__` (binary interface:
  change only with `_DRVSET_HEADER_VERSION`), RAM `.drvset_ram` at the start
  of the main RAM region (kernel linker scripts), modules registered after
  built-in modules (`_MODULES_MAX`). Interrupts: unhandled vectors go to
  `__Default_Handler` of the vector table, which calls
  `_irq_shared_dispatch()` (`src/kernel/kirq.c`): shared handlers of kernel
  drivers, then `_drvset_irq_dispatch()` (IRQ table of the partition, indexed
  by IRQ number, and shared handlers of `DRV_IRQ_SHARED()`, no RAM); the
  partition may use only IRQs whose kernel vector is `__Default_Handler`. Driver
  interfaces of kernel drivers (`_GPIO_DDI_*`, `_DMA_DDI_*`, `_MDMA_DDI_*`,
  `_IRQ_DDI_*` for EXTI lines) are exported in the symbol table as optional
  symbols (`_KSYMTAB_OPTIONAL_LIST`, weak fallbacks in `drivers/drvset.c`
  return an error when the driver is not built); a new optional symbol needs
  entries in both lists. Build parts used by the workspace drvset build
  (`tools/drvset`): `src/drvset/` (`ksymstub.S`, `drvset_header.c`,
  `drvset.ld`, `mkirqtab.awk`); they are not compiled into the kernel.
  File systems of the partition: table of `struct _FS_entry` (public
  `fs/fsif.h`, interface layout depends on `__OS_ENABLE_*__`, `fs_magic`
  checked at start), `_mount()` looks up kernel file systems first, then
  `_drvset_get_filesystem()`.
  POSIX simulator: the image (option `--drvset`) and its RAM are mapped by
  `_host_drvset_map()` (RAM after the partition flash), the first RAM word
  holds the symbol table address (`__ksymtab_vector`), interrupts are not
  supported (header with interrupt tables is rejected).
- Shared interrupts of kernel drivers (`DRV_IRQ_SHARED()` of
  `drivers/driver.h`, many drivers on one IRQ line, e.g. `TIM6_DAC`,
  `SAI2_QUADSPI`): the driver Makefile adds the driver to `IRQ_SHARED`
  (`IRQ_SHARED += <drv>`), the kernel is then built with `__KERNEL_IRQ_SHARED__`
  and `src/kernel/kirq.c`. Entries (8 B flash each, no RAM) are collected in
  `.irq_shared` (`__irq_shared_start/end`, kernel linker scripts), dispatched
  from `__Default_Handler`; the IRQ must not have a handler in the vector
  table (`_irq_shared_init()` prints a warning at start). Kernel and drvset
  drivers can share one IRQ. Each handler checks flags of its own device; a
  driver must not disable the shared NVIC line on release. Without drvset
  and without `IRQ_SHARED` the dispatcher is not built (no cost).
- Prefix tells who can use the function: `sys_` drivers and file systems,
  `drv_` only drivers (`drv_lock`, `drv_get_instance`, `drv_mem_*`,
  `drv_clock_changed`, …), `fs_` only file systems (`fs_dev_*` access to the
  device under file system, `fs_pipe_*`, `fs_mkdir`, `fs_stat`, …).
  `sys_llist_*` is a library available in file systems and kernel.
- Driver and file system sources are compiled with `__DNX_MODULE__` (kernel
  `Makefile`, `module_cflags`). Kernel internal headers (`kernel/sysfunc.h`,
  `kernel/kwrapper.h`, `kernel/process.h`, `fs/vfs.h`, `drivers/drvctrl.h`,
  `mm/*.h`, …) stop the build with `#error` there. Kernel parts in these
  directories (`drvctrl.c`, `drvapi.c`, `vfs.c`, `fsapi.c`, `fsctrl.c`, `pipe.c`,
  `procfs`) are excluded. Kernel internal code includes `kernel/sysfunc.h`.
  When a module needs a kernel service, add it to the API (`sysapi.h`/`sysapi.c`,
  `driver.h`/`drvapi.c` or `fs.h`/`fsapi.c`) instead of using kernel internals.
- Never call user-space libc or raw FreeRTOS APIs from drivers or file
  systems. In interrupts, use the `*_from_ISR` variants.
- Mark unused arguments with `UNUSED_ARG1/2/…`. Kernel log messages go through
  `printk`.
- Drivers implement the `API_MOD_*` interface from `drivers/driver.h`:
  `API_MOD_INIT`, `RELEASE`, `OPEN`, `CLOSE`, `WRITE`, `READ`, `IOCTL`,
  `FLUSH`, `STAT`.
- File systems implement `API_FS_*` from `fs/fs.h`.
- ioctl requests are defined in `<driver>_ioctl.h` with `_IO/_IOR/_IOW/_IOWR`
  as `IOCTL_<DRV>__<NAME>`.
- Drivers and file systems are discovered from the directory structure.
  - A new driver needs:
    - a directory in `src/drivers/<name>/` with `<name>_ioctl.h`,
    - a `Makefile` that adds sources under `ifeq ($(__ENABLE_<NAME>__), _YES_)`
      (using `CSRC_NOARCH`/`HDRLOC_NOARCH` or `CSRC_ARCH`/`HDRLOC_ARCH` with
      `$(TARGET)` checks),
    - an `__ENABLE_<NAME>__` entry in `config/arch/arch_flags.h`,
    - matching options files if needed.
  - Drivers are character devices (`S_IFCHR`) by default. A driver of a
    medium that can hold a file system (random read and overwrite in place)
    is a block device: it defines `_<NAME>_BLOCK_DEVICE` in `<name>_ioctl.h`
    (read by `src/drivers/adddriver.sh`) and reports `st_blksize` in
    `API_MOD_STAT` (stacked drivers such as `part` and `loop` report 1, the
    underlying device handles alignment). File systems get the type with
    `fs_dev_type(dev)`.
  - Requests to block devices not aligned to `st_blksize` are split by
    `drvctrl.c` (read-modify-write), so block drivers get only aligned
    requests. Data buffer requirements (e.g. DMA) are declared in
    `API_MOD_STAT` (`st_rdbufalign`, `st_wrbufalign`, `st_bufdma`,
    `st_bufregions`); buffers that do not meet them are replaced by bounce
    buffers. Block drivers must not check alignment or copy data to own
    buffers.
  - Memory alignment is selected by the allocating code: `sys_malloc()` /
    `sys_zalloc()` use default heap alignment (`_HEAP_ALIGN_`),
    `sys_malloc2()` / `sys_zalloc2()` take `align` after size (drivers:
    `size, align, prefreg, required, mask, mem`; file systems: `size, align,
    prefreg, mem`; 0 is default alignment). The heap places aligned blocks
    natively and rounds their size up to the alignment, so a block aligned to
    `_CPUCTL_CACHE_ALIGN` does not share D-cache lines with other blocks (DMA
    buffers). Memory is always released by `sys_free()`. Buffers inside
    structures use `alignas(_CPUCTL_CACHE_ALIGN)` (FatFs: `FF_BUF_ALIGN`) and
    the structure is allocated with the same alignment. Do not over-allocate
    and move pointers manually. `drvctrl.c` also handles
    `IOCTL_STORAGE__STATS_*` / `IOCTL_STORAGE__GET_STATS` (I/O statistics,
    `iostat` program). The HOSTDISK driver (simulator) can emulate these
    constraints (`block_size`, `buffer_align` configuration string).
  - A new file system needs `src/fs/<name>/` with a `Makefile` and flags in
    `config/filesystems/`.
  - Use `tools/adddriver.sh` / `tools/addfilesystem.sh` or copy `drivers/zero`
    / `fs/ramfs`.
- Programs are not linked with the kernel. The kernel finds them through the
  appset table at `__PROJ_APPSET_TABLE_ADDRESS__`.

## Compatibility with the workspace (important)

The kernel and the application set are separate images built from separate
repositories, so the interface between them must stay consistent:

- **Syscall numbers**: the enum in `src/kernel/syscall.c` must match
  `common_libs/libc/source/syscall.h` (`_LIBC_SYS_*`) in the workspace. Add new
  syscalls at the end of both lists and update both sides together. Never
  reorder or remove entries without being asked.
- **ioctl headers** (`<driver>_ioctl.h`, `drivers/ioctl_macros.h`,
  `drivers/class/`, `gpio_flags.h`) are exported to `build/ioctl/` and used by
  user-space programs. Changing request numbers or structures breaks existing
  programs.
- **Appset table format** (`_program_table_desc_t`, magic value in
  `process.c`) is shared with the appset build. In the POSIX simulator the
  main table is `_program_table_desc` and the workspace may link more appsets
  (`SIM_APPSETS`): their generated program registration puts a reference of
  each table in the `dnx_appset_tables` section, `_process_init()` registers
  them after the main table (weak `__start_/__stop_dnx_appset_tables`, the
  section may not exist).
- **TTY raw mode** (`IOCTL_TTY__RAW_MODE_ON/OFF`) is used by the workspace
  `libtui` library (nano, mc, dialog). In raw mode input bytes go directly to
  the reader and output bypasses the screen buffer; `dsh` switches the
  terminal back to line mode before each prompt.
- **Thread and process lifetime**: a finished thread that is not detached
  stays a zombie (thread ID reserved) until `thread_join` releases it, kernel
  (kworker) threads are always detached. `process_kill` does not remove the
  zombie of a killed process, the parent must wait for it (exit status
  `_PROCESS_STATUS_KILLED`, 137, the same as `PROCESS_STATUS_KILLED` in libc);
  kill of a finished process that was not waited for yet succeeds and keeps
  its status. Tasks of finished joinable threads and processes are destroyed
  by `thread_join`/`process_wait` (memory is released immediately, a task that
  deletes itself is released only by the idle task). Stress tests of this
  behavior are in the workspace `perf` program (`perf -s`).
- **Command line parsing** (`argument_parse()`/`argtab_create()` in
  `process.c`) follows shell quoting rules and is relied on by `dsh`,
  `system()`, Lua `io.popen()` and configtool's dialog calls. `argc` is
  limited to 65535 arguments.
- When a change requires a matching change in the workspace (libc, programs,
  configuration), point it out to the user. This repository cannot be
  verified alone.

## Working rules for agents

- Keep changes minimal and focused. Do not refactor or reformat unrelated
  code.
- Never modify `build/`, generated files or third-party sources unless the
  task explicitly requires it. Third-party forks such as FatFs (`src/fs/fatfs/ffs`)
  and lwext4 (`src/fs/ext4fs/lwext4`) carry local patches, so read them before
  editing.
- Do not change memory layout defaults (`custom_linker_script.*`, appset or
  romfs addresses) without being asked.
- After changes, build the kernel if the toolchain is available and report the
  result. Prefer building from the workspace with the affected configuration.
  For changes in architecture-specific code, check the matching BSP in
  `tools/test_bsp/`. If you cannot build, state it explicitly.
- Do not run `make config`, flashing or other interactive or hardware targets
  unless the user asks.
