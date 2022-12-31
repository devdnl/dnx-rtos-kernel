/*==============================================================================
File    nvm.c

Author  Daniel Zorychta

Brief   NVM driver

        Copyright (C) 2022 Daniel Zorychta <daniel.zorychta@gmail.com>

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
#include "../nvm_ioctl.h"
#include "drivers/driver.h"
#include "stm32h7/stm32h7xx.h"

/*==============================================================================
  Local macros
==============================================================================*/
#define TIMEOUT_MS     15000
#define RDP_KEY        0x00A5
#define FLASH_KEY1     0x45670123
#define FLASH_KEY2     0xCDEF89AB
#define FLASH_OPT_KEY1 0x08192A3B
#define FLASH_OPT_KEY2 0x4C5D6E7F

#define FLASH_CR_PSIZE_BYTE     ((0 * FLASH_CR_PSIZE_1) | (0 * FLASH_CR_PSIZE_0))
#define FLASH_CR_PSIZE_HALFWORD ((0 * FLASH_CR_PSIZE_1) | (1 * FLASH_CR_PSIZE_0))
#define FLASH_CR_PSIZE_WORD     ((1 * FLASH_CR_PSIZE_1) | (0 * FLASH_CR_PSIZE_0))
#define FLASH_CR_PSIZE_DOUBLE   ((1 * FLASH_CR_PSIZE_1) | (1 * FLASH_CR_PSIZE_0))

#define FLASH_WORD_BYTES  (FLASH_NB_32BITWORD_IN_FLASHWORD * sizeof(u32_t))
#define SECTOR_ADDRESS(n) (0x08000000UL + ((n)*FLASH_SECTOR_SIZE))

/*==============================================================================
  Local object types
==============================================================================*/
typedef struct {
        u8_t     major;
        u8_t     minor;
        kmtx_t  *mtx;
        uint32_t start_sector;
        uint32_t sector_count;
        uint32_t total_size;
        uint8_t  tmp_buf[FLASH_WORD_BYTES];
} NVM_t;

typedef struct {
        uint32_t addr;
        uint32_t size;
        uint8_t  bank;
        uint8_t  sector;
} sector_info_t;

/*==============================================================================
  Local function prototypes
==============================================================================*/
static void FLASH_unlock(NVM_t *hdl, u8_t bank);
static void FLASH_lock(NVM_t *hdl, u8_t bank);
static int  FLASH_erase_sector(NVM_t *hdl, u8_t bank, uint32_t sector);
static int  FLASH_wait_for_operation_finish(NVM_t *hdl, u8_t bank);
static int  FLASH_program_flash_word(NVM_t *hdl, u8_t bank, u32_t address, u32_t *data);
static int  configure(NVM_t *hdl, const NVM_config_t *conf);
static int  get_sector_info_by_addr(NVM_t *hdl, u32_t logic_addr, u8_t *sector_num,
         size_t *offset, sector_info_t *sector_info);

/*==============================================================================
  Local object
==============================================================================*/
MODULE_NAME(NVM);

