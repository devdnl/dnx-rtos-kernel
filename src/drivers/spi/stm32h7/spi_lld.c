/*==============================================================================
File    spi_lld.c

Author  Daniel Zorychta

Brief   SPI driver.

        Copyright (C) 2024 Daniel Zorychta <daniel.zorychta@gmail.com>

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

#pragma GCC optimize("Og")

/*==============================================================================
  Include files
==============================================================================*/
#include "drivers/driver.h"
#include "spi_cfg.h"
#include "../spi_ioctl.h"
#include "../spi.h"

#include "stm32h7/stm32h7xx.h"
#include "stm32h7/dma_ddi.h"
#include "stm32h7xx_hal_spi.h"

/*==============================================================================
  Local macros
==============================================================================*/

/*==============================================================================
  Local object types
==============================================================================*/
typedef enum {
        _SPI_MODE_POLLING = 0,
        _SPI_MODE_IRQ     = 1,
        _SPI_MODE_DMA     = 2,
} spi_mode_t;

/* SPI peripheral configuration */
struct SPI_info {
        SPI_TypeDef *const SPI;
        __IO u32_t        *APBRSTR;
        __IO u32_t        *APBENR;
        u32_t              APBRSTRENR;
        IRQn_Type          IRQn;
        spi_mode_t         mode;
        u8_t               DMA_major;
        u8_t               DMA_tx_channel;
        u8_t               DMA_rx_channel;
};

/*==============================================================================
  Local function prototypes
==============================================================================*/
static int register_DMA_tx_channel(struct SPI_slave *hdl);
static int register_DMA_rx_channel(struct SPI_slave *hdl);

/*==============================================================================
  Local objects
==============================================================================*/
MODULE_NAME(SPI);

/* SPI peripherals basic parameters */
static const struct SPI_info SPI_HW[_NUMBER_OF_SPI_PERIPHERALS] = {
#if defined(RCC_APB2ENR_SPI1EN)
        {
                .SPI            = SPI1,
                .APBENR         = &RCC->APB2ENR,
                .APBRSTR        = &RCC->APB2RSTR,
                .APBRSTRENR     = RCC_APB2ENR_SPI1EN,
                .IRQn           = SPI1_IRQn,
                .mode           = __SPI_SPI1_MODE__,
                .DMA_major      = _DMA_DDI_DMA1 | _DMA_DDI_DMA2,
                .DMA_tx_channel = _DMA_DDI_DMA1_2_REQUEST_SPI1_TX,
                .DMA_rx_channel = _DMA_DDI_DMA1_2_REQUEST_SPI1_RX,
        },
#endif
#if defined(RCC_APB1LENR_SPI2EN)
        {
                .SPI            = SPI2,
                .APBENR         = &RCC->APB1LENR,
                .APBRSTR        = &RCC->APB1LRSTR,
                .APBRSTRENR     = RCC_APB1LENR_SPI2EN,
                .IRQn           = SPI2_IRQn,
                .mode           = __SPI_SPI2_MODE__,
                .DMA_major      = _DMA_DDI_DMA1 | _DMA_DDI_DMA2,
                .DMA_tx_channel = _DMA_DDI_DMA1_2_REQUEST_SPI2_TX,
                .DMA_rx_channel = _DMA_DDI_DMA1_2_REQUEST_SPI2_RX,
        },
#endif
#if defined(RCC_APB1LENR_SPI3EN)
        {
                .SPI            = SPI3,
                .APBENR         = &RCC->APB1LENR,
                .APBRSTR        = &RCC->APB1LRSTR,
                .APBRSTRENR     = RCC_APB1LENR_SPI3EN,
                .IRQn           = SPI3_IRQn,
                .mode           = __SPI_SPI3_MODE__,
                .DMA_major      = _DMA_DDI_DMA1 | _DMA_DDI_DMA2,
                .DMA_tx_channel = _DMA_DDI_DMA1_2_REQUEST_SPI3_TX,
                .DMA_rx_channel = _DMA_DDI_DMA1_2_REQUEST_SPI3_RX,
        },
#endif
#if defined(RCC_APB2ENR_SPI4EN)
        {
                .SPI            = SPI4,
                .APBENR         = &RCC->APB2ENR,
                .APBRSTR        = &RCC->APB2RSTR,
                .APBRSTRENR     = RCC_APB2ENR_SPI4EN,
                .IRQn           = SPI4_IRQn,
                .mode           = __SPI_SPI4_MODE__,
                .DMA_major      = _DMA_DDI_DMA1 | _DMA_DDI_DMA2,
                .DMA_tx_channel = _DMA_DDI_DMA1_2_REQUEST_SPI4_TX,
                .DMA_rx_channel = _DMA_DDI_DMA1_2_REQUEST_SPI4_RX,
        },
#endif
#if defined(RCC_APB2ENR_SPI5EN)
        {
                .SPI            = SPI5,
                .APBENR         = &RCC->APB2ENR,
                .APBRSTR        = &RCC->APB2RSTR,
                .APBRSTRENR     = RCC_APB2ENR_SPI5EN,
                .IRQn           = SPI5_IRQn,
                .mode           = __SPI_SPI5_MODE__,
                .DMA_major      = _DMA_DDI_DMA1 | _DMA_DDI_DMA2,
                .DMA_tx_channel = _DMA_DDI_DMA1_2_REQUEST_SPI5_TX,
                .DMA_rx_channel = _DMA_DDI_DMA1_2_REQUEST_SPI5_RX,
        },
#endif
#if defined(RCC_APB4ENR_SPI6EN)
        {
                .SPI            = SPI6,
                .APBENR         = &RCC->APB4ENR,
                .APBRSTR        = &RCC->APB4RSTR,
                .APBRSTRENR     = RCC_APB4ENR_SPI6EN,
                .IRQn           = SPI6_IRQn,
                .mode           = __SPI_SPI6_MODE__,
                .DMA_major      = _DMA_DDI_BDMA,
                .DMA_tx_channel = _DMA_DDI_BDMA_REQUEST_SPI6_TX,
                .DMA_rx_channel = _DMA_DDI_BDMA_REQUEST_SPI6_RX,
        },
#endif
};

