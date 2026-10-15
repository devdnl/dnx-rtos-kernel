/*==============================================================================
File    sdmmc.c

Author  Daniel Zorychta

Brief   SD/MMC Card Driver.

        Copyright (C) 2023 Daniel Zorychta <daniel.zorychta@gmail.com>

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
#include "sys/types.h"
#include "../sdmmc_ioctl.h"
#include "drivers/driver.h"
#include "stm32f7/dma_ddi.h"
#include "stm32f7xx_hal_mmc.h"
#include "stm32f7xx_hal_sd.h"

/*==============================================================================
  Local macros
==============================================================================*/
#define MTX_TIMEOUT 25000

#define SDMMC_BLOCKSIZE SD_BLOCKSIZE

#define _SDMMC_MODE_POLLING 0
#define _SDMMC_MODE_IRQ     1
#define _SDMMC_MODE_DMA     2
#define _SDMMC_TYPE_SD      (1 << 0)
#define _SDMMC_TYPE_MMC     (1 << 1)
#define _SDMMC_TYPE_BOTH    (_SDMMC_TYPE_SD | _SDMMC_TYPE_MMC)

#define SD_ENABLED ((__SDMMC_CFG_SDMMC1_TYPE__ & _SDMMC_TYPE_SD) || (__SDMMC_CFG_SDMMC2_TYPE__ & _SDMMC_TYPE_SD))

#define MMC_ENABLED ((__SDMMC_CFG_SDMMC1_TYPE__ & _SDMMC_TYPE_MMC) || (__SDMMC_CFG_SDMMC2_TYPE__ & _SDMMC_TYPE_MMC))

/*==============================================================================
  Local object types
==============================================================================*/
enum {
#if defined(SDMMC1)
        SDMMC_1,
#endif
#if defined(SDMMC2)
        SDMMC_2,
#endif
        SDMMC_COUNT
};

/** driver instance associated with partition */
typedef struct {
        u32_t mode;
        u8_t major;
        u8_t minor;
        u64_t card_size;
        kmtx_t *mtx;
        ksem_t *sem_xfer_complete;
        bool irq_yield;
        SDMMC_config_t config;

        union {
#if SD_ENABLED
                SD_HandleTypeDef sd;
#endif
#if MMC_ENABLED
                MMC_HandleTypeDef mmc;
#endif
        } handle;
} SDMMC_t;

typedef struct {
        SDMMC_TypeDef *instance;
        __IO u32_t *RCC_APBENR;
        u32_t RCC_APBENR_SDMMCEN;
        IRQn_Type IRQn;
        u32_t IRQ_priority;
        u32_t mode;
        u8_t DMA_channel;
        u8_t DMA_major;
        u8_t DMA_stream_pri;
        u8_t DMA_stream_alt;
} sdmmc_info_t;

/*==============================================================================
  Local function prototypes
==============================================================================*/
static int card_initialize(SDMMC_t *hdl);
static int card_wait_ready(SDMMC_t *hdl);
static int card_check_sdmmc_error(SDMMC_t *hdl);
static int card_write_blocks(SDMMC_t *hdl, const u8_t *src, uint32_t block_address, uint32_t blocks);
static int card_write_blocks_IT(SDMMC_t *hdl, const u8_t *src, uint32_t block_address, uint32_t blocks);
static int card_write_blocks_DMA(SDMMC_t *hdl, const u8_t *src, uint32_t block_address, uint32_t blocks);
static int card_read_blocks(SDMMC_t *hdl, u8_t *dst, uint32_t block_address, uint32_t blocks);
static int card_read_blocks_IT(SDMMC_t *hdl, u8_t *dst, uint32_t block_address, uint32_t blocks);
static int card_read_blocks_DMA(SDMMC_t *hdl, u8_t *dst, uint32_t block_address, uint32_t blocks);

/*==============================================================================
  Local object
==============================================================================*/
MODULE_NAME(SDMMC);

/*
 * For IRQ purpose only.
 */
static SDMMC_t *sdmmc[SDMMC_COUNT];

/*
 * SDMMCx configuration.
 */
static const sdmmc_info_t SDMMC[] = {
#if defined(SDMMC1)
        {
                .instance = SDMMC1,
                .RCC_APBENR = &RCC->APB2ENR,
                .RCC_APBENR_SDMMCEN = RCC_APB2ENR_SDMMC1EN,
                .IRQn = SDMMC1_IRQn,
                .IRQ_priority = __SDMMC_CFG_SDMMC1_IRQ_PRIORITY__,
                .mode = __SDMMC_CFG_SDMMC1_MODE__,
                .DMA_channel = 4,
                .DMA_major = 1,
                .DMA_stream_pri = 3,
                .DMA_stream_alt = 6,
        },
#endif
#if defined(SDMMC2)
        {
                .instance = SDMMC2,
                .RCC_APBENR = &RCC->APB2ENR,
                .RCC_APBENR_SDMMCEN = RCC_APB2ENR_SDMMC2EN,
                .IRQn = SDMMC2_IRQn,
                .IRQ_priority = __SDMMC_CFG_SDMMC2_IRQ_PRIORITY__,
                .mode = __SDMMC_CFG_SDMMC2_MODE__,
                .DMA_channel = 11,
                .DMA_major = 1,
                .DMA_stream_pri = 0,
                .DMA_stream_alt = 5,
        }
#endif
};

static const u32_t CLOCK_EDGE[] = {
        [SDMMC_CLOCK_EDGE__RISING] = SDMMC_CLOCK_EDGE_RISING,
        [SDMMC_CLOCK_EDGE__FALLING] = SDMMC_CLOCK_EDGE_FALLING,
};

static const u32_t BUS_WIDE[] = {
        [SDMMC_BUS_WIDE__1BIT] = SDMMC_BUS_WIDE_1B,
        [SDMMC_BUS_WIDE__4BIT] = SDMMC_BUS_WIDE_4B,
        [SDMMC_BUS_WIDE__8BIT] = SDMMC_BUS_WIDE_8B,
};

static const u32_t BUS_WIDE_BITS[] = {
        [SDMMC_BUS_WIDE__1BIT] = 1,
        [SDMMC_BUS_WIDE__4BIT] = 4,
        [SDMMC_BUS_WIDE__8BIT] = 8,
};