static const sector_info_t SECTOR_INFO[] = {
#ifdef DUAL_BANK
        {.addr = SECTOR_ADDRESS(0), .size = FLASH_SECTOR_SIZE, .bank = 1, .sector = 0},
        {.addr = SECTOR_ADDRESS(1), .size = FLASH_SECTOR_SIZE, .bank = 1, .sector = 1},
        {.addr = SECTOR_ADDRESS(2), .size = FLASH_SECTOR_SIZE, .bank = 1, .sector = 2},
        {.addr = SECTOR_ADDRESS(3), .size = FLASH_SECTOR_SIZE, .bank = 1, .sector = 3},
#if FLASH_BANK_SIZE == 0x100000UL
        {.addr = SECTOR_ADDRESS(4), .size = FLASH_SECTOR_SIZE, .bank = 1, .sector = 4},
        {.addr = SECTOR_ADDRESS(5), .size = FLASH_SECTOR_SIZE, .bank = 1, .sector = 5},
        {.addr = SECTOR_ADDRESS(6), .size = FLASH_SECTOR_SIZE, .bank = 1, .sector = 6},
        {.addr = SECTOR_ADDRESS(7), .size = FLASH_SECTOR_SIZE, .bank = 1, .sector = 7},
#endif
        {.addr = SECTOR_ADDRESS(8), .size = FLASH_SECTOR_SIZE, .bank = 2, .sector = 0},
        {.addr = SECTOR_ADDRESS(9), .size = FLASH_SECTOR_SIZE, .bank = 2, .sector = 1},
        {.addr = SECTOR_ADDRESS(10), .size = FLASH_SECTOR_SIZE, .bank = 2, .sector = 2},
        {.addr = SECTOR_ADDRESS(11), .size = FLASH_SECTOR_SIZE, .bank = 2, .sector = 3},
#if FLASH_BANK_SIZE == 0x100000UL
        {.addr = SECTOR_ADDRESS(12), .size = FLASH_SECTOR_SIZE, .bank = 2, .sector = 4},
        {.addr = SECTOR_ADDRESS(13), .size = FLASH_SECTOR_SIZE, .bank = 2, .sector = 5},
        {.addr = SECTOR_ADDRESS(14), .size = FLASH_SECTOR_SIZE, .bank = 2, .sector = 6},
        {.addr = SECTOR_ADDRESS(15), .size = FLASH_SECTOR_SIZE, .bank = 2, .sector = 7},
#endif
#else
        {.addr = SECTOR_ADDRESS(0), .size = FLASH_SECTOR_SIZE, .bank = 1, .sector = 0},
#if FLASH_BANK_SIZE >= 0x80000UL
        {.addr = SECTOR_ADDRESS(1), .size = FLASH_SECTOR_SIZE, .bank = 1, .sector = 1},
        {.addr = SECTOR_ADDRESS(2), .size = FLASH_SECTOR_SIZE, .bank = 1, .sector = 2},
        {.addr = SECTOR_ADDRESS(3), .size = FLASH_SECTOR_SIZE, .bank = 1, .sector = 3},
#if FLASH_BANK_SIZE == 0x100000UL
        {.addr = SECTOR_ADDRESS(4), .size = FLASH_SECTOR_SIZE, .bank = 1, .sector = 4},
        {.addr = SECTOR_ADDRESS(5), .size = FLASH_SECTOR_SIZE, .bank = 1, .sector = 5},
        {.addr = SECTOR_ADDRESS(6), .size = FLASH_SECTOR_SIZE, .bank = 1, .sector = 6},
        {.addr = SECTOR_ADDRESS(7), .size = FLASH_SECTOR_SIZE, .bank = 1, .sector = 7},
#endif
#endif
#endif
};

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
 * @param[in ]            config               optional module configuration
 *
 * @return One of errno value (errno.h).
 */