/*==============================================================================
  Exported objects
==============================================================================*/

/*==============================================================================
  Function definitions
==============================================================================*/

//==============================================================================
/**
 * @brief Function enable SPI interface
 * @param[in] major     SPI major number
 * @return One of errno value.
 */
//==============================================================================
int _SPI_LLD__turn_on(u8_t major)
{
        if (!(*SPI_HW[major].APBENR & SPI_HW[major].APBRSTRENR)) {

                SET_BIT(*SPI_HW[major].APBRSTR, SPI_HW[major].APBRSTRENR);
                CLEAR_BIT(*SPI_HW[major].APBRSTR, SPI_HW[major].APBRSTRENR);
                SET_BIT(*SPI_HW[major].APBENR, SPI_HW[major].APBRSTRENR);

                NVIC_EnableIRQ(SPI_HW[major].IRQn);
                NVIC_SetPriority(SPI_HW[major].IRQn, _CPU_IRQ_SAFE_PRIORITY_);

                return ESUCC;
        } else {
                return EADDRINUSE;
        }
}

//==============================================================================
/**
 * @brief Function disable SPI interface
 * @param[in] major     SPI major number
 * @return None
 */
//==============================================================================
void _SPI_LLD__turn_off(u8_t major)
{
        if (*SPI_HW[major].APBENR & SPI_HW[major].APBRSTRENR) {
                SET_BIT(*SPI_HW[major].APBRSTR, SPI_HW[major].APBRSTRENR);
                CLEAR_BIT(*SPI_HW[major].APBRSTR, SPI_HW[major].APBRSTRENR);
                CLEAR_BIT(*SPI_HW[major].APBENR, SPI_HW[major].APBRSTRENR);

                NVIC_DisableIRQ(SPI_HW[major].IRQn);
        }
}

//==============================================================================
/**
 * @brief Function apply new configuration for selected SPI
 *
 * @param hdl           SPI slave
 */
