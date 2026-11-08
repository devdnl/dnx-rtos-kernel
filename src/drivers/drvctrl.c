/*==============================================================================
@file    drvctrl.c

@author  Daniel Zorychta

@brief   Modules support.

@note    Copyright (C) 2026 Daniel Zorychta <daniel.zorychta@gmail.com>

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

#pragma GCC optimize("O3")

/*==============================================================================
  Include files
==============================================================================*/
#include <errno.h>
#include <string.h>
#include <stdbool.h>
#include "kernel/kwrapper.h"
#include "drivers/drvctrl.h"
#include "drivers/drvset.h"
#include "kernel/printk.h"
#include "kernel/process.h"
#include "kernel/syscall.h"
#include "kernel/sysfunc.h"
#include "lib/vt100.h"
#include "lib/llist.h"
#include "fs/vfs.h"
#include "lib/misc.h"
#include "drivers/class/storage/ioctl.h"
#include "mm/mm.h"

/*==============================================================================
  Local macros
==============================================================================*/
#undef errno

#define DRIVER_NAME      "Driver %s:%d-%d"
#define DRIVER_NAME_ARGS module, major, minor

/* minimal block size used by I/O statistics */
#define BLK_STATS_MIN_UNIT 512

/* maximal size of bounce buffer used when data buffer does not meet driver requirements */
#define BLK_BOUNCE_MAX_SIZE 4096

/*==============================================================================
  Local object types
==============================================================================*/
/* block device I/O statistics */
typedef struct blkstat {
        STORAGE_stats_t pub;
        u32_t lru_used;
        u32_t lru[STORAGE_STATS_LRU_BLOCKS];
} blkstat_t;

typedef struct drvmem {
        struct drvmem *next;
        void *mem;
        dev_t devid;
        u32_t blksize;           /* block device: block size used for alignment (0 or 1: not used) */
        u32_t rdbufalign;        /* block device: required read buffer alignment (0 or 1: any) */
        u32_t wrbufalign;        /* block device: required write buffer alignment (0 or 1: any) */
        bool bufdma;             /* block device: data buffer must be in DMA capable memory */
        const char **bufregions; /* block device: allowed memory regions of data buffer (NULL: any) */
        blkstat_t *stat;         /* block device: I/O statistics (NULL if disabled) */
} drvmem_t;

/* bounce buffer */
typedef struct {
        void *mem;   /* allocated memory */
        u8_t *buf;   /* aligned buffer */
        size_t size; /* buffer size */
} blkbuf_t;

/*==============================================================================
  Local function prototypes
==============================================================================*/

/*==============================================================================
  Local objects
==============================================================================*/
static drvmem_t **drvmem;
static kmtx_t *blk_mtx; /* serializes read-modify-write operations of block devices (recursive: stacked devices) */

/*==============================================================================
  Exported objects
==============================================================================*/

/*==============================================================================
  External objects
==============================================================================*/
extern const struct _module_entry _drvreg_module_table[];

/*==============================================================================
  Function definitions
==============================================================================*/

//==============================================================================
/**
 * @brief  Function returns number of modules (built-in and drvset modules).
 *
 * @return Number of modules.
 */
//==============================================================================
static inline size_t module__count(void)
{
#if (__OS_ENABLE_DRVSET__ > 0)
        return _drvreg_number_of_modules + _drvset_get_module_count();
#else
        return _drvreg_number_of_modules;
#endif
}

//==============================================================================
/**
 * @brief  Function returns module entry. Module number shall be checked by
 *         caller (module__count()).
 *
 * @param  modno        module number
 *
 * @return Module entry.
 */
//==============================================================================
static inline const struct _module_entry *module__entry(size_t modno)
{
#if (__OS_ENABLE_DRVSET__ > 0)
        if (modno >= _drvreg_number_of_modules) {
                return _drvset_get_module(modno - _drvreg_number_of_modules);
        }
#endif
        return &_drvreg_module_table[modno];
}
//==============================================================================
/**
 * @brief Check if device is running
 *
 * @param [in]  id      driver ID
 * @param [out] modno   module number
 * @param [out] mem     module memory
 *
 * @return On success ESUCC, otherwise other values.
 */
//==============================================================================
static int driver__get(dev_t id, u16_t *modno, drvmem_t **drv)
{
        int err = EINVAL;

        if (modno && drv && id != -1) {
                err = ENODEV;

                *modno = _dev_t__extract_modno(id);

                if (drvmem && (*modno < module__count())) {
                        _kernel_scheduler_lock();
                        {
                                for (drvmem_t *d = drvmem[*modno]; d != NULL && err == ENODEV; d = d->next) {

                                        if (d->devid == id) {
                                                *drv = d;
                                                err = ESUCC;
                                        }
                                }
                        }
                        _kernel_scheduler_unlock();
                }
        }

        return err;
}

