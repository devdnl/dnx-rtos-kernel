/*==============================================================================
File    ioctl.h

Author  Daniel Zorychta

Brief   IOCTL requests for storage class devices.

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
@defgroup DRIVER_CLASS_STORAGE_IOCTL_H_ DRIVER_CLASS_STORAGE_IOCTL_H_

Detailed Doxygen description.
*/
/**@{*/

#ifndef _DRIVER_CLASS_STORAGE_IOCTL_H_
#define _DRIVER_CLASS_STORAGE_IOCTL_H_

/*==============================================================================
  Include files
==============================================================================*/
#include "../../ioctl_macros.h"

#ifdef __cplusplus
extern "C" {
#endif

/*==============================================================================
  Exported macros
==============================================================================*/
/* predefined requests*/
/**
 *  @brief  Initialize storage device
 *  @param  None
 *  @return On success (device initialized) 0 is returned.
 *          On error (device not initialized) -1 is returned and errno is set.
 */
#define IOCTL_STORAGE__INITIALIZE _IO(STORAGE, 0x80)

/**
 *  @brief  Read storage's MBR sector and detect partitions
 *  @param  None
 *  @return On success (MBR detected) 0 is returned.
 *          On error (MBR not exist or IO error) -1 is returned and errno is set.
 */
#define IOCTL_STORAGE__READ_MBR _IO(STORAGE, 0x81)

/**
 *  @brief  Enable and reset I/O statistics of block device.
 *
 *  Requests are handled by the kernel for all block devices (S_IFBLK).
 *
 *  @param  None
 *  @return On success 0 is returned.
 *          On error -1 is returned and errno is set.
 */
#define IOCTL_STORAGE__STATS_ENABLE _IO(STORAGE, 0x82)

/**
 *  @brief  Disable I/O statistics of block device (statistics memory is released).
 *  @param  None
 *  @return On success 0 is returned.
 *          On error -1 is returned and errno is set.
 */
#define IOCTL_STORAGE__STATS_DISABLE _IO(STORAGE, 0x83)

/**
 *  @brief  Read I/O statistics of block device.
 *  @param  [RD] @ref STORAGE_stats_t * statistics (all counters are 0 if disabled)
 *  @return On success 0 is returned.
 *          On error -1 is returned and errno is set.
 */
#define IOCTL_STORAGE__GET_STATS _IOR(STORAGE, 0x84, STORAGE_stats_t *)

/** Number of blocks in LRU model used to estimate cache hit ratio. */
#define STORAGE_STATS_LRU_BLOCKS 64

/** Requests up to this number of blocks are counted in LRU model. */
#define STORAGE_STATS_LRU_MAX_REQ 8

/*==============================================================================
  Exported object types
==============================================================================*/
/**
 * @brief Block device I/O statistics.
 *
 * Request is one read or write operation on device file. Size histogram
 * contains number of requests of size (in blocks): 1, 2-7, 8-63, 64 and more.
 * LRU model counts blocks of requests not bigger than
 * @ref STORAGE_STATS_LRU_MAX_REQ blocks that are found in the list of
 * @ref STORAGE_STATS_LRU_BLOCKS recently used blocks (estimated hits of block
 * cache of that size).
 */
typedef struct {
        u64_t rd_bytes;      //!< bytes read
        u64_t wr_bytes;      //!< bytes written
        u32_t rd_req;        //!< read requests
        u32_t wr_req;        //!< write requests
        u32_t rd_drv;        //!< driver read calls (including read-modify-write)
        u32_t wr_drv;        //!< driver write calls
        u32_t rd_unaligned;  //!< read requests not aligned to block size
        u32_t wr_unaligned;  //!< write requests not aligned to block size (read-modify-write)
        u32_t rd_bounce;     //!< read requests with data copied through bounce buffer
        u32_t wr_bounce;     //!< write requests with data copied through bounce buffer
        u32_t rd_err;        //!< read errors
        u32_t wr_err;        //!< write errors
        u32_t rd_size[4];    //!< read request size histogram
        u32_t wr_size[4];    //!< write request size histogram
        u32_t rd_lru_blocks; //!< read blocks checked in LRU model
        u32_t rd_lru_hits;   //!< read blocks found in LRU model
        u32_t wr_lru_blocks; //!< written blocks checked in LRU model
        u32_t wr_lru_hits;   //!< written blocks found in LRU model
        u32_t blksize;       //!< device block size (0 or 1: not used)
        u32_t unit;          //!< block size used by statistics (at least 512 bytes)
        u32_t enabled;       //!< 1 if statistics are enabled
} STORAGE_stats_t;

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

#endif /* _DRIVER_CLASS_STORAGE_IOCTL_H_ */

/**@}*/
/*==============================================================================
  End of file
==============================================================================*/