//==============================================================================
void _SPI_LLD__apply_config(struct SPI_slave *hdl)
{
        static const typeof(SPI_BAUDRATEPRESCALER_2) divider_mask[] = {
                [SPI_CLK_DIV__2]   = SPI_BAUDRATEPRESCALER_2,
                [SPI_CLK_DIV__4]   = SPI_BAUDRATEPRESCALER_4,
                [SPI_CLK_DIV__8]   = SPI_BAUDRATEPRESCALER_8,
                [SPI_CLK_DIV__16]  = SPI_BAUDRATEPRESCALER_16,
                [SPI_CLK_DIV__32]  = SPI_BAUDRATEPRESCALER_32,
                [SPI_CLK_DIV__64]  = SPI_BAUDRATEPRESCALER_64,
                [SPI_CLK_DIV__128] = SPI_BAUDRATEPRESCALER_128,
                [SPI_CLK_DIV__256] = SPI_BAUDRATEPRESCALER_256,
        };

        static const typeof(SPI_POLARITY_LOW) clk_polarity[] = {
                [SPI_MODE__0] = SPI_POLARITY_LOW,
                [SPI_MODE__1] = SPI_POLARITY_LOW,
                [SPI_MODE__2] = SPI_POLARITY_HIGH,
                [SPI_MODE__3] = SPI_POLARITY_HIGH,
        };

        static const typeof(SPI_PHASE_1EDGE) clk_phase[] = {
                [SPI_MODE__0] = SPI_PHASE_1EDGE,
                [SPI_MODE__1] = SPI_PHASE_2EDGE,
                [SPI_MODE__2] = SPI_PHASE_1EDGE,
                [SPI_MODE__3] = SPI_PHASE_2EDGE,
        };

        SPI_HandleTypeDef *hspi = &_SPI[hdl->major]->hspi;
        hspi->major             = hdl->major;
        hspi->Instance          = SPI_HW[hdl->major].SPI;

        hspi->Init.Mode                       = SPI_MODE_MASTER;
        hspi->Init.Direction                  = SPI_DIRECTION_2LINES;
        hspi->Init.DataSize                   = SPI_DATASIZE_8BIT;
        hspi->Init.CLKPolarity                = clk_polarity[hdl->config.mode];
        hspi->Init.CLKPhase                   = clk_phase[hdl->config.mode];
        hspi->Init.NSS                        = SPI_NSS_SOFT;
        hspi->Init.BaudRatePrescaler          = divider_mask[hdl->config.clk_divider];
        hspi->Init.CRCCalculation             = SPI_CRCCALCULATION_DISABLE;
        hspi->Init.CRCPolynomial              = 7;
        hspi->Init.CRCLength                  = SPI_CRC_LENGTH_DATASIZE;
        hspi->Init.NSSPMode                   = SPI_NSS_PULSE_DISABLE;
        hspi->Init.NSSPolarity                = SPI_NSS_POLARITY_LOW;
        hspi->Init.FifoThreshold              = SPI_FIFO_THRESHOLD_01DATA;
        hspi->Init.TxCRCInitializationPattern = SPI_CRC_INITIALIZATION_ALL_ZERO_PATTERN;
        hspi->Init.RxCRCInitializationPattern = SPI_CRC_INITIALIZATION_ALL_ZERO_PATTERN;
        hspi->Init.MasterSSIdleness           = SPI_MASTER_SS_IDLENESS_00CYCLE;
        hspi->Init.MasterInterDataIdleness    = SPI_MASTER_INTERDATA_IDLENESS_00CYCLE;
        hspi->Init.MasterReceiverAutoSusp     = SPI_MASTER_RX_AUTOSUSP_DISABLE;
        hspi->Init.MasterKeepIOState          = SPI_MASTER_KEEP_IO_STATE_DISABLE;
        hspi->Init.IOSwap                     = SPI_IO_SWAP_DISABLE;
        hspi->Init.FirstBit = hdl->config.msb_first ? SPI_FIRSTBIT_MSB : SPI_FIRSTBIT_LSB;
        hspi->Init.TIMode   = SPI_TIMODE_DISABLE;

        HAL_SPI_DeInit(hspi);

        if (HAL_SPI_Init(hspi) != 0) {
                dev_dbg(hdl, "initialization error!");
        }

        register_DMA_tx_channel(hdl);
        register_DMA_rx_channel(hdl);
}

//==============================================================================
/**
 * @brief Function apply SPI device safe configuration
 *
 * @param major         SPI major number
 */
//==============================================================================
void _SPI_LLD__halt(u8_t major)
{
        HAL_SPI_DeInit(&_SPI[major]->hspi);
}

//==============================================================================
/**
 * @brief  Transceive data by using IRQs or DMA
 * @param  hdl          virtual SPI handler
 * @param  txbuf        source buffer (can be NULL for RX only)
 * @param  rxbuf        destination buffer (can be NULL for TX only)
 * @param  count        number of bytes to transfer
 * @return One of errno value
 */