static const u32_t CLOCK_POWER_SAVE[] = {
        [false] = SDMMC_CLOCK_POWER_SAVE_DISABLE,
        [true] = SDMMC_CLOCK_POWER_SAVE_ENABLE,
};

static const u32_t HARDWARE_FLOW_CONTROL[] = {
        [false] = SDMMC_HARDWARE_FLOW_CONTROL_DISABLE,
        [true] = SDMMC_HARDWARE_FLOW_CONTROL_ENABLE,
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
API_MOD_INIT(SDMMC, void **device_handle, u8_t major, u8_t minor, const void *config)
{
        int err = ENODEV;

        if (not config) {
                err = EINVAL;

        } else if ((major >= SDMMC_COUNT) or (minor != 0)) {
                err = ENODEV;

        } else {
                err = sys_zalloc(sizeof(SDMMC_t), device_handle);
                if (!err) {
                        SDMMC_t *hdl = *device_handle;
                        hdl->major = major;
                        hdl->minor = minor;
                        hdl->config = *cast(typeof(hdl->config) *, config);

                        err = sys_semaphore_create(1, 0, &hdl->sem_xfer_complete);
                        if (!err) {

                                err = sys_mutex_create(KMTX_TYPE_RECURSIVE, &hdl->mtx);
                                if (!err) {
                                        err = card_initialize(hdl);
                                        if (!err) {
                                                sdmmc[hdl->major] = hdl;
                                                return err;
                                        }

                                        sys_mutex_destroy(hdl->mtx);
                                }

                                sys_semaphore_destroy(hdl->sem_xfer_complete);
                        }

                        sys_free(*device_handle);
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
API_MOD_RELEASE(SDMMC, void *device_handle)
{
        SDMMC_t *hdl = device_handle;
        kmtx_t *mtx = hdl->mtx;

        int err = sys_mutex_lock(mtx, MTX_TIMEOUT);
        if (!err) {
                hdl->mtx = NULL;
                sys_sleep_ms(MTX_TIMEOUT + 500);

                if (hdl->config.device_type == SDMMC_DEVICE_TYPE__SD) {
#if SD_ENABLED
                        HAL_SD_DeInit(&hdl->handle.sd);
#endif
                } else if (hdl->config.device_type == SDMMC_DEVICE_TYPE__MMC) {
#if MMC_ENABLED
                        HAL_MMC_DeInit(&hdl->handle.mmc);
#endif
                }

                sys_mutex_unlock(mtx);
                sys_mutex_destroy(hdl->mtx);
                sys_free(device_handle);
                sdmmc[hdl->major] = NULL;
        }

        return err;
}

//==============================================================================
/**
 * @brief Open device.
 *
 * @param[in ] device_handle     device allocated memory
 * @param[in ] flags             file operation flags (O_RDONLY, O_WRONLY, O_RDWR)
 *
 * @return One of errno value (errno.h).
 */
//==============================================================================
API_MOD_OPEN(SDMMC, void *device_handle, u32_t flags)
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
API_MOD_CLOSE(SDMMC, void *device_handle, bool force)
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
API_MOD_WRITE(SDMMC, void *device_handle, const u8_t *src, size_t count, fpos_t *fpos, size_t *wrcnt,
        struct vfs_fattr fattr)
{
        UNUSED_ARG1(fattr);

        SDMMC_t *hdl = device_handle;

        if ((*fpos & (SDMMC_BLOCKSIZE - 1)) != 0) {
                return EINVAL;
        }

        if ((count & (SDMMC_BLOCKSIZE - 1)) != 0) {
                return EINVAL;
        }

        int err = sys_mutex_lock(hdl->mtx, MTX_TIMEOUT);
        if (err) {
                return err;
        }

        u32_t mode = hdl->mode;

        u32_t block_address = *fpos / SDMMC_BLOCKSIZE;
        u32_t blocks = count / SDMMC_BLOCKSIZE;

        for (int i = 0; i < 5; i++) {

                err = card_wait_ready(hdl);
                if (err) {
                        dev_dbg(hdl, "card busy at write transfer");
                }

                if (mode == _SDMMC_MODE_POLLING) {

                        err = card_write_blocks(hdl, src, block_address, blocks);

                } else if (mode == _SDMMC_MODE_IRQ) {

                        err = card_write_blocks_IT(hdl, src, block_address, blocks);

                } else if (mode == _SDMMC_MODE_DMA) {

                        err = card_write_blocks_DMA(hdl, src, block_address, blocks);

                } else {
                        err = EFAULT;
                        dev_dbg(hdl, "unknown card mode!");
                }

                if (!err) {
                        *wrcnt += count;
                        break;

                } else {
                        dev_dbg(hdl, "write error %d at block %u", err, block_address);

                        if (i >= 3) {
                                card_initialize(hdl);
                        }
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
API_MOD_READ(SDMMC, void *device_handle, u8_t *dst, size_t count, fpos_t *fpos, size_t *rdcnt, struct vfs_fattr fattr)
{
        UNUSED_ARG1(fattr);

        SDMMC_t *hdl = device_handle;

        if ((*fpos & (SDMMC_BLOCKSIZE - 1)) != 0) {
                return EINVAL;
        }

        if ((count & (SDMMC_BLOCKSIZE - 1)) != 0) {
                return EINVAL;
        }

        int err = sys_mutex_lock(hdl->mtx, MTX_TIMEOUT);
        if (err) {
                return err;
        }

        u32_t mode = hdl->mode;

        u32_t block_address = *fpos / SDMMC_BLOCKSIZE;
        u32_t blocks = count / SDMMC_BLOCKSIZE;

        for (int i = 0; i < 5; i++) {

                err = card_wait_ready(hdl);
                if (err) {
                        dev_dbg(hdl, "card busy at read transfer");
                }

                if (mode == _SDMMC_MODE_POLLING) {

                        err = card_read_blocks(hdl, dst, block_address, blocks);

                } else if (mode == _SDMMC_MODE_IRQ) {

                        err = card_read_blocks_IT(hdl, dst, block_address, blocks);

                } else if (mode == _SDMMC_MODE_DMA) {

                        err = card_read_blocks_DMA(hdl, dst, block_address, blocks);

                } else {
                        err = EFAULT;
                        dev_dbg(hdl, "unknown card mode!");
                }

                if (!err) {
                        *rdcnt += count;
                        break;
                } else {
                        dev_dbg(hdl, "read error %d at block %u", err, block_address);

                        if (i >= 3) {
                                card_initialize(hdl);
                        }
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
API_MOD_IOCTL(SDMMC, void *device_handle, int request, void *arg)
{
        UNUSED_ARG1(arg);

        SDMMC_t *hdl = device_handle;

        int err = EBADRQC;

        switch (request) {
        case IOCTL_SDMMC__INITIALIZE_CARD:
                err = sys_mutex_lock(hdl->mtx, _WAIT_FOREVER_MS);
                if (!err) {
                        err = card_initialize(hdl);
                        sys_mutex_unlock(hdl->mtx);
                }
                break;

        default:
                return EBADRQC;
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
API_MOD_FLUSH(SDMMC, void *device_handle)
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
API_MOD_STAT(SDMMC, void *device_handle, struct vfs_dev_stat *device_stat)
{
        SDMMC_t *hdl = device_handle;

        device_stat->st_size = hdl->card_size;

        return ESUCC;
}

//==============================================================================
/**
 * @brief  Register DMA channel.
 *
 * @param  hdl          device handle
 *
 * @return One of errno value.
 */
//==============================================================================
static int register_DMA_channel(SDMMC_t *hdl)
{
        DMA_HandleTypeDef *dmah;
        int err = sys_zalloc(sizeof(*dmah), cast(void **, &dmah));
        if (err) {
                return err;
        }

        dmah->dmad = _DMA_DDI_reserve(SDMMC[hdl->major].DMA_major, SDMMC[hdl->major].DMA_stream_pri);

        if (!dmah->dmad) {
                dmah->dmad = _DMA_DDI_reserve(SDMMC[hdl->major].DMA_major, SDMMC[hdl->major].DMA_stream_alt);
        }

        if (!dmah->dmad) {
                sys_free(cast(void **, &dmah));
                return EFAULT;
        }

        if (hdl->config.device_type == SDMMC_DEVICE_TYPE__SD) {
#if SD_ENABLED
                dmah->Config.user_ctx = &hdl->handle.sd;
#endif
        } else if (hdl->config.device_type == SDMMC_DEVICE_TYPE__MMC) {
#if MMC_ENABLED
                dmah->Config.user_ctx = &hdl->handle.mmc;
#endif
        }

        dmah->Config.cb_finish = NULL;
        dmah->Config.cb_half = NULL;
        dmah->Config.cb_next = NULL;
        dmah->Config.data_number = 0;
        dmah->Config.peripheral_address = cast(u32_t, &SDMMC[hdl->major].instance->FIFO);
        dmah->Config.memory_address[0] = 0;
        dmah->Config.memory_address[1] = 0;
        dmah->Config.IRQ_priority = SDMMC[hdl->major].IRQ_priority;
        dmah->Config.channel = SDMMC[hdl->major].DMA_channel;
        dmah->Config.release = false;
        dmah->Config.mode = _DMA_DDI_MODE_FIFO;
        dmah->Config.fifo_threshold = _DMA_DDI_FIFO_THRESHOLD_1_2;
        dmah->Config.memory_burst = _DMA_DDI_MEMORY_BURST_4_BEATS;
        dmah->Config.peripheral_burst = _DMA_DDI_PERIPHERAL_BURST_4_BEATS;
        dmah->Config.double_buffer_mode = _DMA_DDI_DOUBLE_BUFFER_MODE_DISABLED;
        dmah->Config.priority_level = _DMA_DDI_PRIORITY_LEVEL_VERY_HIGH;
        dmah->Config.peripheral_increment_offset = _DMA_DDI_PERIPHERAL_INCREMENT_OFFSET_ACCORDING_TO_PERIPHERAL_SIZE;
        dmah->Config.memory_data_size = _DMA_DDI_MEMORY_DATA_SIZE_BYTE;
        dmah->Config.peripheral_data_size = _DMA_DDI_PERIPHERAL_DATA_SIZE_WORD;
        dmah->Config.memory_address_increment = _DMA_DDI_MEMORY_ADDRESS_POINTER_INCREMENTED;
        dmah->Config.peripheral_address_increment = _DMA_DDI_PERIPHERAL_ADDRESS_POINTER_IS_FIXED;
        dmah->Config.circular_mode = _DMA_DDI_CIRCULAR_MODE_DISABLED;
        dmah->Config.transfer_direction = _DMA_DDI_TRANSFER_DIRECTION_MEMORY_TO_PERIPHERAL;
        dmah->Config.flow_controller = _DMA_DDI_FLOW_CONTROLLER_PERIPHERAL;

        if (hdl->config.device_type == SDMMC_DEVICE_TYPE__SD) {
#if SD_ENABLED
                dmah->Parent = &hdl->handle.sd;
#endif
        } else if (hdl->config.device_type == SDMMC_DEVICE_TYPE__MMC) {
#if MMC_ENABLED
                dmah->Parent = &hdl->handle.mmc;
#endif
        }

        hdl->mode = _SDMMC_MODE_DMA;

        if (hdl->config.device_type == SDMMC_DEVICE_TYPE__SD) {
#if SD_ENABLED
                hdl->handle.sd.hdma = dmah;
#endif
        } else if (hdl->config.device_type == SDMMC_DEVICE_TYPE__MMC) {
#if MMC_ENABLED
                hdl->handle.mmc.hdma = dmah;
#endif
        }

        return err;
}

//==============================================================================
/**
 * @brief  Card initialization.
 *
 * @param  hdl          device handle
 *
 * @return One of errno value (errno.h).
 */
//==============================================================================
static int card_initialize(SDMMC_t *hdl)
{
        int err = EFAULT;

        if (hdl->config.device_type > SDMMC_DEVICE_TYPE__MMC) {
                dev_dbg(hdl, "device type out of range!");
                return EINVAL;
        }

        if (hdl->config.clock_edge > SDMMC_CLOCK_EDGE__FALLING) {
                dev_dbg(hdl, "clock edge out of range!");
                return EINVAL;
        }

        if (hdl->config.bus_wide > SDMMC_BUS_WIDE__8BIT) {
                dev_dbg(hdl, "bus wide out of range!");
                return EINVAL;
        }

        if ((hdl->config.device_type == SDMMC_DEVICE_TYPE__SD) and (hdl->config.bus_wide > SDMMC_BUS_WIDE__4BIT)) {
                dev_dbg(hdl, "set SD bus to 4 bits!");
                hdl->config.bus_wide = SDMMC_BUS_WIDE__4BIT;
        }

        if (hdl->config.clock_div == 0) {
                dev_dbg(hdl, "set clock divider to 1.");
                hdl->config.clock_div = 1;
        }

        if (hdl->config.clock_div > 256) {
                dev_dbg(hdl, "limited clock divider to 256.");
                hdl->config.clock_div = 256;
        }

        hdl->mode = SDMMC[hdl->major].mode;

        if (hdl->config.device_type == SDMMC_DEVICE_TYPE__SD) {
#if SD_ENABLED
                hdl->handle.sd.CtxPtr = hdl;

                if (SDMMC[hdl->major].mode == _SDMMC_MODE_DMA) {

                        if (not hdl->handle.sd.hdma) {
                                err = register_DMA_channel(hdl);
                        } else {
                                err = 0; // already registered
                        }

                        if (err) {
                                dev_dbg(hdl, "DMA not accessible, using IRQ mode");
                                hdl->mode = _SDMMC_MODE_IRQ;
                                err = 0;
                        }
                }

                hdl->handle.sd.Instance = SDMMC[hdl->major].instance;
                hdl->handle.sd.Init.ClockEdge = CLOCK_EDGE[hdl->config.clock_edge];
                hdl->handle.sd.Init.ClockPowerSave = CLOCK_POWER_SAVE[hdl->config.clock_power_save];
                hdl->handle.sd.Init.ClockBypass = SDMMC_CLOCK_BYPASS_DISABLE;
                hdl->handle.sd.Init.BusWide = SDMMC_BUS_WIDE_1B;
                hdl->handle.sd.Init.HardwareFlowControl = HARDWARE_FLOW_CONTROL[hdl->config.hardware_flow_ctrl];
                hdl->handle.sd.Init.ClockDiv = hdl->config.clock_div;

                for (int i = 0; i < 3; i++) {
                        HAL_SD_DeInit(&hdl->handle.sd);

                        err = HAL_SD_Init(&hdl->handle.sd);
                        if (!err) {

                                const char *mode = "?";
                                switch (hdl->mode) {
                                case _SDMMC_MODE_POLLING:
                                        mode = "polling";
                                        break;
                                case _SDMMC_MODE_IRQ:
                                        mode = "IRQ";
                                        break;
                                case _SDMMC_MODE_DMA:
                                        mode = "DMA";
                                        break;
                                }

                                dev_dbg(hdl, "using SD in %s mode", mode);

                                if (HAL_SD_ConfigWideBusOperation(&hdl->handle.sd, BUS_WIDE[hdl->config.bus_wide]) ==
                                        HAL_OK) {
                                        dev_dbg(hdl, "switched to %u-bit bus", BUS_WIDE_BITS[hdl->config.bus_wide]);
                                } else {
                                        dev_dbg(hdl, "%u-bit bus not supported", BUS_WIDE_BITS[hdl->config.bus_wide]);
                                }

                                HAL_SD_CardInfoTypeDef info;
                                err = HAL_SD_GetCardInfo(&hdl->handle.sd, &info);
                                if (!err) {

                                        dev_dbg(hdl, "found SD%s card", info.CardType == CARD_SDSC ? "SC" : "HC/XC");

                                        dev_dbg(hdl, "%d %u-byte logical blocks", info.BlockNbr, info.BlockSize);

                                        hdl->card_size = cast(u64_t, info.BlockSize) * cast(u64_t, info.BlockNbr);
                                }
                        }

                        if (err) {
                                card_check_sdmmc_error(hdl);
                                HAL_SD_DeInit(&hdl->handle.sd);
                        } else {
                                break;
                        }
                }
#else
                dev_dbg(hdl, "SD support not enabled!");
                err = ENOTSUP;
#endif
        } else if (hdl->config.device_type == SDMMC_DEVICE_TYPE__MMC) {
#if MMC_ENABLED
                hdl->handle.mmc.CtxPtr = hdl;

                if (SDMMC[hdl->major].mode == _SDMMC_MODE_DMA) {

                        if (not hdl->handle.mmc.hdma) {
                                err = register_DMA_channel(hdl);
                        } else {
                                err = 0; // already registered
                        }

                        if (err) {
                                dev_dbg(hdl, "DMA not accessible, using IRQ mode");
                                hdl->mode = _SDMMC_MODE_IRQ;
                                err = 0;
                        }
                }

                hdl->handle.mmc.Instance = SDMMC[hdl->major].instance;
                hdl->handle.mmc.Init.ClockEdge = CLOCK_EDGE[hdl->config.clock_edge];
                hdl->handle.mmc.Init.ClockPowerSave = CLOCK_POWER_SAVE[hdl->config.clock_power_save];
                hdl->handle.mmc.Init.BusWide = SDMMC_BUS_WIDE_1B;
                hdl->handle.mmc.Init.HardwareFlowControl = HARDWARE_FLOW_CONTROL[hdl->config.hardware_flow_ctrl];
                hdl->handle.mmc.Init.ClockDiv = hdl->config.clock_div;

                for (int i = 0; i < 3; i++) {
                        HAL_MMC_DeInit(&hdl->handle.mmc);

                        err = HAL_MMC_Init(&hdl->handle.mmc);
                        if (!err) {

                                const char *mode = "?";
                                switch (hdl->mode) {
                                case _SDMMC_MODE_POLLING:
                                        mode = "polling";
                                        break;
                                case _SDMMC_MODE_IRQ:
                                        mode = "IRQ";
                                        break;
                                case _SDMMC_MODE_DMA:
                                        mode = "DMA";
                                        break;
                                }
                                dev_dbg(hdl, "using MMC in %s mode", mode);

                                if (HAL_MMC_ConfigWideBusOperation(&hdl->handle.mmc, BUS_WIDE[hdl->config.bus_wide]) ==
                                        HAL_OK) {
                                        dev_dbg(hdl, "switched to %u-bit bus", BUS_WIDE_BITS[hdl->config.bus_wide]);
                                } else {
                                        dev_dbg(hdl, "%u-bit bus not supported", BUS_WIDE_BITS[hdl->config.bus_wide]);
                                }

                                HAL_MMC_CardInfoTypeDef info;
                                err = HAL_MMC_GetCardInfo(&hdl->handle.mmc, &info);
                                if (!err) {

                                        dev_dbg(hdl, "found %s capacity MMC card",
                                                info.CardType == MMC_LOW_CAPACITY_CARD ? "low" : "high");

                                        dev_dbg(hdl, "%d %u-byte logical blocks", info.BlockNbr, info.BlockSize);

                                        hdl->card_size = cast(u64_t, info.BlockSize) * cast(u64_t, info.BlockNbr);
                                }
                        }

                        if (err) {
                                card_check_sdmmc_error(hdl);
                                HAL_MMC_DeInit(&hdl->handle.mmc);
                        } else {
                                break;
                        }
                }
#else
                dev_dbg(hdl, "MMC support not enabled!");
                err = ENOTSUP;
#endif
        } else {
                dev_dbg(hdl, "unknown device type: %d!", hdl->config.device_type);
        }

        return err;
}

//==============================================================================
/**
 * @brief  Wait for card to be ready.
 *
 * @param  hdl          device handle
 *
 * @return One of errno value (errno.h).
 */
//==============================================================================
static int card_wait_ready(SDMMC_t *hdl)
{
        clock_t timeout = sys_get_uptime_ms() + max(hdl->config.card_timeout / 10, 100);

        while (sys_get_uptime_ms() < timeout) {

                uint32_t state = 0;
                uint32_t expected_state = 0;

                if (hdl->config.device_type == SDMMC_DEVICE_TYPE__SD) {
#if SD_ENABLED
                        state = HAL_SD_GetCardState(&hdl->handle.sd);
                        expected_state = HAL_SD_CARD_TRANSFER;
#endif
                } else if (hdl->config.device_type == SDMMC_DEVICE_TYPE__MMC) {
#if MMC_ENABLED
                        state = HAL_MMC_GetCardState(&hdl->handle.mmc);
                        expected_state = HAL_MMC_CARD_TRANSFER;
#endif
                }

                if (state == expected_state) {
                        return 0;
                } else {
                        sys_sleep_ms(1);
                }
        }

        return ETIME;
}

//==============================================================================
/**
 * @brief  Check SD/MMC card error.
 *
 * @param  hdl          device handle
 *
 * @return One of errno value (errno.h).
 */
//==============================================================================
static int card_check_sdmmc_error(SDMMC_t *hdl)
{
        u32_t hal_err = HAL_SD_ERROR_NONE;

        if (hdl->config.device_type == SDMMC_DEVICE_TYPE__SD) {
#if SD_ENABLED
                hal_err = HAL_SD_GetError(&hdl->handle.sd);
#endif
        } else if (hdl->config.device_type == SDMMC_DEVICE_TYPE__MMC) {
#if MMC_ENABLED
                hal_err = HAL_MMC_GetError(&hdl->handle.mmc);
#endif
        }

        if (hal_err == HAL_SD_ERROR_NONE) {
                return 0;
        }

        int err = EIO;

        if (hal_err & HAL_SD_ERROR_CMD_CRC_FAIL) {
                dev_dbg(hdl, "Command response received (but CRC check failed)");
        }

        if (hal_err & HAL_SD_ERROR_DATA_CRC_FAIL) {
                dev_dbg(hdl, "Data block sent/received (CRC check failed)");
        }

        if (hal_err & HAL_SD_ERROR_CMD_RSP_TIMEOUT) {
                dev_dbg(hdl, "Command response timeout");
                err = ETIME;
        }

        if (hal_err & HAL_SD_ERROR_DATA_TIMEOUT) {
                dev_dbg(hdl, "Data timeout");
                err = ETIME;
        }

        if (hal_err & HAL_SD_ERROR_TX_UNDERRUN) {
                dev_dbg(hdl, "Transmit FIFO underrun");
        }

        if (hal_err & HAL_SD_ERROR_RX_OVERRUN) {
                dev_dbg(hdl, "Receive FIFO overrun");
        }

        if (hal_err & HAL_SD_ERROR_ADDR_MISALIGNED) {
                dev_dbg(hdl, "Misaligned address");
        }

        if (hal_err & HAL_SD_ERROR_BLOCK_LEN_ERR) {
                dev_dbg(hdl,
                        "Transferred block length is not allowed for the card or "
                        "the number of transferred bytes does not match the block "
                        "length");
        }

        if (hal_err & HAL_SD_ERROR_ERASE_SEQ_ERR) {
                dev_dbg(hdl, "An error in the sequence of erase command occurs");
        }

        if (hal_err & HAL_SD_ERROR_BAD_ERASE_PARAM) {
                dev_dbg(hdl, "An invalid selection for erase groups");
        }

        if (hal_err & HAL_SD_ERROR_WRITE_PROT_VIOLATION) {
                dev_dbg(hdl, "Attempt to program a write protect block");
        }

        if (hal_err & HAL_SD_ERROR_LOCK_UNLOCK_FAILED) {
                dev_dbg(hdl,
                        "Sequence or password error has been detected in unlock "
                        "command or if there was an attempt to access a locked card");
        }

        if (hal_err & HAL_SD_ERROR_COM_CRC_FAILED) {
                dev_dbg(hdl, "CRC check of the previous command failed");
        }

        if (hal_err & HAL_SD_ERROR_ILLEGAL_CMD) {
                dev_dbg(hdl, "Command is not legal for the card state ");
        }
        if (hal_err & HAL_SD_ERROR_CARD_ECC_FAILED) {
                dev_dbg(hdl,
                        "Card internal ECC was applied but failed to correct the "
                        "data");
        }
        if (hal_err & HAL_SD_ERROR_CC_ERR) {
                dev_dbg(hdl, "Internal card controller error");
        }

        if (hal_err & HAL_SD_ERROR_GENERAL_UNKNOWN_ERR) {
                dev_dbg(hdl, "General or unknown error");
        }

        if (hal_err & HAL_SD_ERROR_STREAM_READ_UNDERRUN) {
                dev_dbg(hdl, "The card could not sustain data reading in stream rmode");
        }

        if (hal_err & HAL_SD_ERROR_STREAM_WRITE_OVERRUN) {
                dev_dbg(hdl, "The card could not sustain data programming in stream mode", );
        }

        if (hal_err & HAL_SD_ERROR_CID_CSD_OVERWRITE) {
                dev_dbg(hdl, "CID/CSD overwrite error");
        }

        if (hal_err & HAL_SD_ERROR_WP_ERASE_SKIP) {
                dev_dbg(hdl, "Only partial address space was erased");
        }

        if (hal_err & HAL_SD_ERROR_CARD_ECC_DISABLED) {
                dev_dbg(hdl, "Command has been executed without using internal ECC");
        }

        if (hal_err & HAL_SD_ERROR_ERASE_RESET) {
                dev_dbg(hdl,
                        "Erase sequence was cleared before executing because an "
                        "out of erase sequence command was received");
        }

        if (hal_err & HAL_SD_ERROR_AKE_SEQ_ERR) {
                dev_dbg(hdl, "Error in sequence of authentication");
        }

        if (hal_err & HAL_SD_ERROR_INVALID_VOLTRANGE) {
                dev_dbg(hdl, "Invalid voltage range");
        }

        if (hal_err & HAL_SD_ERROR_ADDR_OUT_OF_RANGE) {
                dev_dbg(hdl, "Addressed block is out of range");
        }

        if (hal_err & HAL_SD_ERROR_REQUEST_NOT_APPLICABLE) {
                dev_dbg(hdl, "Command request is not applicable");
        }

        if (hal_err & HAL_SD_ERROR_PARAM) {
                dev_dbg(hdl, "The used parameter is not valid");
        }

        if (hal_err & HAL_SD_ERROR_UNSUPPORTED_FEATURE) {
                dev_dbg(hdl, "Feature is not supported");
        }

        if (hal_err & HAL_SD_ERROR_BUSY) {
                dev_dbg(hdl, "Transfer process is busy");
                err = EBUSY;
        }

        if (hal_err & HAL_SD_ERROR_DMA) {
                dev_dbg(hdl, "Error while DMA transfer");
        }

        if (hal_err & HAL_SD_ERROR_TIMEOUT) {
                dev_dbg(hdl, "Timeout error");
                err = ETIME;
        }

        return err;
}

//==============================================================================
/**
 * @brief  Write blocks to SD/MMC by using polling method.
 *
 * @param  hdl                  driver handle
 * @param  src                  source buffer
 * @param  block_address        block address
 * @param  blocks               blocks to write
 *
 * @return One of errno value (errno.h).
 */
//==============================================================================
static int card_write_blocks(SDMMC_t *hdl, const u8_t *src, uint32_t block_address, uint32_t blocks)
{
        int err = EFAULT;

        if (hdl->config.device_type == SDMMC_DEVICE_TYPE__SD) {
#if SD_ENABLED
                err = HAL_SD_WriteBlocks(&hdl->handle.sd, src, block_address, blocks, hdl->config.card_timeout);
                if (err) {
                        err = card_check_sdmmc_error(hdl);
                }
#endif
        } else if (hdl->config.device_type == SDMMC_DEVICE_TYPE__MMC) {
#if MMC_ENABLED
                err = HAL_MMC_WriteBlocks(&hdl->handle.mmc, src, block_address, blocks, hdl->config.card_timeout);
                if (err) {
                        err = card_check_sdmmc_error(hdl);
                }
#endif
        }

        return err;
}

//==============================================================================
/**
 * @brief  Write blocks to SD/MMC by using interrupts.
 *
 * @param  hdl                  driver handle
 * @param  src                  source buffer
 * @param  block_address        block address
 * @param  blocks               blocks to write
 * @param  timeout              timeout in milliseconds
 *
 * @return One of errno value (errno.h).
 */
//==============================================================================
static int card_write_blocks_IT(SDMMC_t *hdl, const u8_t *src, uint32_t block_address, uint32_t blocks)
{
        int err = EFAULT;

        if (hdl->config.device_type == SDMMC_DEVICE_TYPE__SD) {
#if SD_ENABLED
                err = HAL_SD_WriteBlocks_IT(&hdl->handle.sd, src, block_address, blocks);
#endif
        } else if (hdl->config.device_type == SDMMC_DEVICE_TYPE__MMC) {
#if MMC_ENABLED
                err = HAL_MMC_WriteBlocks_IT(&hdl->handle.mmc, src, block_address, blocks);
#endif
        }

        if (!err) {
                err = sys_semaphore_wait(hdl->sem_xfer_complete, hdl->config.card_timeout);
                if (err) {
                        dev_dbg(hdl, "write timeout");
                        card_check_sdmmc_error(hdl);
                } else {
                        err = card_check_sdmmc_error(hdl);
                }
        } else {
                err = card_check_sdmmc_error(hdl);
        }

        return err;
}

//==============================================================================
/**
 * @brief  Write blocks to SD/MMC by using DMA.
 *
 * @param  hdl                  driver handle
 * @param  src                  source buffer
 * @param  block_address        block address
 * @param  blocks               blocks to write
 * @param  timeout              timeout in milliseconds
 *
 * @return One of errno value (errno.h).
 */
//==============================================================================
static int card_write_blocks_DMA(SDMMC_t *hdl, const u8_t *src, uint32_t block_address, uint32_t blocks)
{
        int err = EFAULT;

        bool mem_dma = sys_is_mem_dma_capable(src) && (((uintptr_t)src % _HEAP_ALIGN_) == 0);

        if (mem_dma) {
                _cpuctl_clean_invalidate_dcache_by_addr(const_cast(src), blocks * SDMMC_BLOCKSIZE);
        }

        if (hdl->config.device_type == SDMMC_DEVICE_TYPE__SD) {
#if SD_ENABLED
                if (mem_dma) {
                        err = HAL_SD_WriteBlocks_DMA(&hdl->handle.sd, src, block_address, blocks);
                } else {
                        err = HAL_SD_WriteBlocks_IT(&hdl->handle.sd, src, block_address, blocks);
                }
#endif
        } else if (hdl->config.device_type == SDMMC_DEVICE_TYPE__MMC) {
#if MMC_ENABLED
                if (mem_dma) {
                        err = HAL_MMC_WriteBlocks_DMA(&hdl->handle.mmc, src, block_address, blocks);
                } else {
                        err = HAL_MMC_WriteBlocks_IT(&hdl->handle.mmc, src, block_address, blocks);
                }
#endif
        }

        if (!err) {
                err = sys_semaphore_wait(hdl->sem_xfer_complete, hdl->config.card_timeout);
                if (err) {
                        dev_dbg(hdl, "write timeout");
                        card_check_sdmmc_error(hdl);
                } else {
                        err = card_check_sdmmc_error(hdl);
                }
        } else {
                err = card_check_sdmmc_error(hdl);
        }

        return err;
}

//==============================================================================
/**
 * @brief  Read blocks to SD/MMC by using polling method.
 *
 * @param  hdl                  driver handle
 * @param  dst                  destination buffer
 * @param  block_address        block address
 * @param  blocks               blocks to write
 *
 * @return One of errno value (errno.h).
 */
//==============================================================================
static int card_read_blocks(SDMMC_t *hdl, u8_t *dst, uint32_t block_address, uint32_t blocks)
{
        int err = EFAULT;

        if (hdl->config.device_type == SDMMC_DEVICE_TYPE__SD) {
#if SD_ENABLED
                err = HAL_SD_ReadBlocks(&hdl->handle.sd, dst, block_address, blocks, hdl->config.card_timeout);
#endif
        } else if (hdl->config.device_type == SDMMC_DEVICE_TYPE__MMC) {
#if MMC_ENABLED
                err = HAL_MMC_ReadBlocks(&hdl->handle.mmc, dst, block_address, blocks, hdl->config.card_timeout);
#endif
        }

        if (err) {
                err = card_check_sdmmc_error(hdl);
        }

        return err;
}

//==============================================================================
/**
 * @brief  Read blocks to SD/MMC by using interrupts.
 *
 * @param  hdl                  driver handle
 * @param  dst                  destination buffer
 * @param  block_address        block address
 * @param  blocks               blocks to write
 * @param  timeout              timeout in milliseconds
 *
 * @return One of errno value (errno.h).
 */
//==============================================================================
static int card_read_blocks_IT(SDMMC_t *hdl, u8_t *dst, uint32_t block_address, uint32_t blocks)
{
        int err = EFAULT;

        if (hdl->config.device_type == SDMMC_DEVICE_TYPE__SD) {
#if SD_ENABLED
                err = HAL_SD_ReadBlocks_IT(&hdl->handle.sd, dst, block_address, blocks);
#endif
        } else if (hdl->config.device_type == SDMMC_DEVICE_TYPE__MMC) {
#if MMC_ENABLED
                err = HAL_MMC_ReadBlocks_IT(&hdl->handle.mmc, dst, block_address, blocks);
#endif
        }

        if (!err) {
                err = sys_semaphore_wait(hdl->sem_xfer_complete, hdl->config.card_timeout);
                if (err) {
                        dev_dbg(hdl, "read timeout");
                        card_check_sdmmc_error(hdl);
                } else {
                        err = card_check_sdmmc_error(hdl);
                }
        } else {
                err = card_check_sdmmc_error(hdl);
        }

        return err;
}

//==============================================================================
/**
 * @brief  Read blocks to SD/MMC by using DMA.
 *
 * @param  hdl                  driver handle
 * @param  dst                  destination buffer
 * @param  block_address        block address
 * @param  blocks               blocks to write
 *
 * @return One of errno value (errno.h).
 */
//==============================================================================
static int card_read_blocks_DMA(SDMMC_t *hdl, u8_t *dst, uint32_t block_address, uint32_t blocks)
{
        int err = EFAULT;

        bool mem_dma = sys_is_mem_dma_capable(dst) && (((uintptr_t)dst % _HEAP_ALIGN_) == 0);

        if (hdl->config.device_type == SDMMC_DEVICE_TYPE__SD) {
#if SD_ENABLED
                if (mem_dma) {
                        err = HAL_SD_ReadBlocks_DMA(&hdl->handle.sd, dst, block_address, blocks);
                } else {
                        err = HAL_SD_ReadBlocks_IT(&hdl->handle.sd, dst, block_address, blocks);
                }
#endif
        } else if (hdl->config.device_type == SDMMC_DEVICE_TYPE__MMC) {
#if MMC_ENABLED
                if (mem_dma) {
                        err = HAL_MMC_ReadBlocks_DMA(&hdl->handle.mmc, dst, block_address, blocks);
                } else {
                        err = HAL_MMC_ReadBlocks_IT(&hdl->handle.mmc, dst, block_address, blocks);
                }
#endif
        }

        if (!err) {
                err = sys_semaphore_wait(hdl->sem_xfer_complete, hdl->config.card_timeout);
                if (err) {
                        dev_dbg(hdl, "read timeout");
                        card_check_sdmmc_error(hdl);
                } else {
                        err = card_check_sdmmc_error(hdl);
                }
        } else {
                err = card_check_sdmmc_error(hdl);
        }

        if (!err && mem_dma) {
                size_t cache_line_size = 32;
                _cpuctl_invalidate_dcache_by_addr(dst - cache_line_size,
                        blocks * SDMMC_BLOCKSIZE + 2 * cache_line_size);
        }

        return err;
}

//==============================================================================
/**
 * @brief  Basic interface initialization.
 *
 * @param  hdl          driver instance
 */
//==============================================================================
static void HAL_SDMMC_MspInit(SDMMC_t *hdl)
{
        SET_BIT(*SDMMC[hdl->major].RCC_APBENR, SDMMC[hdl->major].RCC_APBENR_SDMMCEN);

        if (hdl->mode != _SDMMC_MODE_POLLING) {
                NVIC_SetPriority(SDMMC[hdl->major].IRQn, SDMMC[hdl->major].IRQ_priority);
                NVIC_ClearPendingIRQ(SDMMC[hdl->major].IRQn);
                NVIC_EnableIRQ(SDMMC[hdl->major].IRQn);
        }
}

//==============================================================================
/**
 * @brief  Basic interface de-initialization.
 *
 * @param  hdl          driver instance
 */
//==============================================================================
static void HAL_SDMMC_MspDeInit(SDMMC_t *hdl)
{
        NVIC_ClearPendingIRQ(SDMMC[hdl->major].IRQn);
        NVIC_DisableIRQ(SDMMC[hdl->major].IRQn);

        CLEAR_BIT(*SDMMC[hdl->major].RCC_APBENR, SDMMC[hdl->major].RCC_APBENR_SDMMCEN);
}

//==============================================================================
/**
 * @brief  Basic SD interface initialization.
 *
 * @param  hsd          SD handle
 */
//==============================================================================
void HAL_SD_MspInit(SD_HandleTypeDef *hsd)
{
        HAL_SDMMC_MspInit(hsd->CtxPtr);
}

//==============================================================================
/**
 * @brief  Basic SD interface de-initialization.
 *
 * @param  hsd          SD handle
 */
//==============================================================================
void HAL_SD_MspDeInit(SD_HandleTypeDef *hsd)
{
        HAL_SDMMC_MspDeInit(hsd->CtxPtr);
}

//==============================================================================
/**
 * @brief  Basic MMC interface initialization.
 *
 * @param  hsd          SD handle
 */
//==============================================================================
void HAL_MMC_MspInit(MMC_HandleTypeDef *hmmc)
{
        HAL_SDMMC_MspInit(hmmc->CtxPtr);
}

//==============================================================================
/**
 * @brief  Basic MMC interface de-initialization.
 *
 * @param  hsd          SD handle
 */
//==============================================================================
void HAL_MMC_MspDeInit(MMC_HandleTypeDef *hmmc)
{
        HAL_SDMMC_MspDeInit(hmmc->CtxPtr);
}

#if SD_ENABLED
//==============================================================================
/**
 * @brief  Rx complete callback.
 *
 * @param  hsd          SD handle
 */
//==============================================================================
void HAL_SD_RxCpltCallback(SD_HandleTypeDef *hsd)
{
        SDMMC_t *hdl = hsd->CtxPtr;
        sys_semaphore_signal_from_ISR(hdl->sem_xfer_complete, &hdl->irq_yield);
}

//==============================================================================
/**
 * @brief  Tx complete callback.
 *
 * @param  hsd          SD handle
 */
//==============================================================================
void HAL_SD_TxCpltCallback(SD_HandleTypeDef *hsd)
{
        SDMMC_t *hdl = hsd->CtxPtr;
        sys_semaphore_signal_from_ISR(hdl->sem_xfer_complete, &hdl->irq_yield);
}

//==============================================================================
/**
 * @brief  Tx complete callback.
 *
 * @param  hsd          SD handle
 */
//==============================================================================
void HAL_SD_ErrorCallback(SD_HandleTypeDef *hsd)
{
        SDMMC_t *hdl = hsd->CtxPtr;
        sys_semaphore_signal_from_ISR(hdl->sem_xfer_complete, &hdl->irq_yield);
}
#endif

#if MMC_ENABLED
//==============================================================================
/**
 * @brief  Rx complete callback.
 *
 * @param  hsd          SD handle
 */
//==============================================================================
void HAL_MMC_RxCpltCallback(MMC_HandleTypeDef *hmmc)
{
        SDMMC_t *hdl = hmmc->CtxPtr;
        sys_semaphore_signal_from_ISR(hdl->sem_xfer_complete, &hdl->irq_yield);
}

//==============================================================================
/**
 * @brief  Tx complete callback.
 *
 * @param  hsd          SD handle
 */
//==============================================================================
void HAL_MMC_TxCpltCallback(MMC_HandleTypeDef *hmmc)
{
        SDMMC_t *hdl = hmmc->CtxPtr;
        sys_semaphore_signal_from_ISR(hdl->sem_xfer_complete, &hdl->irq_yield);
}

//==============================================================================
/**
 * @brief  Tx complete callback.
 *
 * @param  hsd          SD handle
 */
//==============================================================================
void HAL_MMC_ErrorCallback(MMC_HandleTypeDef *hmmc)
{
        SDMMC_t *hdl = hmmc->CtxPtr;
        sys_semaphore_signal_from_ISR(hdl->sem_xfer_complete, &hdl->irq_yield);
}
#endif

//==============================================================================
/**
 * @brief  SDMMC1 IRQ handler.
 */
//==============================================================================
#if defined(SDMMC1) && (__SDMMC_CFG_SDMMC1_MODE__ != _SDMMC_MODE_POLLING)
void SDMMC1_IRQHandler(void)
{
        if (sdmmc[SDMMC_1]) {
                if (sdmmc[SDMMC_1]->config.device_type == SDMMC_DEVICE_TYPE__SD) {
#if SD_ENABLED
                        HAL_SD_IRQHandler(&sdmmc[SDMMC_1]->handle.sd);
#endif
                } else if (sdmmc[SDMMC_1]->config.device_type == SDMMC_DEVICE_TYPE__MMC) {
#if MMC_ENABLED
                        HAL_MMC_IRQHandler(&sdmmc[SDMMC_1]->handle.mmc);
#endif
                }
        }

        sys_thread_yield_from_ISR(sdmmc[SDMMC_1]->irq_yield);
}
#endif

//==============================================================================
/**
 * @brief  SDMMC2 IRQ handler.
 */
//==============================================================================
#if defined(SDMMC2) && (__SDMMC_CFG_SDMMC2_MODE__ != _SDMMC_MODE_POLLING)
void SDMMC2_IRQHandler(void)
{
        if (sdmmc[SDMMC_2]) {
                if (sdmmc[SDMMC_2]->config.device_type == SDMMC_DEVICE_TYPE__SD) {
#if SD_ENABLED
                        HAL_SD_IRQHandler(&sdmmc[SDMMC_2]->handle.sd);
#endif
                } else if (sdmmc[SDMMC_2]->config.device_type == SDMMC_DEVICE_TYPE__MMC) {
#if MMC_ENABLED
                        HAL_MMC_IRQHandler(&sdmmc[SDMMC_2]->handle.mmc);
#endif
                }
        }

        sys_thread_yield_from_ISR(sdmmc[SDMMC_2]->irq_yield);
}
#endif

/*==============================================================================
  End of file
==============================================================================*/
