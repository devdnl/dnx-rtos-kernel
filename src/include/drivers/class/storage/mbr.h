/*==============================================================================
File    mbr.h

Author  Daniel Zorychta

Brief   MBR definitions.

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
@defgroup MBR_H_ MBR_H_

Detailed Doxygen description.
*/
/**@{*/

#ifndef _MBR_H_
#define _MBR_H_

/*==============================================================================
  Include files
==============================================================================*/
#include "../../ioctl_macros.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*==============================================================================
  Exported macros
==============================================================================*/
#define MBR_BOOTSTRAP_CODE_OFFSET    0x000
#define MBR_PARTITION_1_ENTRY_OFFSET 0x1BE
#define MBR_PARTITION_2_ENTRY_OFFSET 0x1CE
#define MBR_PARTITION_3_ENTRY_OFFSET 0x1DE
#define MBR_PARTITION_4_ENTRY_OFFSET 0x1EE
#define MBR_BOOT_SIGNATURE_OFFSET    0x1FE

#define MBR_PARTITION_ENTRY_STATUS_OFFSET         0x00
#define MBR_PARTITION_ENTRY_CHS_FIRST_ADDR_OFFSET 0x01
#define MBR_PARTITION_ENTRY_PARTITION_TYPE_OFFSET 0x04
#define MBR_PARTITION_ENTRY_CHS_LAST_ADDR_OFFSET  0x05
#define MBR_PARTITION_ENTRY_LBA_FIRST_ADDR_OFFSET 0x08
#define MBR_PARTITION_ENTRY_NUM_OF_SECTORS_OFFSET 0x0C
#define MBR_PARTITION_ENTRY_SIZE                  0x10

#define MBR_SIGNATURE 0xAA55

/*==============================================================================
  Exported object types
==============================================================================*/

/*==============================================================================
  Exported objects
==============================================================================*/

/*==============================================================================
  Exported functions
==============================================================================*/
extern uint16_t MBR_get_boot_signature(uint8_t *sector);
extern uint32_t MBR_get_partition_first_LBA_sector(int partition, uint8_t *sector);
extern uint32_t MBR_get_partition_number_of_sectors(int partition, uint8_t *sector);

/*==============================================================================
  Exported inline functions
==============================================================================*/

#ifdef __cplusplus
}
#endif

#endif /* _MBR_H_ */

/**@}*/
/*==============================================================================
  End of file
==============================================================================*/