//==============================================================================
int _SPI_LLD__transceive(struct SPI_slave *hdl, const u8_t *txbuf, u8_t *rxbuf,
        size_t count)
{
        _SPI[hdl->major]->current_slave = hdl;

        spi_mode_t mode = SPI_HW[hdl->major].mode;

        if ((count < 16) or (txbuf and not sys_is_mem_dma_capable(txbuf)) or
                (rxbuf and not sys_is_mem_dma_capable(rxbuf)) or
                not _SPI[hdl->major]->hspi.hdmatx.dmad or
                not _SPI[hdl->major]->hspi.hdmarx.dmad) {

                if (mode == _SPI_MODE_DMA) {
                        mode = _SPI_MODE_IRQ;
                }
        }

        int err = EINVAL;

        if (mode == _SPI_MODE_POLLING) {

                if (txbuf and rxbuf) {
                        err = HAL_SPI_TransmitReceive(&_SPI[hdl->major]->hspi, txbuf,
                                rxbuf, count, _SPI_IRQ_WAIT_TIMEOUT);

                } else if (txbuf) {
                        err = HAL_SPI_Transmit(&_SPI[hdl->major]->hspi, txbuf, count,
                                _SPI_IRQ_WAIT_TIMEOUT);

                } else if (rxbuf) {
                        err = HAL_SPI_Receive(&_SPI[hdl->major]->hspi, rxbuf, count,
                                _SPI_IRQ_WAIT_TIMEOUT);
                }

        } else if (mode == _SPI_MODE_IRQ) {

                if (txbuf and rxbuf) {
                        err = HAL_SPI_TransmitReceive_IT(&_SPI[hdl->major]->hspi, txbuf,
                                rxbuf, count);

                } else if (txbuf) {
                        err = HAL_SPI_Transmit_IT(&_SPI[hdl->major]->hspi, txbuf, count);

                } else if (rxbuf) {
                        err = HAL_SPI_Receive_IT(&_SPI[hdl->major]->hspi, rxbuf, count);
                }

                if (not err) {
                        err = sys_semaphore_wait(_SPI[hdl->major]->wait_irq_sem,
                                _SPI_IRQ_WAIT_TIMEOUT);
                }

        } else if (mode == _SPI_MODE_DMA) {

                if (txbuf and rxbuf) {
                        err = HAL_SPI_TransmitReceive_DMA(&_SPI[hdl->major]->hspi, txbuf,
                                rxbuf, count);

                } else if (txbuf) {
                        err = HAL_SPI_Transmit_DMA(&_SPI[hdl->major]->hspi, txbuf, count);

                } else if (rxbuf) {
                        err = HAL_SPI_Receive_DMA(&_SPI[hdl->major]->hspi, rxbuf, count);
                }

                if (not err) {
                        err = sys_semaphore_wait(_SPI[hdl->major]->wait_irq_sem,
                                _SPI_IRQ_WAIT_TIMEOUT);
                }

        } else {
                dev_dbg(hdl, "unknown mode!");
        }

        return err;
}

//==============================================================================
/**
 * @brief  Register DMA TX channel.
 *
 * @param  hdl          device handle
 *
 * @return One of errno value.
 */
//==============================================================================
static int register_DMA_tx_channel(struct SPI_slave *hdl)
{
        DMA_HandleTypeDef *hdma = &_SPI[hdl->major]->hspi.hdmatx;

        if (hdma->dmad) {
                return 0;
        }

        hdma->dmad = _DMA_DDI_reserve(SPI_HW[hdl->major].DMA_major);
        if (not hdma->dmad) {
                dev_dbg(hdl, "no free DMA tx channel");
                return EFAULT;
        }

        hdma->Parent                     = &_SPI[hdl->major]->hspi;
        hdma->Config.user_ctx            = &_SPI[hdl->major]->hspi;
        hdma->Config.cb_finish           = NULL;
        hdma->Config.cb_half             = NULL;
        hdma->Config.cb_next             = NULL;
        hdma->Config.data_number         = 0; // set by transfer function
        hdma->Config.peripheral_address  = cast(u32_t, &SPI_HW[hdl->major].SPI->TXDR);
        hdma->Config.memory_address[0]   = 0; // set by transfer function
        hdma->Config.memory_address[1]   = 0;
        hdma->Config.IRQ_priority        = __CPU_DEFAULT_IRQ_PRIORITY__;
        hdma->Config.channel             = SPI_HW[hdl->major].DMA_tx_channel;
        hdma->Config.release             = false;
        hdma->Config.memory_burst        = _DMA_DDI_MEMORY_BURST_4_BEATS;
        hdma->Config.peripheral_burst    = _DMA_DDI_PERIPHERAL_BURST_SINGLE_TRANSFER;
        hdma->Config.bufferable_transfer = _DMA_DDI_BUFFERABLE_TRANSFER_DISABLED;
        hdma->Config.double_buffer_mode  = _DMA_DDI_DOUBLE_BUFFER_MODE_DISABLED;
        hdma->Config.priority_level      = _DMA_DDI_PRIORITY_LEVEL_MEDIUM;
        hdma->Config.peripheral_increment_offset =
                _DMA_DDI_PERIPHERAL_INCREMENT_OFFSET_ACCORDING_TO_PERIPHERAL_SIZE;
        hdma->Config.memory_data_size     = _DMA_DDI_MEMORY_DATA_SIZE_BYTE;
        hdma->Config.peripheral_data_size = _DMA_DDI_PERIPHERAL_DATA_SIZE_BYTE;
        hdma->Config
                .memory_address_increment = _DMA_DDI_MEMORY_ADDRESS_POINTER_INCREMENTED;
        hdma->Config.peripheral_address_increment =
                _DMA_DDI_PERIPHERAL_ADDRESS_POINTER_IS_FIXED;
        hdma->Config.circular_mode = _DMA_DDI_CIRCULAR_MODE_DISABLED;
        hdma->Config
                .transfer_direction  = _DMA_DDI_TRANSFER_DIRECTION_MEMORY_TO_PERIPHERAL;
        hdma->Config.flow_controller = _DMA_DDI_FLOW_CONTROLLER_DMA;
        hdma->Config.mode            = _DMA_DDI_MODE_FIFO;
        hdma->Config.fifo_threshold  = _DMA_DDI_FIFO_THRESHOLD_FULL;

        return 0;
}

