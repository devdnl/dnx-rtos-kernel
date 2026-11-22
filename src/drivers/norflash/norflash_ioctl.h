/*==============================================================================
File    norflash_ioctl.h

Author  Daniel Zorychta

Brief   Serial NOR flash memory (SPI, QSPI)

        Copyright (C) 2026 Daniel Zorychta <daniel.zorychta@gmail.com>

        This program is free software; you can redistribute it and/or modify
        it under the terms of the GNU General Public License version 2 as
        published by the Free Software Foundation.

        The dnx RTOS is distributed in the hope that it will be useful,
        but WITHOUT ANY WARRANTY; without even the implied warranty of
        MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
        GNU General Public License for more details.

        Programs using the kernel by system calls are not covered by the license,
        drivers and file systems of drvset partition: exception, see the license
        file: doc/license.txt.

==============================================================================*/

/**
@defgroup drv-norflash NORFLASH Driver

\section drv-norflash-desc Description
Driver handles serial NOR flash memories (W25Qxx, MX25Lxx, GD25Qxx, IS25LPxx,
AT25SFxx, SST26VFxx, S25FLxx, N25Qxx, ...) connected to SPI device file (SPI
driver) or QSPI device file (QSPI driver, single line mode). Memory is a block
device: block size is a size of erase sector (4 KiB), the kernel splits
unaligned requests (read-modify-write). The driver erases sector only when
written data cannot be programmed without erase (bit change from 0 to 1) and
programs only pages that differ from memory content.

\section drv-norflash-sup-arch Supported architectures
\li Any (noarch)

\section drv-norflash-ddesc Details
\subsection drv-norflash-ddesc-num Meaning of major and minor numbers
Major number is a memory number, minor number must be 0.

\subsection drv-norflash-ddesc-detect Memory detection
Memory is identified by JEDEC ID (0x9F). Size, erase sector and address size
are read from SFDP table (JESD216, 0x5A). Without SFDP the size is calculated
from the capacity byte of JEDEC ID (2^n bytes) or can be set by configuration.
Memories bigger than 16 MiB are accessed by 4-byte address instructions
(0x13, 0x12, 0x21). Memory is woken up from deep power-down mode (0xAB) before
identification.

\subsection drv-norflash-ddesc-init Driver initialization
@code
driver_init("NORFLASH", 0, 0, "/dev/flash0");
@endcode

\subsection drv-norflash-ddesc-release Driver release
@code
driver_release("NORFLASH", 0, 0);
@endcode

\subsection drv-norflash-ddesc-cfg Driver configuration
Driver is configured by @ref IOCTL_NORFLASH__CONFIGURE (structure
NORFLASH_config_t) or by configuration string (@ref drv-norflash-ddesc-cfgstr):
@code
#include <dnx/misc.h>
#include <sys/ioctl.h>

static const NORFLASH_config_t cfg = {.path = "/dev/spi-flash"};

int fd = open("/dev/flash0", O_RDWR);
if (fd >= 0) {
        ioctl(fd, IOCTL_NORFLASH__CONFIGURE, &cfg);

        NORFLASH_info_t info;
        if (ioctl(fd, IOCTL_NORFLASH__GET_INFO, &info) == 0) {
                printf("JEDEC ID %02X%02X%02X, %u bytes\n", info.jedec_id[0],
                       info.jedec_id[1], info.jedec_id[2], info.size);
        }
        close(fd);
}
@endcode

\subsection drv-norflash-ddesc-cfgstr Configuration string
Configuration string (@ref IOCTL_NORFLASH__CONFIGURE_STR, program `modprobe -c`)
is a list of `key=value` pairs separated by commas, values are described in @ref
IOCTL_DEVICE__CONFIGURE_STR. All keys are set by each configuration (not given:
value of column Omitted). Configuration identifies the memory (ENODEV: memory
not detected).

| Key         | Value         | Omitted  | Description |
|-------------|---------------|----------|-------------|
| `path`      | path          | required | SPI or QSPI device file |
| `interface` | `spi`, `qspi` | spi      | interface (QSPI: single line mode, QSPI driver) |
| `size`      | number        | 0        | memory size [bytes], 0: detected (SFDP, JEDEC ID) |
| `unprotect` | bool          | 0        | clear block protection (global unlock 0x98, status register BP bits) |

@code
modprobe -i NORFLASH 0 0 /dev/flash0 -c path=/dev/spi-flash
@endcode

\subsection drv-norflash-ddesc-write Data write
Memory is a regular block device: file systems can be created on the device
(e.g. mkflatfs). Write of erased area (0xFF) or data that only clears bits is
done without erase.

\subsection drv-norflash-ddesc-read Data read
Memory is read as a regular file.

@{
*/

#ifndef _NORFLASH_IOCTL_H_
#define _NORFLASH_IOCTL_H_

/*==============================================================================
  Include files
==============================================================================*/
#include "drivers/ioctl_macros.h"
#include "drivers/class/device/ioctl.h"

#ifdef __cplusplus
extern "C" {
#endif

/*==============================================================================
  Exported macros
==============================================================================*/
/** Driver provides block devices (S_IFBLK). Marker used by adddriver.sh. */
#define _NORFLASH_BLOCK_DEVICE

/**
 *  @brief  Configure memory (identification).
 *  @param  [WR] @ref NORFLASH_config_t * configuration
 *  @return On success 0 is returned, otherwise -1 and errno is set
 *          (ENODEV: memory not responding or not identified).
 */
#define IOCTL_NORFLASH__CONFIGURE _IOW(NORFLASH, 0x00, const NORFLASH_config_t *)

/**
 *  @brief  Read memory information.
 *  @param  [RD] @ref NORFLASH_info_t * information
 *  @return On success 0 is returned, otherwise -1 and errno is set.
 */
#define IOCTL_NORFLASH__GET_INFO _IOR(NORFLASH, 0x01, NORFLASH_info_t *)

/**
 *  @brief  Configure device by configuration string, keys and values:
 *          @ref drv-norflash-ddesc-cfgstr.
 *  @param  [WR] const char * configuration string
 *  @return On success 0 is returned, otherwise -1 and @ref errno is set.
 */
#define IOCTL_NORFLASH__CONFIGURE_STR IOCTL_DEVICE__CONFIGURE_STR

/*==============================================================================
  Exported object types
==============================================================================*/
/**
 * Memory interface.
 */
typedef enum {
        NORFLASH_INTERFACE__SPI,  //!< SPI device file.
        NORFLASH_INTERFACE__QSPI, //!< QSPI device file (single line mode).
} NORFLASH_interface_t;

/**
 * Memory configuration.
 */
typedef struct {
        const char *path;               /*!< SPI or QSPI device file.*/
        NORFLASH_interface_t interface; /*!< Interface.*/
        u32_t size;                     /*!< Memory size [bytes], 0: detected.*/
        bool unprotect;                 /*!< Clear block protection.*/
} NORFLASH_config_t;

/**
 * Memory information.
 */
typedef struct {
        u8_t jedec_id[3];  /*!< Manufacturer, memory type, capacity.*/
        bool sfdp;         /*!< Parameters read from SFDP table.*/
        u8_t addr_bytes;   /*!< Number of address bytes (3, 4).*/
        u32_t size;        /*!< Memory size [bytes].*/
        u32_t sector_size; /*!< Erase sector size (block size) [bytes].*/
        u32_t erase_count; /*!< Number of sector erases from configuration.*/
} NORFLASH_info_t;

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

#endif /* _NORFLASH_IOCTL_H_ */
/**@}*/
/*==============================================================================
  End of file
==============================================================================*/
