/*==============================================================================
File    urandom.c

Author  Daniel Zorychta

Brief   Cryptographically secure random number generator device

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

/*==============================================================================
  Include files
==============================================================================*/
#include "urandom.h"

/*==============================================================================
  Local macros
==============================================================================*/
#define KEY_WORDS    8
#define BLOCK_WORDS  16
#define RESEED_BYTES 32
#define ROTL(v, n)   (((v) << (n)) | ((v) >> (32 - (n))))
#define QR(a, b, c, d)               \
        do {                         \
                a += b;              \
                d = ROTL(d ^ a, 16); \
                c += d;              \
                b = ROTL(b ^ c, 12); \
                a += b;              \
                d = ROTL(d ^ a, 8);  \
                c += d;              \
                b = ROTL(b ^ c, 7);  \
        } while (0)

/*==============================================================================
  Local object types
==============================================================================*/
typedef struct {
        kmtx_t *mtx;
        u32_t key[KEY_WORDS];
        u64_t counter;
        URANDOM_source_t source;
} URANDOM_t;

/*==============================================================================
  Local function prototypes
==============================================================================*/
static void chacha20_block(URANDOM_t *hdl, u32_t out[BLOCK_WORDS]);
static void rekey(URANDOM_t *hdl);
static void mix(URANDOM_t *hdl, const u8_t *data, size_t len);
static void reseed(URANDOM_t *hdl);
static void wipe(void *mem, size_t len);

/*==============================================================================
  Local object
==============================================================================*/
MODULE_NAME(URANDOM);

/*==============================================================================
  Exported object
==============================================================================*/

/*==============================================================================
  Function definitions
==============================================================================*/

//==============================================================================
/**
 * @brief Initialize device.
 *
 * @param[out]          **device_handle        device allocated memory
 * @param[in ]            major                major device number
 * @param[in ]            minor                minor device number
 *
 * @return One of errno value (errno.h).
 */
//==============================================================================
API_MOD_INIT(URANDOM, void **device_handle, u8_t major, u8_t minor)
{
        UNUSED_ARG1(major);

        if (minor != 0) {
                return ENODEV;
        }

        int err = sys_zalloc(sizeof(URANDOM_t), device_handle);
        if (!err) {
                URANDOM_t *hdl = *device_handle;

                err = sys_mutex_create(KMTX_TYPE_NORMAL, &hdl->mtx);
                if (!err) {
                        hdl->source = _URANDOM_LLD__init();
                        reseed(hdl);

                        if (hdl->source == URANDOM_SOURCE__WEAK) {
                                printk("URANDOM: no hardware entropy source, write secret seed to device");
                        }
                } else {
                        sys_free(device_handle);
                }
        }

        return err;
}

//==============================================================================
/**
 * @brief Release device.
 *
 * @param[in ]          *device_handle          device allocated memory
 *
 * @return One of errno value (errno.h).
 */
//==============================================================================
API_MOD_RELEASE(URANDOM, void *device_handle)
{
        URANDOM_t *hdl = device_handle;

        int err = sys_mutex_lock(hdl->mtx, _WAIT_FOREVER_MS);
        if (!err) {
                kmtx_t *mtx = hdl->mtx;
                _URANDOM_LLD__deinit();
                wipe(hdl, sizeof(*hdl));
                sys_free(&device_handle);
                sys_mutex_unlock(mtx);
                sys_mutex_destroy(mtx);
        }

        return err;
}

//==============================================================================
/**
 * @brief Open device.
 *
 * @param[in ]          *device_handle          device allocated memory
 * @param[in ]           flags                  file operation flags (O_RDONLY, O_WRONLY, O_RDWR)
 *
 * @return One of errno value (errno.h).
 */
//==============================================================================
API_MOD_OPEN(URANDOM, void *device_handle, u32_t flags)
{
        UNUSED_ARG2(device_handle, flags);

        return ESUCC;
}

//==============================================================================
/**
 * @brief Close device.
 *
 * @param[in ]          *device_handle          device allocated memory
 * @param[in ]           force                  device force close (true)
 *
 * @return One of errno value (errno.h).
 */
//==============================================================================
API_MOD_CLOSE(URANDOM, void *device_handle, bool force)
{
        UNUSED_ARG2(device_handle, force);

        return ESUCC;
}

//==============================================================================
/**
 * @brief Write data to device. Data are mixed into generator state.
 *
 * @param[in ]          *device_handle          device allocated memory
 * @param[in ]          *src                    data source
 * @param[in ]           count                  number of bytes to write
 * @param[in ][out]     *fpos                   file position
 * @param[out]          *wrcnt                  number of written bytes
 * @param[in ]           fattr                  file attributes
 *
 * @return One of errno value (errno.h).
 */
//==============================================================================
API_MOD_WRITE(URANDOM, void *device_handle, const u8_t *src, size_t count, fpos_t *fpos, size_t *wrcnt,
        struct vfs_fattr fattr)
{
        UNUSED_ARG2(fpos, fattr);

        URANDOM_t *hdl = device_handle;

        int err = sys_mutex_lock(hdl->mtx, _WAIT_FOREVER_MS);
        if (!err) {
                mix(hdl, src, count);
                *wrcnt = count;
                sys_mutex_unlock(hdl->mtx);
        }

        return err;
}

//==============================================================================
/**
 * @brief Read data from device.
 *
 * @param[in ]          *device_handle          device allocated memory
 * @param[out]          *dst                    data destination
 * @param[in ]           count                  number of bytes to read
 * @param[in ][out]     *fpos                   file position
 * @param[out]          *rdcnt                  number of read bytes
 * @param[in ]           fattr                  file attributes
 *
 * @return One of errno value (errno.h).
 */