//==============================================================================
/**
 * @brief Check if device is running
 *
 * @param [in]  id      driver ID
 * @param [out] modno   module number
 * @param [out] mem     module memory
 *
 * @return On success ESUCC, otherwise other values.
 */
//==============================================================================
static int driver__get_module_no_and_mem(dev_t id, u16_t *modno, void **mem)
{
        drvmem_t *drv = NULL;

        int err = mem ? driver__get(id, modno, &drv) : EINVAL;
        if (!err) {
                *mem = drv->mem;
        }

        return err;
}

//==============================================================================
/**
 * @brief  Function checks whether module provides block devices.
 *
 * @param  modno        module number
 *
 * @return true if block device module, otherwise false.
 */
//==============================================================================
static inline bool driver__is_block(u16_t modno)
{
        return module__entry(modno)->type == S_IFBLK;
}

//==============================================================================
/**
 * @brief  Function reads block size and data buffer requirements of block
 *         device (from device statistics).
 *
 * @param  modno        module number
 * @param  drv          driver instance
 */
//==============================================================================
static void blk__update_params(u16_t modno, drvmem_t *drv)
{
        struct vfs_dev_stat stat;
        memset(&stat, 0, sizeof(stat));

        stat.st_major = _dev_t__extract_major(drv->devid);
        stat.st_minor = _dev_t__extract_minor(drv->devid);

        if (module__entry(modno)->IF.drv_stat(drv->mem, &stat) == ESUCC) {
                drv->blksize = stat.st_blksize;
                drv->rdbufalign = stat.st_rdbufalign;
                drv->wrbufalign = stat.st_wrbufalign;
                drv->bufdma = stat.st_bufdma;
                drv->bufregions = stat.st_bufregions;
        }
}

//==============================================================================
/**
 * @brief  Function checks whether data buffer meets driver requirements.
 *
 * @param  drv          driver instance
 * @param  write        true: write buffer, false: read buffer
 * @param  buf          data buffer
 *
 * @return true if buffer can be passed to the driver, otherwise false.
 */
//==============================================================================
static bool blk__buf_ok(const drvmem_t *drv, bool write, const void *buf)
{
        const u32_t align = write ? drv->wrbufalign : drv->rdbufalign;

        if ((align > 1) && ((cast(uintptr_t, buf) % align) != 0)) {
                return false;
        }

        if (drv->bufdma && !_mm_is_dma_capable(buf)) {
                return false;
        }

        if (drv->bufregions && !_mm_is_mem_region(buf, drv->bufregions)) {
                return false;
        }

        return true;
}

//==============================================================================
/**
 * @brief  Function calculates size of bounce buffer (multiple of block size).
 *
 * @param  bs           block size
 * @param  count        request size
 *
 * @return Bounce buffer size.
 */
//==============================================================================
static size_t blk__bounce_size(u32_t bs, size_t count)
{
        size_t max_size = max(BLK_BOUNCE_MAX_SIZE - (BLK_BOUNCE_MAX_SIZE % bs), bs);
        size_t need = ((count / bs) + ((count % bs) ? 1 : 0)) * bs;

        return max(min(need, max_size), bs);
}

//==============================================================================
/**
 * @brief  Function allocates bounce buffer which meets driver requirements
 *         (alignment, DMA capable memory, memory regions).
 *
 * @param  drv          driver instance
 * @param  size         buffer size
 * @param  bb           bounce buffer
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
static int blk__buf_alloc(const drvmem_t *drv, size_t size, blkbuf_t *bb)
{
        const size_t align = max(drv->rdbufalign, drv->wrbufalign);
        const u32_t flags = drv->bufdma ? _MM_FLAG__DMA : 0;

        bb->mem = NULL;

        int err = _kmalloc(_MM_KRN, size, align, drv->bufregions, flags, flags, &bb->mem);
        if (!err) {
                bb->buf = bb->mem;
                bb->size = size;
        }

        return err;
}

//==============================================================================
/**
 * @brief  Function checks block in LRU model and moves it to the front.
 *
 * @param  st           statistics
 * @param  blk          block number
 *
 * @return true if block was found in the model (cache hit), otherwise false.
 */
//==============================================================================
static bool blk__lru_access(blkstat_t *st, u32_t blk)
{
        u32_t i = 0;

        while ((i < st->lru_used) && (st->lru[i] != blk)) {
                i++;
        }

        bool hit = (i < st->lru_used);

        if (!hit) {
                if (st->lru_used < STORAGE_STATS_LRU_BLOCKS) {
                        st->lru_used++;
                }

                i = st->lru_used - 1;
        }

        memmove(&st->lru[1], &st->lru[0], i * sizeof(st->lru[0]));
        st->lru[0] = blk;

        return hit;
}

