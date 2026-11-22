/*==============================================================================
File    part_ioctl.h

Author  Daniel Zorychta

Brief   Partition driver

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
@defgroup drv-part PART Driver

\section drv-part-desc Description
Driver handles virtual device volume partitioning. Partitions are described by
the partition table of the device (GPT or MBR) or configured manually.

\section drv-part-sup-arch Supported architectures
\li noarch

\section drv-part-ddesc Details
\subsection drv-part-ddesc-num Meaning of major and minor numbers
Major number selects disk (instances of the same major number and device are
updated together by @ref IOCTL_PART__READ_PARTITION_TABLE). Minor 0 is the disk
instance, partition nodes created automatically use minors 1..15 (partition
number) and 17..31 (named GPT partitions: 16 + partition number).

\subsection drv-part-ddesc-mode Instance modes
Mode is selected by configuration (first matching):
\li `label` set: GPT partition selected by name,
\li `part` set: partition number (GPT entry or MBR primary partition 1..4),
\li `total_blocks` set: manual geometry (`offset_blocks`, `total_blocks`),
\li otherwise: disk instance (whole device). Disk instance with minor 0 and
    `nodes` prefix creates partition nodes automatically: `<nodes><n>`
    (`<nodes>p<n>` when prefix ends with digit) and `<nodes>-<name>` for named
    GPT partitions (names without spaces and slashes). Nodes are created on
    initialization and updated by @ref IOCTL_PART__READ_PARTITION_TABLE (nodes
    of removed partitions are released, except opened ones). Release of disk
    instance releases the partition nodes.

Partitions selected by number or name not found in the partition table have
size 0. Partition table uses 512-byte sectors, GPT primary header is checked by
CRC, damaged primary GPT is replaced by the backup GPT (last sector of device),
GPT is also used when the protective MBR is damaged (no signature or no
partitions) and the GPT header is valid.

\subsection drv-part-ddesc-init Driver initialization
To initialize driver the following code can be used:

@code
// /dev/sd0 (whole card), /dev/sd0p1, /dev/sd0p2, ..., /dev/sd0-data, ...
driver_init2("PART", 0, 0, "/dev/sd0", &(const PART_config_t){.path = "/dev/sdmmc0", .nodes = "/dev/sd0"});
@endcode
@code
// partition with name "data" of GPT disk
driver_init2("PART", 1, 0, "/dev/data", &(const PART_config_t){.path = "/dev/sdmmc0", .label = "data"});
@endcode

\subsection drv-part-ddesc-release Driver release
To release driver the following code can be used:
@code
driver_release("PART", 0, 0);
@endcode

\subsection drv-part-ddesc-cfg Driver configuration
Driver can be configured by using IOCTL_PART__CONFIGURE ioctl request.

\subsection drv-part-ddesc-cfgstr Configuration string
Configuration string (@ref IOCTL_PART__CONFIGURE_STR, program `modprobe -c`) is
a list of `key=value` pairs separated by commas, values are described in @ref
IOCTL_DEVICE__CONFIGURE_STR. All keys are set by each configuration (not given:
value of column Omitted).

| Key             | Value  | Omitted  | Description |
|-----------------|--------|----------|-------------|
| `path`          | path   | required | block device file (e.g. /dev/sda) |
| `block_size`    | number | 512      | block size [bytes] (manual geometry) |
| `offset_blocks` | number | 0        | offset of partition [blocks] (manual geometry) |
| `total_blocks`  | number | 0        | size of partition [blocks] (manual geometry) |
| `part`          | number | 0        | partition number 1..15 from partition table |
| `label`         | text   | none     | GPT partition name |
| `nodes`         | path   | none     | disk instance: prefix of automatically created partition nodes |

@code
modprobe -i PART 0 1 /dev/sda1 -c path=/dev/sda,offset_blocks=2048,total_blocks=65536
modprobe -i PART 5 0 /dev/sdf -c path=/dev/hostdisk5,nodes=/dev/sdf
@endcode

\subsection drv-part-ddesc-write Data write
The same as regular file.

\subsection drv-part-ddesc-read Data read
The same as regular file.

@{
*/

#ifndef _PART_IOCTL_H_
#define _PART_IOCTL_H_