//==============================================================================
/**
 * @brief  Register DMA TX channel.
 *
 * @param  hdl          device handle
 *
 * @return One of errno value.
 */
//==============================================================================
static int register_DMA_rx_channel(struct SPI_slave *hdl)
{
        DMA_HandleTypeDef *hdma = &_SPI[hdl->major]->hspi.hdmarx;

        if (hdma->dmad) {
                return 0;
        }

        hdma->dmad = _DMA_DDI_reserve(SPI_HW[hdl->major].DMA_major);
        if (not hdma->dmad) {
                dev_dbg(hdl, "no free DMA rx channel");
                return EFAULT;
        }

        hdma->Parent                     = &_SPI[hdl->major]->hspi;
        hdma->Config.user_ctx            = &_SPI[hdl->major]->hspi;
        hdma->Config.cb_finish           = NULL;
        hdma->Config.cb_half             = NULL;
        hdma->Config.cb_next             = NULL;
        hdma->Config.data_number         = 0; // set by transfer function
        hdma->Config.peripheral_address  = cast(u32_t, &SPI_HW[hdl->major].SPI->RXDR);
        hdma->Config.memory_address[0]   = 0; // set by transfer function
        hdma->Config.memory_address[1]   = 0;
        hdma->Config.IRQ_priority        = __CPU_DEFAULT_IRQ_PRIORITY__;
        hdma->Config.channel             = SPI_HW[hdl->major].DMA_rx_channel;
        hdma->Config.release             = false;
        hdma->Config.memory_burst        = _DMA_DDI_MEMORY_BURST_4_BEATS;
        hdma->Config.peripheral_burst    = _DMA_DDI_PERIPHERAL_BURST_SINGLE_TRANSFER;
        hdma->Config.bufferable_transfer = _DMA_DDI_BUFFERABLE_TRANSFER_DISABLED;
        hdma->Config.double_buffer_mode  = _DMA_DDI_DOUBLE_BUFFER_MODE_DISABLED;
        hdma->Config.priority_level      = _DMA_DDI_PRIORITY_LEVEL_MEDIUM;
        hdma->Config.peripheral_increment_offset =
                _DMA_DDI_PERIPHERAL_INCREMENT_OFFSET_ACCORDING_TO_PERIPHERAL_SIZE;
        hdma->Config.memory_data_size     = _DMA_DDI_MEMORY_DATA_SIZE_BYTE;
        hdma->Config.peripheral_data_size = _DMA_DDI_PERIPHERAL_DATA_SIZE_BYTE;
        hdma->Config
                .memory_address_increment = _DMA_DDI_MEMORY_ADDRESS_POINTER_INCREMENTED;
        hdma->Config.peripheral_address_increment =
                _DMA_DDI_PERIPHERAL_ADDRESS_POINTER_IS_FIXED;
        hdma->Config.circular_mode = _DMA_DDI_CIRCULAR_MODE_DISABLED;
        hdma->Config
                .transfer_direction  = _DMA_DDI_TRANSFER_DIRECTION_PERIPHERAL_TO_MEMORY;
        hdma->Config.flow_controller = _DMA_DDI_FLOW_CONTROLLER_DMA;
        hdma->Config.mode            = _DMA_DDI_MODE_FIFO;
        hdma->Config.fifo_threshold  = _DMA_DDI_FIFO_THRESHOLD_1_2;

        return 0;
}