//==============================================================================
/**
 * @brief  Function counts driver call in statistics.
 *
 * @param  drv          driver instance
 * @param  write        true: write call, false: read call
 */
//==============================================================================
static void blk__count_call(drvmem_t *drv, bool write)
{
        if (drv->stat) {
                _kernel_scheduler_lock();
                {
                        if (drv->stat) {
                                if (write) {
                                        drv->stat->pub.wr_drv++;
                                } else {
                                        drv->stat->pub.rd_drv++;
                                }
                        }
                }
                _kernel_scheduler_unlock();
        }
}

//==============================================================================
/**
 * @brief  Function counts request in statistics.
 *
 * @param  drv          driver instance
 * @param  write        true: write request, false: read request
 * @param  pos          request position
 * @param  count        request size
 * @param  unaligned    request not aligned to block size
 * @param  bounced      data copied through bounce buffer
 * @param  err          request result
 */
//==============================================================================
static void blk__account(drvmem_t *drv, bool write, fpos_t pos, size_t count, bool unaligned, bool bounced, int err)
{
        _kernel_scheduler_lock();
        {
                blkstat_t *st = drv->stat;
                if (st) {
                        STORAGE_stats_t *s = &st->pub;

                        u64_t first = cast(u64_t, pos) / s->unit;
                        u64_t last = (count > 0) ? (cast(u64_t, pos) + count - 1) / s->unit : first;
                        u64_t blocks = last - first + 1;
                        u32_t bucket = (blocks <= 1) ? 0 : (blocks < 8) ? 1 : (blocks < 64) ? 2 : 3;

                        if (write) {
                                s->wr_req++;
                                s->wr_bytes += count;
                                s->wr_size[bucket]++;
                                s->wr_unaligned += unaligned ? 1 : 0;
                                s->wr_bounce += bounced ? 1 : 0;
                                s->wr_err += err ? 1 : 0;
                        } else {
                                s->rd_req++;
                                s->rd_bytes += count;
                                s->rd_size[bucket]++;
                                s->rd_unaligned += unaligned ? 1 : 0;
                                s->rd_bounce += bounced ? 1 : 0;
                                s->rd_err += err ? 1 : 0;
                        }

                        if (blocks <= STORAGE_STATS_LRU_MAX_REQ) {
                                for (u64_t blk = first; blk <= last; blk++) {
                                        bool hit = blk__lru_access(st, cast(u32_t, blk));

                                        if (write) {
                                                s->wr_lru_blocks++;
                                                s->wr_lru_hits += hit ? 1 : 0;
                                        } else {
                                                s->rd_lru_blocks++;
                                                s->rd_lru_hits += hit ? 1 : 0;
                                        }
                                }
                        }
                }
        }
        _kernel_scheduler_unlock();
}