/*==============================================================================
  Include files
==============================================================================*/
#include "drivers/ioctl_macros.h"
#include "drivers/class/device/ioctl.h"
#include "drivers/class/storage/ioctl.h"

#ifdef __cplusplus
extern "C" {
#endif

/*==============================================================================
  Exported macros
==============================================================================*/
/** Driver provides block devices (S_IFBLK). Marker used by adddriver.sh. */
#define _PART_BLOCK_DEVICE

/**
 *  @brief  Configure driver.
 *  @param  [WR] const PART_config_t*
 *  @return On success 0 is returned, otherwise -1.
 *
 *  @b Example
 *  @code
    #include <sys/ioctl.h>

    //...

    driver_init("PART", 0, 0, "/dev/ee0p1");
    f = fopen("/dev/ee0p1", "r+");
    if (f) {
            static const PART_config_t cfg = {
                    .path          = "/dev/ee0",
                    .block_size    = 256,
                    .offset_blocks = 0,
                    .total_blocks  = 256
            };
            ioctl(fileno(f), IOCTL_PART__CONFIGURE, &cfg);
            fclose(f);
    }

    driver_init("PART", 1, 0, "/dev/ee0p2");
    f = fopen("/dev/ee0p2", "r+");
    if (f) {
            static const PART_config_t cfg = {
                    .path          = "/dev/ee0",
                    .block_size    = 256,
                    .offset_blocks = 256,
                    .total_blocks  = 256
            };
            ioctl(fileno(f), IOCTL_PART__CONFIGURE, &cfg);
            fclose(f);
    }

    //...
    @endcode
 */
#define IOCTL_PART__CONFIGURE _IOW(PART, 0x00, const PART_config_t *)

/**
 *  @brief  Configure device by configuration string, keys and values:
 *          @ref drv-part-ddesc-cfgstr.
 *  @param  [WR] const char * configuration string
 *  @return On success 0 is returned, otherwise -1 and @ref errno is set.
 */
#define IOCTL_PART__CONFIGURE_STR IOCTL_DEVICE__CONFIGURE_STR

/**
 *  @brief  Get driver configuration.
 *  @param  [RD] PART_config_t*
 *  @return On success 0 is returned, otherwise -1.
 *
 *  @b Example
 *  @code
    #include <sys/ioctl.h>

    //...

    f = fopen("/dev/ee0p1", "r+");
    if (f) {
            PART_config_t cfg;
            ioctl(fileno(f), IOCTL_PART__GET_CONFIGRATION, &cfg);
            fclose(f);
    }

    //...
    @endcode
 */
#define IOCTL_PART__GET_CONFIGRATION _IOR(PART, 0x01, PART_config_t *)

/**
 *  @brief  Read partition table (GPT or MBR), update geometry of all instances of
 *          the major number connected to the same device and nodes of disk instance.
 *  @param  none
 *  @return On success 0 is returned, otherwise -1 and @ref errno is set
 *          (EMEDIUMTYPE: no partition table, partitions are removed).
 *
 *  @b Example
 *  @code
    #include <sys/ioctl.h>

    //...

    int fd = open("/dev/sd0", O_RDWR); // any instance of the major number
    if (fd >= 0) {
            ioctl(fd, IOCTL_PART__READ_PARTITION_TABLE);
            close(fd);
    }

    //...
    @endcode
 */
#define IOCTL_PART__READ_PARTITION_TABLE IOCTL_STORAGE__READ_PARTITION_TABLE

/*==============================================================================
  Exported object types
==============================================================================*/
typedef struct {
        const char *path;    /*!< Device path. */
        u32_t block_size;    /*!< Block size in bytes (0: 512). */
        u32_t offset_blocks; /*!< Offset in number of blocks. */
        u32_t total_blocks;  /*!< Total size in number of blocks (0: from partition table or whole device). */
        u8_t part;           /*!< Partition number 1..15 (0: not used). */
        const char *label;   /*!< GPT partition name (NULL: not used). */
        const char *nodes;   /*!< Disk instance: prefix of partition nodes (NULL: no nodes). */
} PART_config_t;

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

#endif /* _PART_IOCTL_H_ */
/**@}*/
/*==============================================================================
  End of file
==============================================================================*/
