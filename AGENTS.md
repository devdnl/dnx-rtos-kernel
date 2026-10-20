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
  `printk.c`, `sysfunc.c`, `time.c`, `khooks.c`, `kwrapper.c`.
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
  `kernel/sysfunc.h`, `kernel/errno.h`, …).
- `config/` — default kernel configuration (used only for standalone builds).
- `tools/` — `configtool-cli` (Lua TUI configurator), `configtool.sh`,
  `adddriver.sh` / `addfilesystem.sh` (templates for new modules), `mkfpkg.py`,
  `syncdir.py`, `rungens.sh`, `apply_git_hooks.sh`, `doxygen.sh`, `test_bsp/`.
- `doc/` — `coding_style.txt`, `license.txt`, Doxygen configuration.
- `build/` — output (git-ignored). Never edit or commit it.

## Build

Requirements: `arm-none-eabi-gcc` (10.3-2021.10 in the reference Docker image),
GNU make, Python 3, Lua 5.5 and `dialog` (configtool). The supported
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

Known legacy items: the `flash`/`install`/`reset` targets in `make help` refer
to scripts that do not exist in this repository. `make quickcheck`,
`tools/tests.sh` and `tools/releasepkg.sh` reference outdated paths. Verify
them before relying on them.

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
- Drivers and file systems use only kernel services from
  `src/include/kernel/sysfunc.h`:
  - memory: `sys_malloc`, `sys_zalloc`, `sys_free`,
  - synchronization: `sys_mutex_*`, `sys_semaphore_*`, `sys_queue_*`,
    `sys_flag_*`, `sys_spinlock_*`,
  - time: `sys_sleep_ms`, `sys_sleep_us`,
  - locking: `sys_critical_section_*`,
  - file and driver access: `sys_fopen`/`sys_fread`/`sys_fwrite`/`sys_fclose` and
    `sys_driver_*`.
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
  `process.c`) is shared with the appset build.
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