//==============================================================================
/**
 * @brief  Function calls driver read function.
 *
 * @param  modno        module number
 * @param  drv          driver instance
 * @param  dst          destination buffer
 * @param  count        bytes to read
 * @param  pos          position
 * @param  rdcnt        number of read bytes
 * @param  fattr        file attributes
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
static int blk__drv_read(u16_t modno, drvmem_t *drv, u8_t *dst, size_t count, fpos_t pos, size_t *rdcnt,
        struct vfs_fattr fattr)
{
        *rdcnt = 0;
        int err = module__entry(modno)->IF.drv_read(drv->mem, dst, count, &pos, rdcnt, fattr);
        blk__count_call(drv, false);
        return err;
}

//==============================================================================
/**
 * @brief  Function calls driver write function.
 *
 * @param  modno        module number
 * @param  drv          driver instance
 * @param  src          source buffer
 * @param  count        bytes to write
 * @param  pos          position
 * @param  wrcnt        number of written bytes
 * @param  fattr        file attributes
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
static int blk__drv_write(u16_t modno, drvmem_t *drv, const u8_t *src, size_t count, fpos_t pos, size_t *wrcnt,
        struct vfs_fattr fattr)
{
        *wrcnt = 0;
        int err = module__entry(modno)->IF.drv_write(drv->mem, src, count, &pos, wrcnt, fattr);
        blk__count_call(drv, true);
        return err;
}

//==============================================================================
/**
 * @brief  Function transfers data of block device request that can not be
 *         passed to the driver directly:
 *         - position or size not aligned to block size: partial blocks are read
 *           to bounce buffer (and written back: read-modify-write),
 *         - data buffer does not meet driver requirements (alignment, DMA
 *           capable memory, memory regions): data is copied through bounce
 *           buffer.
 *
 * @param  modno        module number
 * @param  drv          driver instance
 * @param  write        true: write request, false: read request
 * @param  data         data buffer
 * @param  count        request size
 * @param  pos          position
 * @param  cnt          number of transferred bytes
 * @param  fattr        file attributes
 * @param  bounced      set to true if data buffer was replaced by bounce buffer
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
static int blk__xfer(u16_t modno, drvmem_t *drv, bool write, u8_t *data, size_t count, fpos_t pos, size_t *cnt,
        struct vfs_fattr fattr, bool *bounced)
{
        const u32_t bs = max(drv->blksize, 1);

        blkbuf_t bb = {NULL, NULL, 0};
        int err = ESUCC;
        bool locked = false;
        size_t done = 0;

        /* read-modify-write must not be interleaved with other read-modify-write */
        if (write && (bs > 1) && (((cast(u64_t, pos) % bs) != 0) || ((count % bs) != 0))) {
                err = _mutex_lock(blk_mtx, _MAX_DELAY_MS);
                locked = (err == ESUCC);
        }

        while (!err && (done < count)) {
                const fpos_t p = pos + done;
                const size_t off = cast(u64_t, p) % bs;
                const size_t rem = count - done;
                u8_t *ptr = data + done;
                size_t n = 0;

                if ((off == 0) && (rem >= bs) && blk__buf_ok(drv, write, ptr)) {
                        /* whole blocks directly from/to data buffer */
                        size_t len = rem - (rem % bs);

                        if (write) {
                                err = blk__drv_write(modno, drv, ptr, len, p, &n, fattr);
                        } else {
                                err = blk__drv_read(modno, drv, ptr, len, p, &n, fattr);
                        }

                        done += min(n, len);

                        if (n < len) {
                                break;
                        }

                        continue;
                }

                if (bb.mem == NULL) {
                        err = blk__buf_alloc(drv, blk__bounce_size(bs, count), &bb);
                        if (err) {
                                break;
                        }
                }

                if ((off == 0) && (rem >= bs)) {
                        /* whole blocks through bounce buffer */
                        size_t len = min(rem - (rem % bs), bb.size);
                        *bounced = true;

                        if (write) {
                                memcpy(bb.buf, ptr, len);
                                err = blk__drv_write(modno, drv, bb.buf, len, p, &n, fattr);
                        } else {
                                err = blk__drv_read(modno, drv, bb.buf, len, p, &n, fattr);
                                if (!err) {
                                        memcpy(ptr, bb.buf, min(n, len));
                                }
                        }

                        done += min(n, len);

                        if (n < len) {
                                break;
                        }

                } else {
                        /* partial block */
                        size_t len = min(bs - off, rem);

                        err = blk__drv_read(modno, drv, bb.buf, bs, p - off, &n, fattr);
                        if (err) {
                                break;
                        }

                        if (write) {
                                if (n < bs) {
                                        break; // end of device
                                }

                                memcpy(bb.buf + off, ptr, len);

                                err = blk__drv_write(modno, drv, bb.buf, bs, p - off, &n, fattr);
                                if (!err && (n < bs)) {
                                        break;
                                }

                                if (!err) {
                                        done += len;
                                }

                        } else {
                                size_t avail = (n > off) ? min(n - off, len) : 0;
                                memcpy(ptr, bb.buf + off, avail);
                                done += avail;

                                if (avail < len) {
                                        break;
                                }
                        }
                }
        }

        if (locked) {
                _mutex_unlock(blk_mtx);
        }

        *cnt += done;

        if (bb.mem) {
                _kfree(_MM_KRN, &bb.mem);
        }

        return err;
}

//==============================================================================
/**
 * @brief  Function handles block device requests implemented by the kernel.
 *
 * @param  drv          driver instance
 * @param  request      ioctl request
 * @param  arg          request argument
 * @param  err          request result
 *
 * @return true if request was handled, otherwise false (request for driver).
 */