//==============================================================================
API_MOD_INIT(NVM, void **device_handle, u8_t major, u8_t minor, const void *config)
{
        UNUSED_ARG1(major);

        int err = EFAULT;

        if (minor == 0) {
                err = sys_zalloc(sizeof(NVM_t), device_handle);
                if (!err) {
                        NVM_t *hdl = *device_handle;

                        err = sys_mutex_create(KMTX_TYPE_NORMAL, &hdl->mtx);

                        if (!err && config) {
                                err = configure(hdl, config);
                        }

                        if (err) {
                                if (hdl->mtx) {
                                        sys_mutex_destroy(hdl->mtx);
                                }

                                sys_free(device_handle);
                        }
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
API_MOD_RELEASE(NVM, void *device_handle)
{
        NVM_t *hdl = device_handle;

        int err = sys_mutex_lock(hdl->mtx, TIMEOUT_MS);
        if (!err) {
                kmtx_t *mtx = hdl->mtx;

                hdl->mtx = NULL;

                sys_free(&device_handle);

                sys_sleep_ms(TIMEOUT_MS + 100);

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
 * @param[in ]           flags                  file operation flags (O_RDONLY, O_WRONLY,
 * O_RDWR)
 *
 * @return One of errno value (errno.h).
 */
//==============================================================================
API_MOD_OPEN(NVM, void *device_handle, u32_t flags)
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
API_MOD_CLOSE(NVM, void *device_handle, bool force)
{
        UNUSED_ARG2(device_handle, force);

        return ESUCC;
}

//==============================================================================
/**
 * @brief Write data to device.
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
API_MOD_WRITE(NVM, void *device_handle, const u8_t *src, size_t count, fpos_t *fpos,
        size_t *wrcnt, struct vfs_fattr fattr)
{
        UNUSED_ARG1(fattr);

        NVM_t *hdl = device_handle;

        if (hdl->sector_count == 0) {
                dev_dbg(hdl, "write to not configured device");
                return EINVAL;
        }

        if (*fpos >= hdl->total_size) {
                dev_dbg(hdl, "write pointer out of range");
                return ENOSPC;
        }

        if (*fpos % FLASH_WORD_BYTES != 0) {
                dev_dbg(hdl, "write pointer shall be aligned to Flash Word (%u bytes)",
                        FLASH_WORD_BYTES);
                return EINVAL;
        }

        *wrcnt = 0;

        int err = sys_mutex_lock(hdl->mtx, TIMEOUT_MS);
        if (err) {
                dev_dbg(hdl, "write mutex timeout");
                return err;
        }

        count = min(count, (hdl->total_size - *fpos));

        size_t pos = *fpos;

        while (not err and count) {
                sector_info_t sector_info;
                u8_t          sector_num;
                size_t        offset;

                err = get_sector_info_by_addr(hdl, pos, &sector_num, &offset,
                        &sector_info);

                if (!err) {
                        uint32_t dst = sector_info.addr + offset;

                        if (count < FLASH_WORD_BYTES) {
                                dev_dbg(hdl, "Unaligned buffer size, fitting by FFh.");
                                memset(hdl->tmp_buf, 0xFF, sizeof(hdl->tmp_buf));
                                memcpy(hdl->tmp_buf, src, count);
                                src = hdl->tmp_buf;
                        }

                        FLASH_unlock(hdl, sector_info.bank);

                        err = FLASH_program_flash_word(hdl, sector_info.bank, dst,
                                cast(u32_t *, src));

                        if (not err) {
                                size_t bytes = min(count, FLASH_WORD_BYTES);

                                count -= bytes;
                                src += bytes;
                                dst += bytes;
                                *wrcnt += bytes;
                                pos += bytes;
                        }

                        FLASH_lock(hdl, sector_info.bank);
                }
        }

        sys_mutex_unlock(hdl->mtx);

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
API_MOD_READ(NVM, void *device_handle, u8_t *dst, size_t count, fpos_t *fpos,
        size_t *rdcnt, struct vfs_fattr fattr)
{
        UNUSED_ARG1(fattr);

        NVM_t *hdl = device_handle;

        if (hdl->sector_count == 0) {
                dev_dbg(hdl, "read from not configured device");
                return EINVAL;
        }

        if (*fpos >= hdl->total_size) {
                dev_dbg(hdl, "read pointer out of range");
                return ENOSPC;
        }

        *rdcnt = 0;

        int err = sys_mutex_lock(hdl->mtx, TIMEOUT_MS);
        if (err) {
                dev_dbg(hdl, "read mutex timeout");
                return err;
        }

        size_t pos = *fpos;

        while (not err and count) {
                sector_info_t sector_info;
                u8_t          sector_num;
                size_t        offset;

                err = get_sector_info_by_addr(hdl, pos, &sector_num, &offset,
                        &sector_info);

                if (!err) {
                        uint32_t src   = sector_info.addr + offset;
                        size_t   bytes = min(count, sector_info.size - offset);

                        memcpy(dst, cast(uint8_t *, src), bytes);

                        dst += bytes;
                        count -= bytes;
                        pos += bytes;
                        *rdcnt += bytes;
                }
        }

        sys_mutex_unlock(hdl->mtx);

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
API_MOD_IOCTL(NVM, void *device_handle, int request, void *arg)
{
        NVM_t *hdl = device_handle;

        int err = EINVAL;

        switch (request) {
        case IOCTL_NVM__CONFIGURE:
                if (arg) {
                        err = configure(hdl, arg);
                }
                break;

        case IOCTL_NVM__SECTOR_ERASE:
                if (not arg) {
                        break;
                }

                err = sys_mutex_lock(hdl->mtx, TIMEOUT_MS);
                if (err) {
                        break;
                }

                size_t sector = *cast(size_t *, arg) + hdl->start_sector;
                u8_t   bank   = SECTOR_INFO[sector].bank;

                if (sector < ARRAY_SIZE(SECTOR_INFO)) {
                        FLASH_unlock(hdl, bank);
                        err = FLASH_erase_sector(hdl, bank, SECTOR_INFO[sector].sector);
                        FLASH_lock(hdl, bank);

                } else {
                        err = ESPIPE;
                }

                sys_mutex_unlock(hdl->mtx);
                break;

        case IOCTL_NVM__MASS_ERASE:
                err = sys_mutex_lock(hdl->mtx, TIMEOUT_MS);
                if (err) {
                        break;
                }

                for (size_t i = hdl->start_sector;
                        i < (hdl->start_sector + hdl->sector_count); i++) {

                        size_t sector = i + hdl->start_sector;
                        u8_t   bank   = SECTOR_INFO[sector].bank;

                        FLASH_unlock(hdl, bank);
                        err = FLASH_erase_sector(hdl, bank, SECTOR_INFO[sector].sector);
                        FLASH_lock(hdl, bank);

                        if (err) {
                                break;
                        }
                }

                sys_mutex_unlock(hdl->mtx);
                break;

        case IOCTL_NVM__GET_SECTOR_SIZE:
                if (arg) {
                        NVM_sector_size_t *secsz = arg;

                        size_t sector = secsz->sector + hdl->start_sector;

                        if (sector < (hdl->start_sector + hdl->sector_count)) {
                                secsz->size = SECTOR_INFO[sector].size;
                                err         = ESUCC;
                        }
                }
                break;

        case IOCTL_NVM__GET_SECTOR_COUNT:
                if (arg) {
                        *cast(size_t *, arg) = hdl->sector_count;
                        err                  = ESUCC;
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
API_MOD_FLUSH(NVM, void *device_handle)
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
API_MOD_STAT(NVM, void *device_handle, struct vfs_dev_stat *device_stat)
{
        NVM_t *hdl = device_handle;

        device_stat->st_size = hdl->total_size;

        return ESUCC;
}

//==============================================================================
/**
 * @brief  Function configure NVM.
 *
 * @param  hdl          driver handle
 * @param  config       driver configuration
 *
 * @return One of errno value (errno.h).
 */
//==============================================================================
static int configure(NVM_t *hdl, const NVM_config_t *conf)
{
        if ((conf->number_of_sectors == 0)
                || (conf->start_sector >= ARRAY_SIZE(SECTOR_INFO))
                || ((conf->start_sector + conf->number_of_sectors - 1)
                        >= ARRAY_SIZE(SECTOR_INFO))) {

                return EINVAL;
        }

        int err = sys_mutex_lock(hdl->mtx, TIMEOUT_MS);
        if (!err) {
                hdl->sector_count = conf->number_of_sectors;
                hdl->start_sector = conf->start_sector;

                hdl->total_size = 0;
                size_t sector   = hdl->start_sector;
                size_t count    = hdl->sector_count;

                while (count--) {
                        hdl->total_size += SECTOR_INFO[sector].size;
                        sector++;
                }

                sys_mutex_unlock(hdl->mtx);
        }

        return err;
}

//==============================================================================
/**
 * @brief  Return sector information by given address.
 *
 * @param  logic_addr   logical sector address
 * @param  sector_num   sector number
 * @param  offset       offset in sector of logical address
 * @param  sector_info  sector information
 *
 * @return One of errno value (errno.h).
 */
//==============================================================================
static int get_sector_info_by_addr(NVM_t *hdl, u32_t logic_addr, u8_t *sector_num,
        size_t *offset, sector_info_t *sector_info)
{
        u8_t sector = logic_addr / FLASH_SECTOR_SIZE + hdl->start_sector;

        if (sector < ARRAY_SIZE(SECTOR_INFO)) {
                *sector_num  = sector;
                *sector_info = SECTOR_INFO[sector];
                *offset      = logic_addr % FLASH_SECTOR_SIZE;
                return 0;
        } else {
                return ENODEV;
        }
}

//==============================================================================
/**
 * @brief  Unlock for erase/program flash memory.
 *
 * @param bank          flash bank
 */
//==============================================================================
static void FLASH_unlock(NVM_t *hdl, u8_t bank)
{
        UNUSED_ARG1(hdl);

        sys_critical_section_begin();

        volatile u32_t *CR   = (bank == 1) ? &FLASH->CR1 : &FLASH->CR2;
        volatile u32_t *KEYR = (bank == 1) ? &FLASH->KEYR1 : &FLASH->KEYR2;

        if (*CR & FLASH_CR_LOCK) {
                *KEYR = FLASH_KEY1;
                *KEYR = FLASH_KEY2;
        }

        sys_critical_section_end();
}

//==============================================================================
/**
 * @brief  Lock for erase/program flash memory.
 *
 * @param bank          flash bank
 */
//==============================================================================
static void FLASH_lock(NVM_t *hdl, u8_t bank)
{
        UNUSED_ARG1(hdl);

        volatile u32_t *CR = (bank == 1) ? &FLASH->CR1 : &FLASH->CR2;

        *CR |= FLASH_CR_LOCK;
}

//==============================================================================
/**
 * @brief  Erase selected sector.
 *
 * @param  bank         bank
 * @param  sector       sector to erase
 *
 * @return One of errno value.
 */
//==============================================================================
static int FLASH_erase_sector(NVM_t *hdl, u8_t bank, uint32_t sector)
{
        int err = FLASH_wait_for_operation_finish(hdl, bank);

        if (!err) {
                volatile u32_t *CR = (bank == 1) ? &FLASH->CR1 : &FLASH->CR2;

                *CR &= ~(FLASH_CR_PSIZE | FLASH_CR_SNB);
                *CR |= FLASH_CR_PSIZE_WORD;
                *CR |= FLASH_CR_SER | ((sector << FLASH_CR_SNB_Pos) & FLASH_CR_SNB_Msk)
                        | FLASH_CR_START;

                err = FLASH_wait_for_operation_finish(hdl, bank);

                *CR &= ~(FLASH_CR_SER | FLASH_CR_SNB);
        }

        return err;
}

//==============================================================================
/**
 * @brief  Program flash word (128 or 256-bit word) at selected address.
 *
 * @param  bank         flash bank
 * @param  address      destination address
 * @param  data         data to program
 *
 * @return One of errno value.
 */
//==============================================================================
static int FLASH_program_flash_word(NVM_t *hdl, u8_t bank, u32_t address, u32_t *data)
{
        int err = FLASH_wait_for_operation_finish(hdl, bank);

        sys_ISR_disable();

        if (!err) {
                /*
                 * Programming phase.
                 */
                volatile u32_t *CR = (bank == 1) ? &FLASH->CR1 : &FLASH->CR2;

                CLEAR_BIT(*CR, FLASH_CR_PSIZE);
                SET_BIT(*CR, FLASH_CR_PG);

                __ISB();
                __DSB();

                size_t             row_index = FLASH_NB_32BITWORD_IN_FLASHWORD;
                volatile uint32_t *dest_addr = cast(volatile uint32_t *, address);
                uint32_t          *src_addr  = data;

                while (row_index--) {
                        *dest_addr++ = *src_addr++;
                }

                __ISB();
                __DSB();

                err = FLASH_wait_for_operation_finish(hdl, bank);

                CLEAR_BIT(*CR, FLASH_CR_PG);

                /*
                 * Verification phase.
                 */
                if (!err) {
                        row_index = FLASH_NB_32BITWORD_IN_FLASHWORD;
                        dest_addr = cast(volatile uint32_t *, address);
                        src_addr  = data;

                        while (row_index--) {
                                if (*dest_addr++ != *src_addr++) {
                                        dev_dbg(hdl, "verification fail @ %p",
                                                src_addr - 1);
                                        err = EILSEQ;
                                        break;
                                }
                        }
                }
        }

        sys_ISR_enable();

        return err;
}

//==============================================================================
/**
 * @brief  Waits for operation finish.
 *
 * @param bank          flash bank
 *
 * @return  One of errno value.
 */
//==============================================================================
static int FLASH_wait_for_operation_finish(NVM_t *hdl, u8_t bank)
{
        int err = ESUCC;

        clock_t tref = sys_get_uptime_ms();

        volatile u32_t *SR  = (bank == 1) ? &FLASH->SR1 : &FLASH->SR2;
        volatile u32_t *CCR = (bank == 1) ? &FLASH->CCR1 : &FLASH->CCR2;

        while (*SR & FLASH_SR_QW) {

                if (sys_is_time_expired(tref, TIMEOUT_MS)) {
                        err = ETIME;
                }
        }

        u32_t status = *SR
                & (FLASH_SR_WRPERR | FLASH_SR_STRBERR | FLASH_SR_OPERR | FLASH_SR_RDSERR
                        | FLASH_SR_DBECCERR);

        if (status & FLASH_SR_PGSERR) {
                dev_dbg(hdl, "programming sequence error");
                err = EIO;

        } else if (status & FLASH_SR_STRBERR) {
                dev_dbg(hdl, "strobe error");
                err = EIO;

        } else if (status & FLASH_SR_INCERR) {
                dev_dbg(hdl, "inconsistency error");
                err = EIO;

        } else if (status & FLASH_SR_OPERR) {
                dev_dbg(hdl, "write/erase error");
                err = EIO;

        } else if (status & FLASH_SR_RDPERR) {
                dev_dbg(hdl, "read protection error");
                err = EIO;

        } else if (status & FLASH_SR_RDSERR) {
                dev_dbg(hdl, "secure error");
                err = EIO;
        }

        if (status != 0) {
                WRITE_REG(*CCR, status);
        }

        return err;
}

/*==============================================================================
  End of file
==============================================================================*/