//==============================================================================
/**
 * @brief Tx and Rx Transfer completed callback.
 *
 * @param  hspi: pointer to a SPI_HandleTypeDef structure that contains
 *               the configuration information for SPI module.
 *
 * @retval None
 */
//==============================================================================
void HAL_SPI_TxRxCpltCallback(SPI_HandleTypeDef *hspi)
{
        sys_semaphore_signal_from_ISR(_SPI[hspi->major]->wait_irq_sem,
                &_SPI[hspi->major]->irq_yield);
}

//==============================================================================
/**
 * @brief Tx Transfer completed callback.
 *
 * @param  hspi: pointer to a SPI_HandleTypeDef structure that contains
 *               the configuration information for SPI module.
 *
 * @retval None
 */
//==============================================================================
void HAL_SPI_TxCpltCallback(SPI_HandleTypeDef *hspi)
{
        sys_semaphore_signal_from_ISR(_SPI[hspi->major]->wait_irq_sem,
                &_SPI[hspi->major]->irq_yield);
}

//==============================================================================
/**
 * @brief Rx Transfer completed callback.
 *
 * @param  hspi: pointer to a SPI_HandleTypeDef structure that contains
 *               the configuration information for SPI module.
 *
 * @retval None
 */
//==============================================================================
void HAL_SPI_RxCpltCallback(SPI_HandleTypeDef *hspi)
{
        sys_semaphore_signal_from_ISR(_SPI[hspi->major]->wait_irq_sem,
                &_SPI[hspi->major]->irq_yield);
}

#if defined(RCC_APB2ENR_SPI1EN)
//==============================================================================
/**
 * @brief SPI1 IRQ handler
 */
//==============================================================================
void SPI1_IRQHandler(void)
{
        HAL_SPI_IRQHandler(&_SPI[_SPI1]->hspi);

        sys_thread_yield_from_ISR(_SPI[_SPI1]->irq_yield);
}
#endif

//==============================================================================

#if defined(RCC_APB1LENR_SPI2EN)
//==============================================================================
/**
 * @brief SPI2 IRQ handler
 */
//==============================================================================
void SPI2_IRQHandler(void)
{
        HAL_SPI_IRQHandler(&_SPI[_SPI2]->hspi);

        sys_thread_yield_from_ISR(_SPI[_SPI2]->irq_yield);
}
#endif

#if defined(RCC_APB1LENR_SPI3EN)
//==============================================================================
/**
 * @brief SPI3 IRQ handler
 */
//==============================================================================
void SPI3_IRQHandler(void)
{
        HAL_SPI_IRQHandler(&_SPI[_SPI3]->hspi);

        sys_thread_yield_from_ISR(_SPI[_SPI3]->irq_yield);
}
#endif

#if defined(RCC_APB2ENR_SPI4EN)
//==============================================================================
/**
 * @brief SPI4 IRQ handler
 */
//==============================================================================
void SPI4_IRQHandler(void)
{
        HAL_SPI_IRQHandler(&_SPI[_SPI4]->hspi);

        sys_thread_yield_from_ISR(_SPI[_SPI4]->irq_yield);
}
#endif

#if defined(RCC_APB2ENR_SPI5EN)
//==============================================================================
/**
 * @brief SPI5 IRQ handler
 */
//==============================================================================
void SPI5_IRQHandler(void)
{
        HAL_SPI_IRQHandler(&_SPI[_SPI5]->hspi);

        sys_thread_yield_from_ISR(_SPI[_SPI5]->irq_yield);
}
#endif

#if defined(RCC_APB4ENR_SPI6EN)
//==============================================================================
/**
 * @brief SPI6 IRQ handler
 */
//==============================================================================
void SPI6_IRQHandler(void)
{
        HAL_SPI_IRQHandler(&_SPI[_SPI6]->hspi);

        sys_thread_yield_from_ISR(_SPI[_SPI6]->irq_yield);
}
#endif

/*==============================================================================
  End of file
==============================================================================*/