//==============================================================================
static bool blk__ioctl(drvmem_t *drv, int request, void *arg, int *err)
{
        switch (request) {
        case IOCTL_STORAGE__STATS_ENABLE: {
                blkstat_t *st = NULL;
                *err = _kzalloc(_MM_KRN, sizeof(blkstat_t), 0, NULL, 0, 0, cast(void *, &st));

                if (!*err) {
                        st->pub.blksize = drv->blksize;
                        st->pub.unit = max(drv->blksize, BLK_STATS_MIN_UNIT);
                        st->pub.enabled = 1;

                        _kernel_scheduler_lock();
                        {
                                blkstat_t *old = drv->stat;
                                drv->stat = st;
                                st = old;
                        }
                        _kernel_scheduler_unlock();

                        if (st) {
                                _kfree(_MM_KRN, cast(void *, &st));
                        }
                }

                return true;
        }

        case IOCTL_STORAGE__STATS_DISABLE: {
                blkstat_t *st = NULL;

                _kernel_scheduler_lock();
                {
                        st = drv->stat;
                        drv->stat = NULL;
                }
                _kernel_scheduler_unlock();

                if (st) {
                        _kfree(_MM_KRN, cast(void *, &st));
                }

                *err = ESUCC;
                return true;
        }

        case IOCTL_STORAGE__GET_STATS: {
                STORAGE_stats_t *out = arg;

                if (out) {
                        memset(out, 0, sizeof(*out));
                        out->blksize = drv->blksize;
                        out->unit = max(drv->blksize, BLK_STATS_MIN_UNIT);

                        _kernel_scheduler_lock();
                        {
                                if (drv->stat) {
                                        *out = drv->stat->pub;
                                }
                        }
                        _kernel_scheduler_unlock();

                        *err = ESUCC;
                } else {
                        *err = EINVAL;
                }

                return true;
        }

        default:
                return false;
        }
}

//==============================================================================
/**
 * @brief  Register driver in system
 *
 * @param [in]  modno   module number
 * @param [in]  major   module major number
 * @param [in]  minor   module minor number
 * @param [out] drv     registered module memory
 *
 * @return On success ESUCC is returned. ENODEV and EADDRINUSE on error.
 */
//==============================================================================
static int driver__register(u16_t modno, u8_t major, u8_t minor, drvmem_t **drv)
{
        int err = ENODEV;

        if (modno < module__count() && drv) {

                err = ESUCC;

                _kernel_scheduler_lock();
                {
                        // find that module is not already initialized
                        for (drvmem_t *drv = drvmem[modno]; drv && !err; drv = drv->next) {
                                if (_dev_t__extract_major(drv->devid) == major &&
                                        _dev_t__extract_minor(drv->devid) == minor) {

                                        err = EADDRINUSE;
                                }
                        }

                        // create new driver chain
                        if (!err) {
                                err = _kzalloc(_MM_KRN, sizeof(drvmem_t), 0, _cpuctl_fast_mem, 0, 0, cast(void *, drv));
                                if (!err) {
                                        (*drv)->devid = _dev_t__create(modno, major, minor);
                                        (*drv)->mem = NULL;
                                        (*drv)->next = NULL;

                                        if (drvmem[modno] == NULL) {
                                                drvmem[modno] = *drv;
                                        } else {
                                                drvmem_t *curr = drvmem[modno];
                                                for (; curr->next; curr = curr->next)
                                                        ;
                                                curr->next = *drv;
                                        }
                                }
                        }
                }
                _kernel_scheduler_unlock();
        }

        return err;
}

//==============================================================================
/**
 * @brief  Remove driver from register list
 *
 * @param  devid        device id
 *
 * @return None
 */
//==============================================================================
static void driver__remove(dev_t devid)
{
        u16_t modno = _dev_t__extract_modno(devid);

        if (modno < module__count()) {

                _kernel_scheduler_lock();
                {
                        drvmem_t *prev = NULL;
                        drvmem_t *curr = drvmem[modno];

                        for (; curr; curr = curr->next) {
                                if (curr->devid == devid) {
                                        void *tofree = curr;

                                        if (curr->stat) {
                                                _kfree(_MM_KRN, cast(void *, &curr->stat));
                                        }

                                        if (prev) {
                                                prev->next = curr->next;
                                        } else {
                                                drvmem[modno] = curr->next;
                                        }

                                        _kfree(_MM_KRN, &tofree);

                                        break;
                                }

                                prev = curr;
                        }
                }
                _kernel_scheduler_unlock();
        }
}

//==============================================================================
/**
 * @brief  Initialize selected module
 *
 * @param  modno        module number
 * @param  major        module major number
 * @param  minor        module minor number
 * @param  config       module configuration (optional, NULL if not used)
 * @param  mem          module memory to be allocated by driver
 *
 * @return One of errno value.
 */
//==============================================================================
static inline int driver__initialize(u16_t modno, u8_t major, u8_t minor, const void *config, void **mem)
{
        return module__entry(modno)->IF.drv_init(mem, major, minor, config);
}

//==============================================================================
/**
 * @brief  Release module
 *
 * @param  modno        module number
 * @param  mem          module memory
 *
 * @return One of errno value.
 */
//==============================================================================
static inline int driver__release(u16_t modno, void *mem)
{
        return module__entry(modno)->IF.drv_release(mem);
}

//==============================================================================
/**
 * @brief Function find driver name and then initialize device
 *
 * @param [IN]  module              module name
 * @param [IN]  major               major number
 * @param [IN]  minor               minor number
 * @param [IN]  node_path           path name to create in the file system (can be NULL or empty string)
 * @param [IN]  config              driver configuration (can be NULL)
 * @param [OUT] id                  module id (can be NULL)
 *
 * @return One of error code (errno)
 */
