/*==============================================================================
File    qspi_ioctl.h

Author  Daniel Zorychta

Brief   Quad SPI

        Copyright (C) 2026 Daniel Zorychta <daniel.zorychta@gmail.com>

        This program is free software; you can redistribute it and/or modify
        it under the terms of the GNU General Public License as published by
        the Free Software Foundation and modified by the dnx RTOS exception.

        NOTE: The modification  to the GPL is  included to allow you to
              distribute a combined work that includes dnx RTOS without
              being obliged to provide the source  code for proprietary
              components outside of the dnx RTOS.

        The dnx RTOS  is  distributed  in the hope  that  it will be useful,
        but WITHOUT  ANY  WARRANTY;  without  even  the implied  warranty of
        MERCHANTABILITY  or  FITNESS  FOR  A  PARTICULAR  PURPOSE.  See  the
        GNU General Public License for more details.

        Full license text is available on the following file: doc/license.txt.

==============================================================================*/

/**
@defgroup drv-qspi QSPI Driver

\section drv-qspi-desc Description
Driver handles Quad SPI peripheral.

\section drv-qspi-sup-arch Supported architectures
\li stm32f4

\section drv-qspi-ddesc Details
\subsection drv-qspi-ddesc-num Meaning of major and minor numbers
Major number selects QUADSPI peripheral. Minor number selects block
included in the peripheral. When dual-flash mode is enabled then is not possible
to use blocks individually.

\subsubsection drv-qspi-ddesc-numres Numeration restrictions
In stm32f4 only 0 can be set as major number. Minor can be set from 0 to 1.

\subsection drv-qspi-ddesc-init Driver initialization
To initialize driver the following code can be used:

@code
driver_init("QSPI", 0, 0, "/dev/qspi-blk0");
@endcode
@code
driver_init("QSPI", 0, 1, "/dev/qspi-blk1");
@endcode

\subsection drv-qspi-ddesc-release Driver release
To release driver the following code can be used:
@code
driver_release("QSPI", 0, 0);
@endcode
@code
driver_release("QSPI", 0, 1);
@endcode

\subsection drv-qspi-ddesc-cfg Driver configuration

@code
// Example commands for MX25L12872F NOR flash memory working in DualSPI mode.
static const QSPI_command_t qspi_dread = {
        .instruction_mode      = QSPI_MODE__SINGLE,
        .address_mode          = QSPI_MODE__DOUBLE,
        .alternate_byte_mode   = QSPI_MODE__NONE,
        .data_mode             = QSPI_MODE__DOUBLE,
        .address_size          = QSPI_SIZE__24_BITS,
        .alternate_bytes_size  = QSPI_SIZE__8_BITS,
        .instruction           = 0xBB,
        .address               = 0,
        .alternate_bytes       = 0,
        .dummy_cycles          = 4,
        .data_size             = 0,
        .data                  = NULL,
        .instruction_only_once = false,
        .ddr                   = false,
        .ddr_hold_cycle        = QSPI_DDR_HOLD_CYCLE__ANALOG_DELAY,
        .direction             = QSPI_DIRECTION__READ,
};

static const QSPI_command_t qspi_pp = {
        .instruction_mode      = QSPI_MODE__SINGLE,
        .address_mode          = QSPI_MODE__SINGLE,
        .alternate_byte_mode   = QSPI_MODE__NONE,
        .data_mode             = QSPI_MODE__SINGLE,
        .address_size          = QSPI_SIZE__24_BITS,
        .alternate_bytes_size  = QSPI_SIZE__8_BITS,
        .instruction           = 0x02,
        .address               = 0,
        .alternate_bytes       = 0,
        .dummy_cycles          = 0,
        .data_size             = 0,
        .data                  = NULL,
        .instruction_only_once = false,
        .ddr                   = false,
        .ddr_hold_cycle        = QSPI_DDR_HOLD_CYCLE__ANALOG_DELAY,
        .direction             = QSPI_DIRECTION__WRITE,
};

static const QSPI_command_t qspi_wren = {
        .instruction_mode      = QSPI_MODE__SINGLE,
        .address_mode          = QSPI_MODE__NONE,
        .alternate_byte_mode   = QSPI_MODE__NONE,
        .data_mode             = QSPI_MODE__NONE,
        .address_size          = QSPI_SIZE__8_BITS,
        .alternate_bytes_size  = QSPI_SIZE__8_BITS,
        .instruction           = 0x06,
        .address               = 0,
        .alternate_bytes       = 0,
        .dummy_cycles          = 0,
        .data_size             = 0,
        .data                  = NULL,
        .instruction_only_once = false,
        .ddr                   = false,
        .ddr_hold_cycle        = QSPI_DDR_HOLD_CYCLE__ANALOG_DELAY,
        .direction             = QSPI_DIRECTION__WRITE,
};

static const QSPI_command_t qspi_rdsr = {
        .instruction_mode      = QSPI_MODE__SINGLE,
        .address_mode          = QSPI_MODE__NONE,
        .alternate_byte_mode   = QSPI_MODE__NONE,
        .data_mode             = QSPI_MODE__SINGLE,
        .address_size          = QSPI_SIZE__8_BITS,
        .alternate_bytes_size  = QSPI_SIZE__8_BITS,
        .instruction           = 0x05,
        .address               = 0,
        .alternate_bytes       = 0,
        .dummy_cycles          = 0,
        .data_size             = 0,
        .data                  = NULL,
        .instruction_only_once = false,
        .ddr                   = false,
        .ddr_hold_cycle        = QSPI_DDR_HOLD_CYCLE__ANALOG_DELAY,
        .direction             = QSPI_DIRECTION__READ,
};

static const QSPI_config_t qspi_configuration = {
        .clock_prescaller                = 2,
        .sample_shifting                 = QSPI_SAMPLE_SHIFTING__NONE,
        .flash_size_bits                 = 24,
        .chip_select_high_time           = 5,
        .clock_mode                      = QSPI_CLOCK_MODE__0,
        .dual_flash                      = false,
        .page_size                       = 256,
        .write_in_progress_mask          = 0x01,
        .write_in_progress_match         = 0x00,
        .write_in_progress_register_size = QSPI_SIZE__8_BITS,
        .read_command                    = &qspi_dread,
        .page_program_command            = &qspi_pp,
        .write_enable_command            = &qspi_wren,
        .read_status_register_command    = &qspi_rdsr,
}

// configuration in initialization phase
driver_init2("QSPI", 0, 0, "/dev/qspi", &qspi_configuration);

// or configuration later
int qspi = open("/dev/qspi", O_RDWR);
if (qspi >= 0) {
        ioctl(qspi, IOCTL_QSPI__CONFIGURE, &qspi_configuration);
        close(qspi);
}
@endcode

\subsection drv-qspi-ddesc-write Data write
Data can be written to the memory as regular file with one restriction: memory
array should be erased first. To do this user should use erase command before
write by using IOCTL_QSPI__COMMAND ioctl's request. To wait for operation finish
one can use IOCTL_QSPI__WAIT_FOR_MEMORY_TO_BE_READY request.

Example:
@code
// Example command for MX25L12872F NOR flash memory.
QSPI_command_t sector_erase_4KiB = {
        .instruction_mode      = QSPI_MODE__SINGLE,
        .address_mode          = QSPI_MODE__SINGLE,
        .alternate_byte_mode   = QSPI_MODE__NONE,
        .data_mode             = QSPI_MODE__NONE,
        .address_size          = QSPI_SIZE__24_BITS,
        .alternate_bytes_size  = QSPI_SIZE__8_BITS,
        .instruction           = 0x20,
        .address               = erase_address,
        .alternate_bytes       = 0,
        .dummy_cycles          = 0,
        .data_size             = 0,
        .data                  = NULL,
        .instruction_only_once = false,
        .ddr                   = false,
        .ddr_hold_cycle        = QSPI_DDR_HOLD_CYCLE__HALF_CLK_DELAY,
        .direction             = QSPI_DIRECTION__WRITE,
};

int err = ioctl(fd, IOCTL_QSPI__COMMAND, &sector_erase_4KiB);
if (not err) {
        err = ioctl(fd, IOCTL_QSPI__WAIT_FOR_MEMORY_TO_BE_READY, &(u32_t){2000});
}

printf("Erase result: %d\n", err);
@endcode

\subsection drv-qspi-ddesc-read Data read
Data can be read from momory as regular file without any restrictions.
To read memory more effecient the Memory Mapped Mode can be used. In this mode
CPU is automatically reading data from memory without software intervetion.
To enable this mode the following command should be used:
@code
int err = ioctl(fd, IOCTL_QSPI__ENTER_MEMORY_MAPPED_MODE, &qspi_dread);
if (not err) {
        puts("Peripheral switched to Memory Mapped Mode");
} else {
        uts("Switch to Memory Mapped Mode error");
}
@endcode

Note: If Memory Mapped Mode is started then is not possible to write to the memory.
Reading data as regular file is also not possible because whole memory is mapped
to specified address:
\li stm32f4: 0x90000000
In that mode the XIP (eXecute In Place) is possible with most of CPUs.

To exit Memory Mapped Mode the following code can be used:
@code
int err = ioctl(fd, IOCTL_QSPI__EXIT_MEMORY_MAPPED_MODE);
if (not err) {
        puts("Peripheral switched to normal mode");
} else {
        uts("Switch to normal mode error");
}
@endcode
@{
*/

