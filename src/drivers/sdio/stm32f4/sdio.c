/*==============================================================================
File    sdio.c

Author  Daniel Zorychta

Brief   SD Card Interface Driver.

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

#pragma GCC optimize("Os")

/*==============================================================================
  Include files
==============================================================================*/
#include "../sdio_ioctl.h"
#include "drivers/driver.h"
#include "stm32f4/dma_ddi.h"
#include "stm32f4xx_hal_sd.h"

/*==============================================================================
  Local macros
==============================================================================*/
#define MTX_TIMEOUT        25000

#define _SDIO_MODE_POLLING 0
#define _SDIO_MODE_IRQ     1
#define _SDIO_MODE_DMA     2

/*==============================================================================
  Local object types
==============================================================================*/
enum {
        SD1,
        SD_COUNT
};

/** driver instance associated with partition */
typedef struct {
        u32_t            mode;
        u8_t             major;
        u8_t             minor;
        SD_HandleTypeDef hsd;
        u64_t            card_size;
        kmtx_t          *mtx;
        ksem_t          *sem_xfer_complete;
        bool             irq_yield;
} SDIO_t;

typedef struct {
        SDIO_TypeDef *instance;
        __IO u32_t   *RCC_APBENR;
        u32_t         RCC_APBENR_SDMMCEN;
        IRQn_Type     IRQn;
        u32_t         IRQ_priority;
        u32_t         card_timeout;
        u32_t         clock_edge;
        u32_t         clock_power_save;
        u32_t         clock_div;
        u32_t         bus_wide;
        u32_t         hardware_flow_ctrl;
        u32_t         mode;
        u8_t          DMA_channel;
        u8_t          DMA_major;
        u8_t          DMA_stream_pri;
        u8_t          DMA_stream_alt;
} sd_info_t;

/*==============================================================================
  Local function prototypes
==============================================================================*/
static int card_initialize(SDIO_t *hdl);
static int card_wait_ready(SDIO_t *hdl);
static int card_check_error(SDIO_t *hdl);

/*==============================================================================
  Local object
==============================================================================*/
MODULE_NAME(SDIO);

/*
 * For IRQ purpose only.
 */
static SDIO_t *sdio[SD_COUNT];

/*
 * SDMMCx configuration.
 */