//==============================================================================
int _driver_init(const char *module, u8_t major, u8_t minor, const char *node_path, const void *config, dev_t *id)
{
        int err;

        // allocate modules memory handles
        if (drvmem == NULL) {
                err = _kzalloc(_MM_KRN, _MODULES_MAX * sizeof(drvmem_t *), 0, _cpuctl_fast_mem, 0, 0,
                        cast(void *, &drvmem));

                if (err) {
                        return err;
                }
        }

        if (blk_mtx == NULL) {
                err = _mutex_create(KMTX_TYPE_RECURSIVE, &blk_mtx);
                if (err) {
                        return err;
                }
        }

        // initialize selected module
        int modno = _module_get_ID(module);
        drvmem_t *drv = NULL;
        err = driver__register(modno, major, minor, &drv);
        if (!err) {

                err = driver__initialize(modno, major, minor, config, &drv->mem);
                if (!err) {
                        if (id) {
                                *id = drv->devid;
                        }

                        if (node_path) {
                                struct vfs_path cpath;
                                cpath.CWD = NULL;
                                cpath.PATH = node_path;

                                err = _vfs_mknod(&cpath, drv->devid);
                                if (!err) {
                                        printk(DRIVER_NAME " initialized as %s", DRIVER_NAME_ARGS, node_path);

                                } else {
                                        driver__release(modno, drv->mem);
                                        driver__remove(drv->devid);
                                        printk(DRIVER_NAME " node create fail (%d)", DRIVER_NAME_ARGS, err);
                                }
                        } else {
                                printk(DRIVER_NAME " initialized", DRIVER_NAME_ARGS);
                        }

                } else {
                        driver__remove(drv->devid);
                        printk(DRIVER_NAME " initialization error (%d)", DRIVER_NAME_ARGS, err);
                }
        } else {
                switch (err) {
                case EADDRINUSE:
                        printk(DRIVER_NAME " already initialized", DRIVER_NAME_ARGS);
                        break;
                default:
                        printk(DRIVER_NAME " not exist", DRIVER_NAME_ARGS);
                        break;
                }
        }

        return err;
}

//==============================================================================
/**
 * @brief Function release selected device by using device ID
 *
 * @param id                   device id
 *
 * @return One of error code (errno)
 */
//==============================================================================
int _driver_release(dev_t id)
{
        u16_t modno;
        void *mem;

        int err = driver__get_module_no_and_mem(id, &modno, &mem);
        if (!err) {
#if (__OS_SYSTEM_MSG_ENABLE__ > 0)
                u8_t major = _dev_t__extract_major(id);
                u8_t minor = _dev_t__extract_minor(id);
                const char *module = _module_get_name(modno);
#endif

                err = driver__release(modno, mem);
                if (!err) {
                        driver__remove(id);
                        printk(DRIVER_NAME " released", DRIVER_NAME_ARGS);
                } else {
                        printk(DRIVER_NAME " release fail (%d)", DRIVER_NAME_ARGS, err);
                }
        }

        return err;
}