#ifndef _QSPI_IOCTL_H_
#define _QSPI_IOCTL_H_

/*==============================================================================
  Include files
==============================================================================*/
#include "drivers/ioctl_macros.h"

#ifdef __cplusplus
extern "C" {
#endif

/*==============================================================================
  Exported macros
==============================================================================*/
/**
 *  @brief  Set QSPI configuration.
 *  @param  [WR] const QSPI_config_t * QSPI peripheral configuration
 *  @return On success 0 is returned, otherwise -1.
 */
#define IOCTL_QSPI__CONFIGURE _IOW(QSPI, 0x00, const QSPI_config_t *)

/**
 *  @brief  Command transfer operation (write/read or only command).
 *  @param  [WR,RD] QSPI_command_t * QSPI command specification
 *  @return On success 0 is returned, otherwise -1.
 */
#define IOCTL_QSPI__COMMAND _IOWR(QSPI, 0x01, QSPI_command_t *)

/**
 *  @brief  Enter to memory mapped mode by using specific command.
 *  @param  [WR] QSPI_command_t * command to use in memory mapped mode
 *  @return On success 0 is returned, otherwise -1.
 */
#define IOCTL_QSPI__ENTER_MEMORY_MAPPED_MODE _IOW(QSPI, 0x02, QSPI_command_t *)

/**
 *  @brief  Exit from memory mapped mode.
 *  @return On success 0 is returned, otherwise -1.
 */
#define IOCTL_QSPI__EXIT_MEMORY_MAPPED_MODE _IO(QSPI, 0x03)

/**
 *  @brief  Wait for memory to be ready.
 *  @param [WR] uint32_t * timeout in milliseconds
 *  @return On success 0 is returned, otherwise -1.
 */
#define IOCTL_QSPI__WAIT_FOR_MEMORY_TO_BE_READY _IOW(QSPI, 0x04, uint32_t *)

/*==============================================================================
  Exported object types
==============================================================================*/
/**
 * Type represent transmission mode.
 */
typedef enum {
        QSPI_MODE__NONE,
        QSPI_MODE__SINGLE,
        QSPI_MODE__DOUBLE,
        QSPI_MODE__QUAD,
} QSPI_mode_t;

/**
 * Type represent transfer size.
 */
typedef enum {
        QSPI_SIZE__8_BITS,
        QSPI_SIZE__16_BITS,
        QSPI_SIZE__24_BITS,
        QSPI_SIZE__32_BITS,
} QSPI_size_t;

/**
 * Type represent DDR hold cycle delay.
 */
typedef enum {
        QSPI_DDR_HOLD_CYCLE__ANALOG_DELAY,
        QSPI_DDR_HOLD_CYCLE__HALF_CLK_DELAY,
} QSPI_DDR_hold_cycle_t;

/**
 * Type represent Clock/Data mode.
 */
typedef enum {
        QSPI_CLOCK_MODE__0,
        QSPI_CLOCK_MODE__3,
} QSPI_clock_mode_t;

/**
 * Type represent sample shifting mode.
 */
typedef enum {
        QSPI_SAMPLE_SHIFTING__NONE,
        QSPI_SAMPLE_SHIFTING__HALF_CYCLE,
} QSPI_sample_shifting_t;

/**
 * Type represent transmission direction.
 */
typedef enum {
        QSPI_DIRECTION__READ,
        QSPI_DIRECTION__WRITE,
} QSPI_direction_t;

/**
 * Type represent QSPI command.
 */
typedef struct {
        QSPI_mode_t instruction_mode;
        uint8_t instruction;
        QSPI_mode_t address_mode;
        QSPI_size_t address_size;
        uint32_t address;
        QSPI_mode_t alternate_byte_mode;
        uint32_t alternate_bytes;
        QSPI_size_t alternate_bytes_size;
        QSPI_mode_t data_mode;
        uint8_t dummy_cycles;
        uint32_t data_size;
        bool ddr;
        QSPI_DDR_hold_cycle_t ddr_hold_cycle;
        bool instruction_only_once;
        QSPI_direction_t direction;
        void *data;
} QSPI_command_t;

/**
 * Type represent QSPI configuation.
 */
typedef struct {
        uint32_t clock_prescaller;
        QSPI_sample_shifting_t sample_shifting;
        uint8_t flash_size_bits;
        uint8_t chip_select_high_time;
        QSPI_clock_mode_t clock_mode;
        bool dual_flash;
        size_t page_size;
        uint32_t write_in_progress_mask;
        uint32_t write_in_progress_match;
        QSPI_size_t write_in_progress_register_size;
        const QSPI_command_t *read_command;
        const QSPI_command_t *page_program_command;
        const QSPI_command_t *write_enable_command;
        const QSPI_command_t *read_status_register_command;
} QSPI_config_t;

/*==============================================================================
  Exported objects
==============================================================================*/

/*==============================================================================
  Exported functions
==============================================================================*/

/*==============================================================================
  Exported inline functions
==============================================================================*/

#ifdef __cplusplus
}
#endif

#endif /* _QSPI_IOCTL_H_ */
/**@}*/
/*==============================================================================
  End of file
==============================================================================*/