static const sd_info_t SDMMC[] = {
        {
                .instance           = SDIO,
                .RCC_APBENR         = &RCC->APB2ENR,
                .RCC_APBENR_SDMMCEN = RCC_APB2ENR_SDIOEN,
                .IRQn               = SDIO_IRQn,
                .IRQ_priority       = __SDIO_CFG_SDMMC1_IRQ_PRIORITY__,
                .card_timeout       = __SDIO_CFG_SDMMC1_CARD_TIMEOUT__,
                .clock_edge         = __SDIO_CFG_SDMMC1_CLOCK_EDGE__,
                .clock_power_save   = __SDIO_CFG_SDMMC1_PWRSAVE__,
                .clock_div          = __SDIO_CFG_SDMMC1_CKDIV__ - 1,
                .bus_wide           = __SDIO_CFG_SDMMC1_BUS_WIDE__,
                .hardware_flow_ctrl = __SDIO_CFG_SDMMC1_HW_FLOW_CTRL__,
                .mode               = __SDIO_CFG_SDMMC1_MODE__,
                .DMA_channel        = 4,
                .DMA_major          = 1,
                .DMA_stream_pri     = 3,
                .DMA_stream_alt     = 6,
        },
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
API_MOD_INIT(SDIO, void **device_handle, u8_t major, u8_t minor, const void *config)
{
        UNUSED_ARG1(config);

        int err = ENODEV;

        if ((major < SD_COUNT) && (minor == 0)) {
                err = sys_zalloc(sizeof(SDIO_t), device_handle);
                if (!err) {
                        SDIO_t *hdl = *device_handle;
                        hdl->major  = major;
                        hdl->minor  = minor;

                        err = sys_semaphore_create(1, 0, &hdl->sem_xfer_complete);
                        if (!err) {

                                err = sys_mutex_create(KMTX_TYPE_RECURSIVE, &hdl->mtx);
                                if (!err) {
                                        err = card_initialize(hdl);
                                        if (!err) {
                                                sdio[hdl->major] = hdl;
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
API_MOD_RELEASE(SDIO, void *device_handle)
{
        SDIO_t *hdl = device_handle;

        kmtx_t *mtx = hdl->mtx;

        int err = sys_mutex_lock(mtx, MTX_TIMEOUT);
        if (!err) {
                hdl->mtx = NULL;
                sys_sleep_ms(MTX_TIMEOUT + 500);
                HAL_SD_DeInit(&hdl->hsd);
                sys_mutex_unlock(mtx);
                sys_mutex_destroy(hdl->mtx);
                sys_free(device_handle);
                sdio[hdl->major] = NULL;
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
API_MOD_OPEN(SDIO, void *device_handle, u32_t flags)
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
API_MOD_CLOSE(SDIO, void *device_handle, bool force)
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
API_MOD_WRITE(SDIO, void *device_handle, const u8_t *src, size_t count, fpos_t *fpos,
        size_t *wrcnt, struct vfs_fattr fattr)
{
        UNUSED_ARG1(fattr);

        SDIO_t *hdl = device_handle;

        if ((*fpos & (BLOCKSIZE - 1)) != 0) {
                return EINVAL;
        }

        if ((count & (BLOCKSIZE - 1)) != 0) {
                return EINVAL;
        }

        int err = sys_mutex_lock(hdl->mtx, MTX_TIMEOUT);
        if (err) {
                return err;
        }

        u32_t block_address = *fpos / BLOCKSIZE;
        u32_t blocks        = count / BLOCKSIZE;

        for (int i = 0; i < 5; i++) {

                err = card_wait_ready(hdl);
                if (err) {
                        dev_dbg(hdl, "card busy at write transfer", 0);
                }

                if (hdl->mode == _SDIO_MODE_POLLING) {

                        err = HAL_SD_WriteBlocks(&hdl->hsd, src, block_address, blocks,
                                SDMMC[hdl->major].card_timeout);
                        if (err) {
                                err = card_check_error(hdl);
                        }

                } else if (hdl->mode == _SDIO_MODE_IRQ) {

                        err = HAL_SD_WriteBlocks_IT(&hdl->hsd, src, block_address,
                                blocks);
                        if (!err) {
                                err = sys_semaphore_wait(hdl->sem_xfer_complete,
                                        SDMMC[hdl->major].card_timeout);
                                if (err) {
                                        dev_dbg(hdl, "write timeout", 0);
                                        card_check_error(hdl);
                                } else {
                                        err = card_check_error(hdl);
                                }
                        } else {
                                err = card_check_error(hdl);
                        }

                } else if (hdl->mode == _SDIO_MODE_DMA) {

                        err = HAL_SD_WriteBlocks_DMA(&hdl->hsd, src, block_address,
                                blocks);
                        if (!err) {
                                err = sys_semaphore_wait(hdl->sem_xfer_complete,
                                        SDMMC[hdl->major].card_timeout);
                                if (err) {
                                        dev_dbg(hdl, "write timeout", 0);
                                        card_check_error(hdl);
                                } else {
                                        err = card_check_error(hdl);
                                }
                        } else {
                                err = card_check_error(hdl);
                        }

                } else {
                        err = EFAULT;
                        dev_dbg(hdl, "unknown card mode!", 0);
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
API_MOD_READ(SDIO, void *device_handle, u8_t *dst, size_t count, fpos_t *fpos,
        size_t *rdcnt, struct vfs_fattr fattr)
{
        UNUSED_ARG1(fattr);

        SDIO_t *hdl = device_handle;

        if ((*fpos & (BLOCKSIZE - 1)) != 0) {
                return EINVAL;
        }

        if ((count & (BLOCKSIZE - 1)) != 0) {
                return EINVAL;
        }

        int err = sys_mutex_lock(hdl->mtx, MTX_TIMEOUT);
        if (err) {
                return err;
        }

        u32_t block_address = *fpos / BLOCKSIZE;
        u32_t blocks        = count / BLOCKSIZE;

        for (int i = 0; i < 5; i++) {

                err = card_wait_ready(hdl);
                if (err) {
                        dev_dbg(hdl, "card busy at read transfer", 0);
                }

                if (hdl->mode == _SDIO_MODE_POLLING) {

                        err = HAL_SD_ReadBlocks(&hdl->hsd, dst, block_address, blocks,
                                SDMMC[hdl->major].card_timeout);
                        if (err) {
                                err = card_check_error(hdl);
                        }

                } else if (hdl->mode == _SDIO_MODE_IRQ) {

                        err = HAL_SD_ReadBlocks_IT(&hdl->hsd, dst, block_address, blocks);
                        if (!err) {
                                err = sys_semaphore_wait(hdl->sem_xfer_complete,
                                        SDMMC[hdl->major].card_timeout);
                                if (err) {
                                        dev_dbg(hdl, "read timeout", 0);
                                        card_check_error(hdl);
                                } else {
                                        err = card_check_error(hdl);
                                }
                        } else {
                                err = card_check_error(hdl);
                        }

                } else if (hdl->mode == _SDIO_MODE_DMA) {

                        err = HAL_SD_ReadBlocks_DMA(&hdl->hsd, dst, block_address,
                                blocks);
                        if (!err) {
                                err = sys_semaphore_wait(hdl->sem_xfer_complete,
                                        SDMMC[hdl->major].card_timeout);
                                if (err) {
                                        dev_dbg(hdl, "read timeout", 0);
                                        card_check_error(hdl);
                                } else {
                                        err = card_check_error(hdl);
                                }
                        } else {
                                err = card_check_error(hdl);
                        }

                } else {
                        err = EFAULT;
                        dev_dbg(hdl, "unknown card mode!", 0);
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
API_MOD_IOCTL(SDIO, void *device_handle, int request, void *arg)
{
        UNUSED_ARG1(arg);

        SDIO_t *hdl = device_handle;

        int err = EBADRQC;

        switch (request) {
        case IOCTL_SDIO__INITIALIZE_CARD:
                err = sys_mutex_lock(hdl->mtx, _MAX_DELAY_MS);
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
API_MOD_FLUSH(SDIO, void *device_handle)
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
API_MOD_STAT(SDIO, void *device_handle, struct vfs_dev_stat *device_stat)
{
        SDIO_t *hdl = device_handle;

        device_stat->st_size = hdl->card_size;

        return ESUCC;
}

//==============================================================================
/**
 * @brief  Basic interface initialization.
 *
 * @param  hsd          SD handle
 */
//==============================================================================
void HAL_SD_MspInit(SD_HandleTypeDef *hsd)
{
        SDIO_t *hdl = hsd->CtxPtr;

        SET_BIT(*SDMMC[hdl->major].RCC_APBENR, SDMMC[hdl->major].RCC_APBENR_SDMMCEN);

        if (hdl->mode != _SDIO_MODE_POLLING) {
                NVIC_SetPriority(SDMMC[hdl->major].IRQn, SDMMC[hdl->major].IRQ_priority);
                NVIC_ClearPendingIRQ(SDMMC[hdl->major].IRQn);
                NVIC_EnableIRQ(SDMMC[hdl->major].IRQn);
        }
}

//==============================================================================
/**
 * @brief  Basic interface de-initialization.
 *
 * @param  hsd          SD handle
 */
//==============================================================================
void HAL_SD_MspDeInit(SD_HandleTypeDef *hsd)
{
        SDIO_t *hdl = hsd->CtxPtr;

        NVIC_ClearPendingIRQ(SDMMC[hdl->major].IRQn);
        NVIC_DisableIRQ(SDMMC[hdl->major].IRQn);

        CLEAR_BIT(*SDMMC[hdl->major].RCC_APBENR, SDMMC[hdl->major].RCC_APBENR_SDMMCEN);
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
static int register_DMA_channel(SDIO_t *hdl)
{
        DMA_HandleTypeDef *dmah;
        int                err = sys_zalloc(sizeof(*dmah), cast(void **, &dmah));
        if (err) {
                return err;
        }

        dmah->dmad = _DMA_DDI_reserve(SDMMC[hdl->major].DMA_major,
                SDMMC[hdl->major].DMA_stream_pri);

        if (!dmah->dmad) {
                dmah->dmad = _DMA_DDI_reserve(SDMMC[hdl->major].DMA_major,
                        SDMMC[hdl->major].DMA_stream_alt);
        }

        if (!dmah->dmad) {
                sys_free(cast(void **, &dmah));
                return EFAULT;
        }

        dmah->Config.user_ctx           = &hdl->hsd;
        dmah->Config.cb_finish          = NULL;
        dmah->Config.cb_half            = NULL;
        dmah->Config.cb_next            = NULL;
        dmah->Config.data_number        = 0;
        dmah->Config.peripheral_address = cast(u32_t, &SDIO->FIFO);
        dmah->Config.memory_address[0]  = 0;
        dmah->Config.memory_address[1]  = 0;
        dmah->Config.IRQ_priority       = SDMMC[hdl->major].IRQ_priority;
        dmah->Config.channel            = SDMMC[hdl->major].DMA_channel;
        dmah->Config.release            = false;
        dmah->Config.mode               = _DMA_DDI_MODE_FIFO;
        dmah->Config.fifo_threshold     = _DMA_DDI_FIFO_THRESHOLD_FULL;
        dmah->Config.memory_burst       = _DMA_DDI_MEMORY_BURST_4_BEATS;
        dmah->Config.peripheral_burst   = _DMA_DDI_PERIPHERAL_BURST_4_BEATS;
        dmah->Config.double_buffer_mode = _DMA_DDI_DOUBLE_BUFFER_MODE_DISABLED;
        dmah->Config.priority_level     = _DMA_DDI_PRIORITY_LEVEL_VERY_HIGH;
        dmah->Config.peripheral_increment_offset =
                _DMA_DDI_PERIPHERAL_INCREMENT_OFFSET_ACCORDING_TO_PERIPHERAL_SIZE;
        dmah->Config.memory_data_size     = _DMA_DDI_MEMORY_DATA_SIZE_BYTE;
        dmah->Config.peripheral_data_size = _DMA_DDI_PERIPHERAL_DATA_SIZE_WORD;
        dmah->Config
                .memory_address_increment = _DMA_DDI_MEMORY_ADDRESS_POINTER_INCREMENTED;
        dmah->Config.peripheral_address_increment =
                _DMA_DDI_PERIPHERAL_ADDRESS_POINTER_IS_FIXED;
        dmah->Config.circular_mode = _DMA_DDI_CIRCULAR_MODE_DISABLED;
        dmah->Config
                .transfer_direction  = _DMA_DDI_TRANSFER_DIRECTION_MEMORY_TO_PERIPHERAL;
        dmah->Config.flow_controller = _DMA_DDI_FLOW_CONTROLLER_PERIPHERAL;

        dmah->Parent = &hdl->hsd;

        hdl->mode = _SDIO_MODE_DMA;

        hdl->hsd.hdma = dmah;

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
static int card_initialize(SDIO_t *hdl)
{
        int err = EFAULT;

        hdl->hsd.CtxPtr = hdl;

        hdl->mode = SDMMC[hdl->major].mode;

        if (SDMMC[hdl->major].hardware_flow_ctrl == SDIO_HARDWARE_FLOW_CONTROL_ENABLE) {
                dev_dbg(hdl, "warning: hardware flow control enabled, hw bug may appear!",
                        0);
        }

        if (SDMMC[hdl->major].mode == _SDIO_MODE_DMA) {

                if (not hdl->hsd.hdma) {
                        err = register_DMA_channel(hdl);
                } else {
                        err = 0; // already registered
                }

                if (err) {
                        dev_dbg(hdl, "DMA not accessible, using IRQ mode", 0);
                        hdl->mode = _SDIO_MODE_IRQ;
                        err       = 0;
                }
        }

        hdl->hsd.Instance                 = SDMMC[hdl->major].instance;
        hdl->hsd.Init.ClockEdge           = SDMMC[hdl->major].clock_edge;
        hdl->hsd.Init.ClockBypass         = SDIO_CLOCK_BYPASS_DISABLE;
        hdl->hsd.Init.ClockPowerSave      = SDMMC[hdl->major].clock_power_save;
        hdl->hsd.Init.BusWide             = SDIO_BUS_WIDE_1B;
        hdl->hsd.Init.HardwareFlowControl = SDMMC[hdl->major].hardware_flow_ctrl;
        hdl->hsd.Init.ClockDiv            = SDMMC[hdl->major].clock_div;

        for (int i = 0; i < 3; i++) {
                HAL_SD_DeInit(&hdl->hsd);

                err = HAL_SD_Init(&hdl->hsd);
                if (!err) {

                        const char *mode = "?";
                        switch (hdl->mode) {
                        case _SDIO_MODE_POLLING:
                                mode = "polling";
                                break;
                        case _SDIO_MODE_IRQ:
                                mode = "IRQ";
                                break;
                        case _SDIO_MODE_DMA:
                                mode = "DMA";
                                break;
                        }
                        dev_dbg(hdl, "using %s mode", mode);

                        if (SDMMC[hdl->major].bus_wide != SDIO_BUS_WIDE_1B) {
                                if (HAL_SD_ConfigWideBusOperation(&hdl->hsd,
                                            SDIO_BUS_WIDE_4B) == SD_HAL_OK) {
                                        dev_dbg(hdl, "switched to 4-bit bus", 0);
                                } else {
                                        dev_dbg(hdl, "4-bit bus not supported", 0);
                                }
                        }

                        HAL_SD_CardInfoTypeDef info;
                        err = HAL_SD_GetCardInfo(&hdl->hsd, &info);
                        if (!err) {

                                dev_dbg(hdl, "found SD%s card",
                                        info.CardType == CARD_SDSC ? "SC" : "HC/XC");

                                dev_dbg(hdl, "%d %u-byte logical blocks", info.BlockNbr,
                                        info.BlockSize);

                                hdl->card_size = cast(u64_t, info.BlockSize) *
                                        cast(u64_t, info.BlockNbr);
                        }
                }

                if (err) {
                        dev_dbg(hdl, "error code: %08Xh", hdl->hsd.ErrorCode);
                        HAL_SD_DeInit(&hdl->hsd);
                } else {
                        break;
                }
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
static int card_wait_ready(SDIO_t *hdl)
{
        int err = 0;

        u64_t timeout = sys_get_uptime_ms() +
                max(SDMMC[hdl->major].card_timeout / 10, 100);

        HAL_SD_CardStateTypeDef state;
        while ((state = HAL_SD_GetCardState(&hdl->hsd)) != HAL_SD_CARD_TRANSFER) {

                sys_sleep_ms(1);

                if (sys_get_uptime_ms() >= timeout) {
                        err = ETIME;
                        break;
                }
        }

        return err;
}

//==============================================================================
/**
 * @brief  Check card ereror.
 *
 * @param  hdl          device handle
 *
 * @return One of errno value (errno.h).
 */
//==============================================================================
static int card_check_error(SDIO_t *hdl)
{
        u32_t hal_err = HAL_SD_GetError(&hdl->hsd);
        if (hal_err == HAL_SD_ERROR_NONE) {
                return 0;
        }

        int err = EIO;

        if (hal_err & HAL_SD_ERROR_CMD_CRC_FAIL) {
                dev_dbg(hdl, "Command response received (but CRC check failed)", 0);
        }

        if (hal_err & HAL_SD_ERROR_DATA_CRC_FAIL) {
                dev_dbg(hdl, "Data block sent/received (CRC check failed)", 0);
        }

        if (hal_err & HAL_SD_ERROR_CMD_RSP_TIMEOUT) {
                dev_dbg(hdl, "Command response timeout", 0);
                err = ETIME;
        }

        if (hal_err & HAL_SD_ERROR_DATA_TIMEOUT) {
                dev_dbg(hdl, "Data timeout", 0);
                err = ETIME;
        }

        if (hal_err & HAL_SD_ERROR_TX_UNDERRUN) {
                dev_dbg(hdl, "Transmit FIFO underrun", 0);
        }

        if (hal_err & HAL_SD_ERROR_RX_OVERRUN) {
                dev_dbg(hdl, "Receive FIFO overrun", 0);
        }

        if (hal_err & HAL_SD_ERROR_ADDR_MISALIGNED) {
                dev_dbg(hdl, "Misaligned address", 0);
        }

        if (hal_err & HAL_SD_ERROR_BLOCK_LEN_ERR) {
                dev_dbg(hdl,
                        "Transferred block length is not allowed for the card or "
                        "the number of transferred bytes does not match the block "
                        "length",
                        0);
        }

        if (hal_err & HAL_SD_ERROR_ERASE_SEQ_ERR) {
                dev_dbg(hdl, "An error in the sequence of erase command occurs", 0);
        }

        if (hal_err & HAL_SD_ERROR_BAD_ERASE_PARAM) {
                dev_dbg(hdl, "An invalid selection for erase groups", 0);
        }

        if (hal_err & HAL_SD_ERROR_WRITE_PROT_VIOLATION) {
                dev_dbg(hdl, "Attempt to program a write protect block", 0);
        }

        if (hal_err & HAL_SD_ERROR_LOCK_UNLOCK_FAILED) {
                dev_dbg(hdl,
                        "Sequence or password error has been detected in unlock "
                        "command or if there was an attempt to access a locked card",
                        0);
        }

        if (hal_err & HAL_SD_ERROR_COM_CRC_FAILED) {
                dev_dbg(hdl, "CRC check of the previous command failed", 0);
        }

        if (hal_err & HAL_SD_ERROR_ILLEGAL_CMD) {
                dev_dbg(hdl, "Command is not legal for the card state ", 0);
        }
        if (hal_err & HAL_SD_ERROR_CARD_ECC_FAILED) {
                dev_dbg(hdl,
                        "Card internal ECC was applied but failed to correct the "
                        "data",
                        0);
        }
        if (hal_err & HAL_SD_ERROR_CC_ERR) {
                dev_dbg(hdl, "Internal card controller error", 0);
        }

        if (hal_err & HAL_SD_ERROR_GENERAL_UNKNOWN_ERR) {
                dev_dbg(hdl, "General or unknown error", 0);
        }

        if (hal_err & HAL_SD_ERROR_STREAM_READ_UNDERRUN) {
                dev_dbg(hdl, "The card could not sustain data reading in stream rmode",
                        0);
        }

        if (hal_err & HAL_SD_ERROR_STREAM_WRITE_OVERRUN) {
                dev_dbg(hdl,
                        "The card could not sustain data programming in stream "
                        "mode",
                        0);
        }

        if (hal_err & HAL_SD_ERROR_CID_CSD_OVERWRITE) {
                dev_dbg(hdl, "CID/CSD overwrite error", 0);
        }

        if (hal_err & HAL_SD_ERROR_WP_ERASE_SKIP) {
                dev_dbg(hdl, "Only partial address space was erased", 0);
        }

        if (hal_err & HAL_SD_ERROR_CARD_ECC_DISABLED) {
                dev_dbg(hdl, "Command has been executed without using internal ECC", 0);
        }

        if (hal_err & HAL_SD_ERROR_ERASE_RESET) {
                dev_dbg(hdl,
                        "Erase sequence was cleared before executing because an "
                        "out of erase sequence command was received",
                        0);
        }

        if (hal_err & HAL_SD_ERROR_AKE_SEQ_ERR) {
                dev_dbg(hdl, "Error in sequence of authentication", 0);
        }

        if (hal_err & HAL_SD_ERROR_INVALID_VOLTRANGE) {
                dev_dbg(hdl, "Error in case of invalid voltage range", 0);
        }

        if (hal_err & HAL_SD_ERROR_ADDR_OUT_OF_RANGE) {
                dev_dbg(hdl, "Error when addressed block is out of range", 0);
        }

        if (hal_err & HAL_SD_ERROR_REQUEST_NOT_APPLICABLE) {
                dev_dbg(hdl, "Error when command request is not applicable", 0);
        }

        if (hal_err & HAL_SD_ERROR_PARAM) {
                dev_dbg(hdl, "the used parameter is not valid", 0);
        }

        if (hal_err & HAL_SD_ERROR_UNSUPPORTED_FEATURE) {
                dev_dbg(hdl, "Error when feature is not insupported", 0);
        }

        if (hal_err & HAL_SD_ERROR_BUSY) {
                dev_dbg(hdl, "Error when transfer process is busy", 0);
                err = EBUSY;
        }

        if (hal_err & HAL_SD_ERROR_DMA) {
                dev_dbg(hdl, "Error while DMA transfer", 0);
        }

        if (hal_err & HAL_SD_ERROR_TIMEOUT) {
                dev_dbg(hdl, "Timeout error", 0);
                err = ETIME;
        }

        return err;
}

//==============================================================================
/**
 * @brief  Rx complete callback.
 *
 * @param  hsd          SD handle
 */
//==============================================================================
void HAL_SD_RxCpltCallback(SD_HandleTypeDef *hsd)
{
        SDIO_t *hdl = hsd->CtxPtr;
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
        SDIO_t *hdl = hsd->CtxPtr;
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
        SDIO_t *hdl = hsd->CtxPtr;
        sys_semaphore_signal_from_ISR(hdl->sem_xfer_complete, &hdl->irq_yield);
}

//==============================================================================
/**
 * @brief  SDMMC1 IRQ handler.
 */
//==============================================================================
#if defined(SDIO) && (__SDIO_CFG_SDMMC1_MODE__ != _SDIO_MODE_POLLING)
void SDIO_IRQHandler(void)
{
        if (sdio[SD1]) {
                HAL_SD_IRQHandler(&sdio[SD1]->hsd);
        }

        sys_thread_yield_from_ISR(&sdio[SD1]->irq_yield);
}
#endif

/*==============================================================================
  End of file
==============================================================================*/