//==============================================================================
/**
 * @brief Function open selected driver
 *
 * @param id           module id
 * @param flags         flags
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
int _driver_open(dev_t id, u32_t flags)
{
        u16_t modno;
        drvmem_t *drv;

        int err = driver__get(id, &modno, &drv);
        if (!err) {
                err = module__entry(modno)->IF.drv_open(drv->mem, vfs_filter_flags_for_device(flags));

                if (!err && driver__is_block(modno)) {
                        blk__update_params(modno, drv);
                }
        }

        return err;
}

//==============================================================================
/**
 * @brief Function close selected driver
 *
 * @param id            module id
 * @param force         force close request
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
int _driver_close(dev_t id, bool force)
{
        u16_t modno;
        void *mem;

        int err = driver__get_module_no_and_mem(id, &modno, &mem);
        if (!err) {
                err = module__entry(modno)->IF.drv_close(mem, force);
        }

        return err;
}

//==============================================================================
/**
 * @brief Function write data to driver
 *
 * @param id            module id
 * @param src           data source
 * @param count         buffer size
 * @param fpos          file position
 * @param wrcnt         number of written bytes
 * @param fattr         file attributes
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
int _driver_write(dev_t id, const u8_t *src, size_t count, fpos_t *fpos, size_t *wrcnt, struct vfs_fattr fattr)
{
        u16_t modno;
        drvmem_t *drv;

        int err = driver__get(id, &modno, &drv);
        if (!err) {
                const fpos_t pos = *fpos;
                const u32_t bs = drv->blksize;
                const bool unaligned = (bs > 1) && (((cast(u64_t, pos) % bs) != 0) || ((count % bs) != 0));
                bool bounced = false;

                if (unaligned || !blk__buf_ok(drv, true, src)) {
                        err = blk__xfer(modno, drv, true, const_cast(src), count, pos, wrcnt, fattr, &bounced);
                } else {
                        err = module__entry(modno)->IF.drv_write(drv->mem, src, count, fpos, wrcnt, fattr);
                        blk__count_call(drv, true);
                }

                if (drv->stat) {
                        blk__account(drv, true, pos, count, unaligned, bounced, err);
                }
        }

        return err;
}

//==============================================================================
/**
 * @brief Function read data to driver
 *
 * @param id            module id
 * @param dst           data destination
 * @param count         buffer size
 * @param fpos          file position
 * @param rdcnt         number of read bytes
 * @param fattr         file attributes
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
int _driver_read(dev_t id, u8_t *dst, size_t count, fpos_t *fpos, size_t *rdcnt, struct vfs_fattr fattr)
{
        u16_t modno;
        drvmem_t *drv;

        int err = driver__get(id, &modno, &drv);
        if (!err) {
                const fpos_t pos = *fpos;
                const u32_t bs = drv->blksize;
                const bool unaligned = (bs > 1) && (((cast(u64_t, pos) % bs) != 0) || ((count % bs) != 0));
                bool bounced = false;

                if (unaligned || !blk__buf_ok(drv, false, dst)) {
                        err = blk__xfer(modno, drv, false, dst, count, pos, rdcnt, fattr, &bounced);
                } else {
                        err = module__entry(modno)->IF.drv_read(drv->mem, dst, count, fpos, rdcnt, fattr);
                        blk__count_call(drv, false);
                }

                if (drv->stat) {
                        blk__account(drv, false, pos, count, unaligned, bounced, err);
                }
        }

        return err;
}

//==============================================================================
/**
 * @brief IO control
 *
 * @param id            module id
 * @param request       io request
 * @param arg           argument
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
int _driver_ioctl(dev_t id, int request, void *arg)
{
        u16_t modno;
        drvmem_t *drv;

        int err = driver__get(id, &modno, &drv);
        if (!err) {
                if (driver__is_block(modno) && blk__ioctl(drv, request, arg, &err)) {
                        return err;
                }

                err = module__entry(modno)->IF.drv_ioctl(drv->mem, request, arg);

                /* block size and buffer requirements can be known after device initialization */
                if (!err && driver__is_block(modno) && (request == IOCTL_STORAGE__INITIALIZE)) {
                        blk__update_params(modno, drv);
                }
        }

        return err;
}

//==============================================================================
/**
 * @brief Flush device buffer (forces write)
 *
 * @param id            module id
 * @param request       io request
 * @param arg           argument
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
int _driver_flush(dev_t id)
{
        u16_t modno;
        void *mem;

        int err = driver__get_module_no_and_mem(id, &modno, &mem);
        if (!err) {
                err = module__entry(modno)->IF.drv_flush(mem);
        }

        return err;
}

//==============================================================================
/**
 * @brief Device information
 *
 * @param id            module id
 * @param stat          status object
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
int _driver_stat(dev_t id, struct vfs_dev_stat *stat)
{
        u16_t modno;
        void *mem;

        int err = driver__get_module_no_and_mem(id, &modno, &mem);
        if (!err) {
                stat->st_major = _dev_t__extract_major(id);
                stat->st_minor = _dev_t__extract_minor(id);
                stat->st_size = 0;
                stat->st_blksize = 0;
                stat->st_rdbufalign = 0;
                stat->st_wrbufalign = 0;
                stat->st_bufdma = false;
                stat->st_bufregions = NULL;
                err = module__entry(modno)->IF.drv_stat(mem, stat);
        }

        return err;
}

//==============================================================================
/**
 * @brief Function returns type of device (character or block device). The type
 *        is a property of the module, so the device does not need to be
 *        initialized.
 *
 * @param id            device id
 *
 * @return S_IFBLK for block devices, S_IFCHR for character devices or unknown
 *         modules.
 */
//==============================================================================
mode_t _driver_get_type(dev_t id)
{
        u16_t modno = _dev_t__extract_modno(id);

        if (modno < module__count()) {
                return module__entry(modno)->type;
        } else {
                return S_IFCHR;
        }
}

//==============================================================================
/**
 * @brief  Function return instance of selected module.
 *
 * @param  module_name          module name
 * @param  major                module major number
 * @param  minor                module minor number
 * @param  mem                  memory instance
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
int _module_get_instance(const char *module_name, u8_t major, u8_t minor, void **mem)
{
        dev_t dev = _dev_t__create(_module_get_ID(module_name), major, minor);

        u16_t modno = 0;
        return driver__get_module_no_and_mem(dev, &modno, mem);
}

//==============================================================================
/**
 * @brief Function return module name
 *
 * @param module_number         the module number in the table
 *
 * @return pointer to module's name or NULL if error
 */