//==============================================================================
API_MOD_READ(URANDOM, void *device_handle, u8_t *dst, size_t count, fpos_t *fpos, size_t *rdcnt, struct vfs_fattr fattr)
{
        UNUSED_ARG2(fpos, fattr);

        URANDOM_t *hdl = device_handle;

        int err = sys_mutex_lock(hdl->mtx, _WAIT_FOREVER_MS);
        if (!err) {
                reseed(hdl);

                u32_t block[BLOCK_WORDS];
                size_t n = count;

                while (n) {
                        chacha20_block(hdl, block);
                        size_t len = min(n, sizeof(block));
                        memcpy(dst, block, len);
                        dst += len;
                        n -= len;
                }

                wipe(block, sizeof(block));
                rekey(hdl);
                *rdcnt = count;

                sys_mutex_unlock(hdl->mtx);
        }

        return err;
}

//==============================================================================
/**
 * @brief IO control.
 *
 * @param[in ]          *device_handle          device allocated memory
 * @param[in ]           request                request
 * @param[in ][out]     *arg                    request's argument
 *
 * @return One of errno value (errno.h).
 */
//==============================================================================
API_MOD_IOCTL(URANDOM, void *device_handle, int request, void *arg)
{
        URANDOM_t *hdl = device_handle;

        int err = EINVAL;

        switch (request) {
        case IOCTL_URANDOM__GET_SOURCE:
                if (arg) {
                        *cast(URANDOM_source_t *, arg) = hdl->source;
                        err = ESUCC;
                }
                break;

        default:
                err = EBADRQC;
                break;
        }

        return err;
}

//==============================================================================
/**
 * @brief Flush device.
 *
 * @param[in ]          *device_handle          device allocated memory
 *
 * @return One of errno value (errno.h).
 */
//==============================================================================
API_MOD_FLUSH(URANDOM, void *device_handle)
{
        UNUSED_ARG1(device_handle);
        return ESUCC;
}

//==============================================================================
/**
 * @brief Device information.
 *
 * @param[in ]          *device_handle          device allocated memory
 * @param[out]          *device_stat            device status
 *
 * @return One of errno value (errno.h).
 */
//==============================================================================
API_MOD_STAT(URANDOM, void *device_handle, struct vfs_dev_stat *device_stat)
{
        UNUSED_ARG1(device_handle);

        device_stat->st_size = 0;

        return ESUCC;
}

//==============================================================================
/**
 * @brief  ChaCha20 block (RFC 8439) of generator key and next block counter.
 *
 * @param  hdl          generator
 * @param  out          output block
 */
//==============================================================================
static void chacha20_block(URANDOM_t *hdl, u32_t out[BLOCK_WORDS])
{
        u32_t x[BLOCK_WORDS] = {0x61707865, 0x3320646E, 0x79622D32, 0x6B206574};

        memcpy(&x[4], hdl->key, sizeof(hdl->key));
        x[12] = cast(u32_t, hdl->counter);
        x[13] = cast(u32_t, hdl->counter >> 32);
        x[14] = 0;
        x[15] = 0;
        hdl->counter++;

        memcpy(out, x, sizeof(x));

        for (int i = 0; i < 10; i++) {
                QR(x[0], x[4], x[8], x[12]);
                QR(x[1], x[5], x[9], x[13]);
                QR(x[2], x[6], x[10], x[14]);
                QR(x[3], x[7], x[11], x[15]);
                QR(x[0], x[5], x[10], x[15]);
                QR(x[1], x[6], x[11], x[12]);
                QR(x[2], x[7], x[8], x[13]);
                QR(x[3], x[4], x[9], x[14]);
        }

        for (int i = 0; i < BLOCK_WORDS; i++) {
                out[i] += x[i];
        }

        wipe(x, sizeof(x));
}

//==============================================================================
/**
 * @brief  Replace key by generator output (fast key erasure).
 *
 * @param  hdl          generator
 */
//==============================================================================
static void rekey(URANDOM_t *hdl)
{
        u32_t block[BLOCK_WORDS];
        chacha20_block(hdl, block);
        memcpy(hdl->key, block, sizeof(hdl->key));
        wipe(block, sizeof(block));
}

//==============================================================================
/**
 * @brief  Mix data into key (each 32 bytes are XORed with key and key is
 *         replaced by generator output).
 *
 * @param  hdl          generator
 * @param  data         data
 * @param  len          data length
 */
//==============================================================================
static void mix(URANDOM_t *hdl, const u8_t *data, size_t len)
{
        u8_t *key = cast(u8_t *, hdl->key);

        while (len) {
                size_t n = min(len, sizeof(hdl->key));

                for (size_t i = 0; i < n; i++) {
                        key[i] ^= data[i];
                }

                data += n;
                len -= n;
                rekey(hdl);
        }
}

//==============================================================================
/**
 * @brief  Mix entropy of source and uptime into key.
 *
 * @param  hdl          generator
 */
//==============================================================================
static void reseed(URANDOM_t *hdl)
{
        u8_t entropy[RESEED_BYTES];

        u32_t uptime = sys_get_uptime_ms();
        mix(hdl, cast(u8_t *, &uptime), sizeof(uptime));

        if (_URANDOM_LLD__read(entropy, sizeof(entropy)) == ESUCC) {
                mix(hdl, entropy, sizeof(entropy));
        }

        wipe(entropy, sizeof(entropy));
}

//==============================================================================
/**
 * @brief  Clear memory (not removed by compiler optimization).
 *
 * @param  mem          memory
 * @param  len          length
 */
//==============================================================================
static void wipe(void *mem, size_t len)
{
        volatile u8_t *p = mem;

        while (len--) {
                *p++ = 0;
        }
}

/*==============================================================================
  End of file
==============================================================================*/