//==============================================================================
const char *_module_get_name(size_t module_number)
{
        if (module_number >= module__count()) {
                return NULL;
        } else {
                return module__entry(module_number)->name;
        }
}

//==============================================================================
/**
 * @brief Function returns number of modules
 *
 * @param None
 *
 * @return Number of modules
 */
//==============================================================================
size_t _module_get_count(void)
{
        return module__count();
};

//==============================================================================
/**
 * @brief Function return module number
 *
 * @param[in] *module_name      module name
 *
 * @return module number, -1 on error
 */
//==============================================================================
int _module_get_ID(const char *module_name)
{
        if (module_name) {
                for (size_t module = 0; module < module__count(); module++) {
                        if (strcmp(module__entry(module)->name, module_name) == 0) {
                                return module;
                        }
                }
        }

        return -1;
}

//==============================================================================
/**
 * @brief Function return number of module instances.
 *
 * @param n             module number
 *
 * @return Number of instances or -1 on error.
 */
//==============================================================================
ssize_t _module_get_number_of_instances(size_t n)
{
        int instances = -1;

        if (n < module__count()) {
                _kernel_scheduler_lock();
                {
                        instances = 0;
                        for (drvmem_t *drv = drvmem[n]; drv; instances++, drv = drv->next)
                                ;
                }
                _kernel_scheduler_unlock();
        }

        return instances;
}

//==============================================================================
/**
 * @brief Function lock device for this task
 *
 * Doxygen documentation in drivers/driver.h.
 *
 * ERRNO: EBUSY
 *
 * @param *dev_lock     pointer to device lock object
 *
 * @return One of errno value (ESUCC for success)
 */
//==============================================================================
int _device_lock(dev_lock_t *dev_lock)
{
        int err = EINVAL;

        if (dev_lock) {
                _kernel_scheduler_lock();
                {
                        if (*dev_lock == 0) {

                                *dev_lock = _process_get_active_process_pid();
                                if (*dev_lock == 0) {
                                        _process_get_pid(_kworker_proc, dev_lock);
                                }

                                err = ESUCC;
                        } else {
                                err = EBUSY;
                        }
                }
                _kernel_scheduler_unlock();
        }

        return err;
}

//==============================================================================
/**
 * @brief Function unlock before locked device
 *
 * Doxygen documentation in drivers/driver.h.
 *
 * @param *dev_lock     pointer to device lock object
 * @param  force        true: force unlock
 *
 * @return One of errno value (ESUCC for success)
 */
//==============================================================================
int _device_unlock(dev_lock_t *dev_lock, bool force)
{
        int err = EINVAL;

        if (dev_lock) {
                _kernel_scheduler_lock();
                {
                        pid_t kworker_pid = 0;
                        _process_get_pid(_kworker_proc, &kworker_pid);

                        pid_t client_pid = 0;
                        client_pid = _process_get_active_process_pid();

                        if (force == true || *dev_lock == client_pid || *dev_lock == kworker_pid) {

                                *dev_lock = 0;
                                err = ESUCC;
                        } else {
                                err = EBUSY;
                        }
                }
                _kernel_scheduler_unlock();
        }

        return err;
}

//==============================================================================
/**
 * @brief Function check that current task has access to device
 *
 * Doxygen documentation in drivers/driver.h.
 *
 * @param *dev_lock     pointer to device lock object
 *
 * @return One of errno value (ESUCC for access granted)
 */
//==============================================================================
int _device_get_access(dev_lock_t *dev_lock)
{
        int err = EINVAL;

        if (dev_lock) {
                _kernel_scheduler_lock();
                {
                        pid_t kworker_pid = 0;
                        _process_get_pid(_kworker_proc, &kworker_pid);

                        pid_t client_pid = 0;
                        client_pid = _process_get_active_process_pid();

                        if (*dev_lock == client_pid || *dev_lock == kworker_pid) {

                                err = ESUCC;
                        } else {
                                err = EBUSY;
                        }
                }
                _kernel_scheduler_unlock();
        }

        return err;
}

//==============================================================================
/**
 * @brief Function check that device is locked
 *
 * Doxygen documentation in drivers/driver.h.
 *
 * @param *dev_lock     pointer to device lock object
 *
 * @return true if locked, otherwise false
 */
//==============================================================================
bool _device_is_locked(dev_lock_t *dev_lock)
{
        bool err = false;

        if (dev_lock) {
                err = *dev_lock != 0;
        }

        return err;
}

/*==============================================================================
  End of file
==============================================================================*/
