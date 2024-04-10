/*=========================================================================*//**
@file    dma_ddi.h

@author  Daniel Zorychta

@brief   DMA Driver Direct Interface.

@note    Copyright (C) 2020 Daniel Zorychta <daniel.zorychta@gmail.com>

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


*//*==========================================================================*/

/**
 * @defgroup drv-dma-ddi DMA Driver Direct Interface.
 *
 * \section drv-dma-ddi-desc Description
 * DMA Driver Direct Interface. This interface can be used only from driver
 * level. Interface is created to provide common low-level functions that
 * handles basic microcontroller functionality. The interface of this driver
 * is not common for all architectures.
 *
 * \section drv-dma-sup-arch Supported architectures
 * \li stm32f4
 *
 * @todo Details
 *
 *
 * @{
 */

#ifndef _DMA_DDI_H_
#define _DMA_DDI_H_

/*==============================================================================
  Include files
==============================================================================*/
#include "config.h"
#if defined(ARCH_stm32f4) || defined(ARCH_stm32f7) || defined(ARCH_stm32h7)

#include "lib/sys/types.h"
#if defined(ARCH_stm32f4)
#include "stm32f4/stm32f4xx.h"
#elif defined(ARCH_stm32f7)
#include "stm32f7/stm32f7xx.h"
#elif defined(ARCH_stm32h7)
#include "stm32h7/stm32h7xx.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

/*==============================================================================
  Exported macros
==============================================================================*/
#define DMA_SR_FEIF  DMA_LISR_FEIF0
#define DMA_SR_DMEIF DMA_LISR_DMEIF0
#define DMA_SR_TEIF  DMA_LISR_TEIF0
#define DMA_SR_HTIF  DMA_LISR_HTIF0
#define DMA_SR_TCIF  DMA_LISR_TCIF0

#if defined(ARCH_stm32f4) || defined(ARCH_stm32f7)
#define _DMA_DDI_DMA1 0
#define _DMA_DDI_DMA2 1
#elif defined(ARCH_stm32h7)
#define _DMA_DDI_DMA1                         (1 << 0)
#define _DMA_DDI_DMA2                         (1 << 1)
#define _DMA_DDI_BDMA                         (1 << 2)

/* DMAMUX1 requests */
#define _DMA_DDI_DMA1_2_REQUEST_MEM2MEM       0U
#define _DMA_DDI_DMA1_2_REQUEST_GENERATOR0    1U
#define _DMA_DDI_DMA1_2_REQUEST_GENERATOR1    2U
#define _DMA_DDI_DMA1_2_REQUEST_GENERATOR2    3U
#define _DMA_DDI_DMA1_2_REQUEST_GENERATOR3    4U
#define _DMA_DDI_DMA1_2_REQUEST_GENERATOR4    5U
#define _DMA_DDI_DMA1_2_REQUEST_GENERATOR5    6U
#define _DMA_DDI_DMA1_2_REQUEST_GENERATOR6    7U
#define _DMA_DDI_DMA1_2_REQUEST_GENERATOR7    8U
#define _DMA_DDI_DMA1_2_REQUEST_ADC1          9U
#define _DMA_DDI_DMA1_2_REQUEST_ADC2          10U
#define _DMA_DDI_DMA1_2_REQUEST_TIM1_CH1      11U
#define _DMA_DDI_DMA1_2_REQUEST_TIM1_CH2      12U
#define _DMA_DDI_DMA1_2_REQUEST_TIM1_CH3      13U
#define _DMA_DDI_DMA1_2_REQUEST_TIM1_CH4      14U
#define _DMA_DDI_DMA1_2_REQUEST_TIM1_UP       15U
#define _DMA_DDI_DMA1_2_REQUEST_TIM1_TRIG     16U
#define _DMA_DDI_DMA1_2_REQUEST_TIM1_COM      17U
#define _DMA_DDI_DMA1_2_REQUEST_TIM2_CH1      18U
#define _DMA_DDI_DMA1_2_REQUEST_TIM2_CH2      19U
#define _DMA_DDI_DMA1_2_REQUEST_TIM2_CH3      20U
#define _DMA_DDI_DMA1_2_REQUEST_TIM2_CH4      21U
#define _DMA_DDI_DMA1_2_REQUEST_TIM2_UP       22U
#define _DMA_DDI_DMA1_2_REQUEST_TIM3_CH1      23U
#define _DMA_DDI_DMA1_2_REQUEST_TIM3_CH2      24U
#define _DMA_DDI_DMA1_2_REQUEST_TIM3_CH3      25U
#define _DMA_DDI_DMA1_2_REQUEST_TIM3_CH4      26U
#define _DMA_DDI_DMA1_2_REQUEST_TIM3_UP       27U
#define _DMA_DDI_DMA1_2_REQUEST_TIM3_TRIG     28U
#define _DMA_DDI_DMA1_2_REQUEST_TIM4_CH1      29U
#define _DMA_DDI_DMA1_2_REQUEST_TIM4_CH2      30U
#define _DMA_DDI_DMA1_2_REQUEST_TIM4_CH3      31U
#define _DMA_DDI_DMA1_2_REQUEST_TIM4_UP       32U
#define _DMA_DDI_DMA1_2_REQUEST_I2C1_RX       33U
#define _DMA_DDI_DMA1_2_REQUEST_I2C1_TX       34U
#define _DMA_DDI_DMA1_2_REQUEST_I2C2_RX       35U
#define _DMA_DDI_DMA1_2_REQUEST_I2C2_TX       36U
#define _DMA_DDI_DMA1_2_REQUEST_SPI1_RX       37U
#define _DMA_DDI_DMA1_2_REQUEST_SPI1_TX       38U
#define _DMA_DDI_DMA1_2_REQUEST_SPI2_RX       39U
#define _DMA_DDI_DMA1_2_REQUEST_SPI2_TX       40U
#define _DMA_DDI_DMA1_2_REQUEST_USART1_RX     41U
#define _DMA_DDI_DMA1_2_REQUEST_USART1_TX     42U
#define _DMA_DDI_DMA1_2_REQUEST_USART2_RX     43U
#define _DMA_DDI_DMA1_2_REQUEST_USART2_TX     44U
#define _DMA_DDI_DMA1_2_REQUEST_USART3_RX     45U
#define _DMA_DDI_DMA1_2_REQUEST_USART3_TX     46U
#define _DMA_DDI_DMA1_2_REQUEST_TIM8_CH1      47U
#define _DMA_DDI_DMA1_2_REQUEST_TIM8_CH2      48U
#define _DMA_DDI_DMA1_2_REQUEST_TIM8_CH3      49U
#define _DMA_DDI_DMA1_2_REQUEST_TIM8_CH4      50U
#define _DMA_DDI_DMA1_2_REQUEST_TIM8_UP       51U
#define _DMA_DDI_DMA1_2_REQUEST_TIM8_TRIG     52U
#define _DMA_DDI_DMA1_2_REQUEST_TIM8_COM      53U
#define _DMA_DDI_DMA1_2_REQUEST_TIM5_CH1      55U
#define _DMA_DDI_DMA1_2_REQUEST_TIM5_CH2      56U
#define _DMA_DDI_DMA1_2_REQUEST_TIM5_CH3      57U
#define _DMA_DDI_DMA1_2_REQUEST_TIM5_CH4      58U
#define _DMA_DDI_DMA1_2_REQUEST_TIM5_UP       59U
#define _DMA_DDI_DMA1_2_REQUEST_TIM5_TRIG     60U
#define _DMA_DDI_DMA1_2_REQUEST_SPI3_RX       61U
#define _DMA_DDI_DMA1_2_REQUEST_SPI3_TX       62U
#define _DMA_DDI_DMA1_2_REQUEST_UART4_RX      63U
#define _DMA_DDI_DMA1_2_REQUEST_UART4_TX      64U
#define _DMA_DDI_DMA1_2_REQUEST_UART5_RX      65U
#define _DMA_DDI_DMA1_2_REQUEST_UART5_TX      66U
#define _DMA_DDI_DMA1_2_REQUEST_DAC1_CH1      67U
#define _DMA_DDI_DMA1_2_REQUEST_DAC1_CH2      68U
#define _DMA_DDI_DMA1_2_REQUEST_TIM6_UP       69U
#define _DMA_DDI_DMA1_2_REQUEST_TIM7_UP       70U
#define _DMA_DDI_DMA1_2_REQUEST_USART6_RX     71U
#define _DMA_DDI_DMA1_2_REQUEST_USART6_TX     72U
#define _DMA_DDI_DMA1_2_REQUEST_I2C3_RX       73U
#define _DMA_DDI_DMA1_2_REQUEST_I2C3_TX       74U
#define _DMA_DDI_DMA1_2_REQUEST_DCMI_PSSI     75U
#define _DMA_DDI_DMA1_2_REQUEST_DCMI          _DMA_DDI_DMA1_2_REQUEST_DCMI_PSSI
#define _DMA_DDI_DMA1_2_REQUEST_CRYP_IN       76U
#define _DMA_DDI_DMA1_2_REQUEST_CRYP_OUT      77U
#define _DMA_DDI_DMA1_2_REQUEST_HASH_IN       78U
#define _DMA_DDI_DMA1_2_REQUEST_UART7_RX      79U
#define _DMA_DDI_DMA1_2_REQUEST_UART7_TX      80U
#define _DMA_DDI_DMA1_2_REQUEST_UART8_RX      81U
#define _DMA_DDI_DMA1_2_REQUEST_UART8_TX      82U
#define _DMA_DDI_DMA1_2_REQUEST_SPI4_RX       83U
#define _DMA_DDI_DMA1_2_REQUEST_SPI4_TX       84U
#define _DMA_DDI_DMA1_2_REQUEST_SPI5_RX       85U
#define _DMA_DDI_DMA1_2_REQUEST_SPI5_TX       86U
#define _DMA_DDI_DMA1_2_REQUEST_SAI1_A        87U
#define _DMA_DDI_DMA1_2_REQUEST_SAI1_B        88U
#define _DMA_DDI_DMA1_2_REQUEST_SAI2_A        89U
#define _DMA_DDI_DMA1_2_REQUEST_SAI2_B        90U
#define _DMA_DDI_DMA1_2_REQUEST_SWPMI_RX      91U
#define _DMA_DDI_DMA1_2_REQUEST_SWPMI_TX      92U
#define _DMA_DDI_DMA1_2_REQUEST_SPDIF_RX_DT   93U
#define _DMA_DDI_DMA1_2_REQUEST_SPDIF_RX_CS   94U
#define _DMA_DDI_DMA1_2_REQUEST_HRTIM_MASTER  95U
#define _DMA_DDI_DMA1_2_REQUEST_HRTIM_TIMER_A 96U
#define _DMA_DDI_DMA1_2_REQUEST_HRTIM_TIMER_B 97U
#define _DMA_DDI_DMA1_2_REQUEST_HRTIM_TIMER_C 98U
#define _DMA_DDI_DMA1_2_REQUEST_HRTIM_TIMER_D 99U
#define _DMA_DDI_DMA1_2_REQUEST_HRTIM_TIMER_E 100U
#define _DMA_DDI_DMA1_2_REQUEST_DFSDM1_FLT0   101U
#define _DMA_DDI_DMA1_2_REQUEST_DFSDM1_FLT1   102U
#define _DMA_DDI_DMA1_2_REQUEST_DFSDM1_FLT2   103U
#define _DMA_DDI_DMA1_2_REQUEST_DFSDM1_FLT3   104U
#define _DMA_DDI_DMA1_2_REQUEST_TIM15_CH1     105U
#define _DMA_DDI_DMA1_2_REQUEST_TIM15_UP      106U
#define _DMA_DDI_DMA1_2_REQUEST_TIM15_TRIG    107U
#define _DMA_DDI_DMA1_2_REQUEST_TIM15_COM     108U
#define _DMA_DDI_DMA1_2_REQUEST_TIM16_CH1     109U
#define _DMA_DDI_DMA1_2_REQUEST_TIM16_UP      110U
#define _DMA_DDI_DMA1_2_REQUEST_TIM17_CH1     111U
#define _DMA_DDI_DMA1_2_REQUEST_TIM17_UP      112U
#define _DMA_DDI_DMA1_2_REQUEST_SAI3_A        113U
#define _DMA_DDI_DMA1_2_REQUEST_SAI3_B        114U
#define _DMA_DDI_DMA1_2_REQUEST_ADC3          115U
#define _DMA_DDI_DMA1_2_REQUEST_UART9_RX      116U
#define _DMA_DDI_DMA1_2_REQUEST_UART9_TX      117U
#define _DMA_DDI_DMA1_2_REQUEST_USART10_RX    118U
#define _DMA_DDI_DMA1_2_REQUEST_USART10_TX    119U
#define _DMA_DDI_DMA1_2_REQUEST_FMAC_READ     120U
#define _DMA_DDI_DMA1_2_REQUEST_FMAC_WRITE    121U
#define _DMA_DDI_DMA1_2_REQUEST_CORDIC_READ   122U
#define _DMA_DDI_DMA1_2_REQUEST_CORDIC_WRITE  123U
#define _DMA_DDI_DMA1_2_REQUEST_I2C5_RX       124U
#define _DMA_DDI_DMA1_2_REQUEST_I2C5_TX       125U
#define _DMA_DDI_DMA1_2_REQUEST_TIM23_CH1     126U
#define _DMA_DDI_DMA1_2_REQUEST_TIM23_CH2     127U
#define _DMA_DDI_DMA1_2_REQUEST_TIM23_CH3     128U
#define _DMA_DDI_DMA1_2_REQUEST_TIM23_CH4     129U
#define _DMA_DDI_DMA1_2_REQUEST_TIM23_UP      130U
#define _DMA_DDI_DMA1_2_REQUEST_TIM23_TRIG    131U
#define _DMA_DDI_DMA1_2_REQUEST_TIM24_CH1     132U
#define _DMA_DDI_DMA1_2_REQUEST_TIM24_CH2     133U
#define _DMA_DDI_DMA1_2_REQUEST_TIM24_CH3     134U
#define _DMA_DDI_DMA1_2_REQUEST_TIM24_CH4     135U
#define _DMA_DDI_DMA1_2_REQUEST_TIM24_UP      136U
#define _DMA_DDI_DMA1_2_REQUEST_TIM24_TRIG    137U

/* DMAMUX2 requests */
#define _DMA_DDI_BDMA_REQUEST_MEM2MEM         0U
#define _DMA_DDI_BDMA_REQUEST_GENERATOR0      1U
#define _DMA_DDI_BDMA_REQUEST_GENERATOR1      2U
#define _DMA_DDI_BDMA_REQUEST_GENERATOR2      3U
#define _DMA_DDI_BDMA_REQUEST_GENERATOR3      4U
#define _DMA_DDI_BDMA_REQUEST_GENERATOR4      5U
#define _DMA_DDI_BDMA_REQUEST_GENERATOR5      6U
#define _DMA_DDI_BDMA_REQUEST_GENERATOR6      7U
#define _DMA_DDI_BDMA_REQUEST_GENERATOR7      8U
#define _DMA_DDI_BDMA_REQUEST_LPUART1_RX      9U
#define _DMA_DDI_BDMA_REQUEST_LPUART1_TX      10U
#define _DMA_DDI_BDMA_REQUEST_SPI6_RX         11U
#define _DMA_DDI_BDMA_REQUEST_SPI6_TX         12U
#define _DMA_DDI_BDMA_REQUEST_I2C4_RX         13U
#define _DMA_DDI_BDMA_REQUEST_I2C4_TX         14U
#define _DMA_DDI_BDMA_REQUEST_SAI4_A          15U
#define _DMA_DDI_BDMA_REQUEST_SAI4_B          16U
#define _DMA_DDI_BDMA_REQUEST_ADC3            17U
#define _DMA_DDI_BDMA_REQUEST_DAC2_CH1        17U
#define _DMA_DDI_BDMA_REQUEST_DFSDM2_FLT0     18U

#endif

/*==============================================================================
  Exported object types
==============================================================================*/
typedef bool (*_DMA_cb_t)(DMA_Stream_TypeDef *stream, u8_t SR, void *arg);

typedef struct {
        void     *user_ctx; /*! user context */
        _DMA_cb_t cb_finish; /*! finish callback */
        _DMA_cb_t cb_half; /*! half transfer callback */
        _DMA_cb_t cb_next; /*! next callback */
        u32_t     data_number; /*! data number */
        u32_t     peripheral_address; /*! peripheral address */
        u32_t     memory_address[2]; /*! memory address */
        u32_t     IRQ_priority; /*! IRQ priority */
        u32_t     channel; /*! DMA channel */
        bool      release; /*! automatically release stream */

        enum {
                _DMA_DDI_MEMORY_BURST_SINGLE_TRANSFER,
                _DMA_DDI_MEMORY_BURST_4_BEATS,
                _DMA_DDI_MEMORY_BURST_8_BEATS,
                _DMA_DDI_MEMORY_BURST_16_BEATS,
        } memory_burst :2;

        enum {
                _DMA_DDI_PERIPHERAL_BURST_SINGLE_TRANSFER,
                _DMA_DDI_PERIPHERAL_BURST_4_BEATS,
                _DMA_DDI_PERIPHERAL_BURST_8_BEATS,
                _DMA_DDI_PERIPHERAL_BURST_16_BEATS,
        } peripheral_burst :2;

        enum {
                _DMA_DDI_BUFFERABLE_TRANSFER_DISABLED,
                _DMA_DDI_BUFFERABLE_TRANSFER_ENABLED,
        } bufferable_transfer :1;

        enum {
                _DMA_DDI_DOUBLE_BUFFER_MODE_DISABLED,
                _DMA_DDI_DOUBLE_BUFFER_MODE_ENABLED,
        } double_buffer_mode :1;

        enum {
                _DMA_DDI_PRIORITY_LEVEL_LOW,
                _DMA_DDI_PRIORITY_LEVEL_MEDIUM,
                _DMA_DDI_PRIORITY_LEVEL_HIGH,
                _DMA_DDI_PRIORITY_LEVEL_VERY_HIGH,
        } priority_level :2;

        enum {
                _DMA_DDI_PERIPHERAL_INCREMENT_OFFSET_ACCORDING_TO_PERIPHERAL_SIZE,
                _DMA_DDI_PERIPHERAL_INCREMENT_OFFSET_FIXED_TO_4,
        } peripheral_increment_offset :1;

        enum {
                _DMA_DDI_MEMORY_DATA_SIZE_BYTE,
                _DMA_DDI_MEMORY_DATA_SIZE_HALF_WORD,
                _DMA_DDI_MEMORY_DATA_SIZE_WORD,
        } memory_data_size :2;

        enum {
                _DMA_DDI_PERIPHERAL_DATA_SIZE_BYTE,
                _DMA_DDI_PERIPHERAL_DATA_SIZE_HALF_WORD,
                _DMA_DDI_PERIPHERAL_DATA_SIZE_WORD,
        } peripheral_data_size :2;

        enum {
                _DMA_DDI_MEMORY_ADDRESS_POINTER_IS_FIXED,
                _DMA_DDI_MEMORY_ADDRESS_POINTER_INCREMENTED,
        } memory_address_increment :1;

        enum {
                _DMA_DDI_PERIPHERAL_ADDRESS_POINTER_IS_FIXED,
                _DMA_DDI_PERIPHERAL_ADDRESS_POINTER_INCREMENTED,
        } peripheral_address_increment :1;

        enum {
                _DMA_DDI_CIRCULAR_MODE_DISABLED,
                _DMA_DDI_CIRCULAR_MODE_ENABLED,
        } circular_mode :1;

        enum {
                _DMA_DDI_TRANSFER_DIRECTION_PERIPHERAL_TO_MEMORY,
                _DMA_DDI_TRANSFER_DIRECTION_MEMORY_TO_PERIPHERAL,
                _DMA_DDI_TRANSFER_DIRECTION_MEMORY_TO_MEMORY,
        } transfer_direction :2;

        enum {
                _DMA_DDI_FLOW_CONTROLLER_DMA,
                _DMA_DDI_FLOW_CONTROLLER_PERIPHERAL,
        } flow_controller :1;

        enum {
                _DMA_DDI_MODE_DIRECT,
                _DMA_DDI_MODE_FIFO,
        } mode :1;

        enum {
                _DMA_DDI_FIFO_THRESHOLD_1_4,
                _DMA_DDI_FIFO_THRESHOLD_1_2,
                _DMA_DDI_FIFO_THRESHOLD_3_4,
                _DMA_DDI_FIFO_THRESHOLD_FULL,
        } fifo_threshold :2;
} _DMA_DDI_config_t;

#if defined(ARCH_stm32h7)
/*------------------------------------------------------------------------------
 * MDMA
 -----------------------------------------------------------------------------*/
typedef bool (*_MDMA_cb_t)(MDMA_Channel_TypeDef *stream, void *arg);

/*
 * MDMA configuration structure
 */
typedef struct {
        void      *user_ctx;
        _MDMA_cb_t cb_channel_transfer_completed;
        _MDMA_cb_t cb_block_transfer_completed;
        _MDMA_cb_t cb_block_transfer_repeat_completed;
        _MDMA_cb_t cb_buffer_transfer_completed;
        _MDMA_cb_t cb_transfer_error;
        u32_t      source_address;
        u32_t      destination_address;
        u32_t      buffer_transfer_length;
        u32_t      link_address;
        u16_t      block_repeat_destination_address_update;
        u16_t      block_repeat_source_address_update;
        u32_t      block_repeat_count;
        u32_t      block_size;
        u32_t      mask_address;
        u32_t      mask_data;
        bool       release;
        u8_t       trigger_selection;

        enum {
                _MDMA_DDI_SOFTWARE_REQUEST_DISABLE,
                _MDMA_DDI_SOFTWARE_REQUEST_ENABLE,
        } software_request :1;

        enum {
                _MDMA_DDI_WORD_ENDIANESS_EXCHANGE_DISABLE,
                _MDMA_DDI_WORD_ENDIANESS_EXCHANGE_ENABLE,
        } word_endianess_exchange :1;

        enum {
                _MDMA_DDI_HALF_WORD_ENDIANESS_EXCHANGE_DISABLE,
                _MDMA_DDI_HALF_WORD_ENDIANESS_EXCHANGE_ENABLE,
        } half_word_endianess_exchange :1;

        enum {
                _MDMA_DDI_BYTE_ENDIANESS_EXCHANGE_DISABLE,
                _MDMA_DDI_BYTE_ENDIANESS_EXCHANGE_ENABLE,
        } byte_endianess_exchange :1;

        enum {
                _MDMA_DDI_PRIORITY_LEVEL_LOW,
                _MDMA_DDI_PRIORITY_LEVEL_MEDIUM,
                _MDMA_DDI_PRIORITY_LEVEL_HIGH,
                _MDMA_DDI_PRIORITY_LEVEL_VERY_HIGH,
        } priority_level :2;

        enum {
                _MDMA_DDI_DESTINATION_WRITE_NON_BUFFERABLE,
                _MDMA_DDI_DESTINATION_WRITE_BUFFERABLE
        } bufferable_write_mode :1;

        enum {
                _MDMA_DDI_REQUEST_MODE_HARDWARE,
                _MDMA_DDI_REQUEST_MODE_SOFTWARE
        } request_mode :1;

        enum {
                _MDMA_DDI_TRIGGER_MODE_BUFFER_TRANSFER,
                _MDMA_DDI_TRIGGER_MODE_BLOCK_TRANSFER,
                _MDMA_DDI_TRIGGER_MODE_REPEATED_BLOCK_TRANSFER,
                _MDMA_DDI_TRIGGER_MODE_LINKED_LIST_TRANSFER,
        } trigger_mode :2;

        enum {
                _MDMA_DDI_PADDING_ALIGNMENT_RIGHT_ALIGNED_PADDED_0,
                _MDMA_DDI_PADDING_ALIGNMENT_RIGHT_ALIGNED_SIGN_EXTENDED,
                _MDMA_DDI_PADDING_ALIGNMENT_LEFT_ALIGNED_PADDED_0,
        } padding_alignment_mode :2;

        enum {
                _MDMA_DDI_PACK_MODE_DISABLED,
                _MDMA_DDI_PACK_MODE_ENABLED
        } pack_mode :1;

        enum {
                _MDMA_DDI_DESTINATION_BURST_SINGLE_TRANSFER,
                _MDMA_DDI_DESTINATION_BURST_2_BEATS,
                _MDMA_DDI_DESTINATION_BURST_4_BEATS,
                _MDMA_DDI_DESTINATION_BURST_8_BEATS,
                _MDMA_DDI_DESTINATION_BURST_16_BEATS,
                _MDMA_DDI_DESTINATION_BURST_32_BEATS,
                _MDMA_DDI_DESTINATION_BURST_64_BEATS,
                _MDMA_DDI_DESTINATION_BURST_128_BEATS,
        } destination_burst :3;

        enum {
                _MDMA_DDI_SOURCE_BURST_SINGLE_TRANSFER,
                _MDMA_DDI_SOURCE_BURST_2_BEATS,
                _MDMA_DDI_SOURCE_BURST_4_BEATS,
                _MDMA_DDI_SOURCE_BURST_8_BEATS,
                _MDMA_DDI_SOURCE_BURST_16_BEATS,
                _MDMA_DDI_SOURCE_BURST_32_BEATS,
                _MDMA_DDI_SOURCE_BURST_64_BEATS,
                _MDMA_DDI_SOURCE_BURST_128_BEATS,
        } source_burst :3;

        enum {
                _MDMA_DDI_DESTINATION_INCREMENT_OFFSET_BYTE,
                _MDMA_DDI_DESTINATION_INCREMENT_OFFSET_HALF_WORD,
                _MDMA_DDI_DESTINATION_INCREMENT_OFFSET_WORD,
                _MDMA_DDI_DESTINATION_INCREMENT_OFFSET_DOUBLE_WORD
        } destination_increment_offset_size :2;

        enum {
                _MDMA_DDI_SOURCE_INCREMENT_OFFSET_BYTE,
                _MDMA_DDI_SOURCE_INCREMENT_OFFSET_HALF_WORD,
                _MDMA_DDI_SOURCE_INCREMENT_OFFSET_WORD,
                _MDMA_DDI_SOURCE_INCREMENT_OFFSET_DOUBLE_WORD
        } source_increment_offset_size :2;

        enum {
                _MDMA_DDI_DESTINATION_DATA_SIZE_BYTE,
                _MDMA_DDI_DESTINATION_DATA_SIZE_HALF_WORD,
                _MDMA_DDI_DESTINATION_DATA_SIZE_WORD,
                _MDMA_DDI_DESTINATION_DATA_SIZE_DOUBLE_WORD
        } destination_data_size :2;

        enum {
                _MDMA_DDI_SOURCE_DATA_SIZE_BYTE,
                _MDMA_DDI_SOURCE_DATA_SIZE_HALF_WORD,
                _MDMA_DDI_SOURCE_DATA_SIZE_WORD,
                _MDMA_DDI_SOURCE_DATA_SIZE_DOUBLE_WORD
        } source_data_size :2;

        enum {
                _MDMA_DDI_DESTINATION_ADDRESS_POINTER_FIXED      = 0,
                _MDMA_DDI_DESTINATION_ADDRESS_PONTER_INCREMENTED = 2,
                _MDMA_DDI_DESTINATION_ADDRESS_PONTER_DECREMENTED = 3,
        } destination_address_pointer_mode :2;

        enum {
                _MDMA_DDI_SOURCE_ADDRESS_POINTER_FIXED      = 0,
                _MDMA_DDI_SOURCE_ADDRESS_PONTER_INCREMENTED = 2,
                _MDMA_DDI_SOURCE_ADDRESS_PONTER_DECREMENTED = 3,
        } source_address_pointer_mode :2;

        enum { // BRDUM
                _MDMA_DDI_BLOCK_REPEAT_DESTINATION_ADDRESS_UPDATE_ADD,
                _MDMA_DDI_BLOCK_REPEAT_DESTINATION_ADDRESS_UPDATE_SUB,
        } block_repeat_destination_address_update_mode :1;

        enum { // BRSUM
                _MDMA_DDI_BLOCK_REPEAT_SOURCE_ADDRESS_UPDATE_ADD,
                _MDMA_DDI_BLOCK_REPEAT_SOURCE_ADDRESS_UPDATE_SUB,
        } block_repeat_source_address_update_mode :1;

        enum {
                _MDMA_DDI_DESTINATION_BUS_SYSTEM_AXI,
                _MDMA_DDI_DESTINATION_BUS_AHB_TCM
        } destination_bus :1;

        enum {
                _MDMA_DDI_SOURCE_BUS_SYSTEM_AXI,
                _MDMA_DDI_SOURCE_BUS_AHB_TCM
        } source_bus :1;

} _MDMA_DDI_config_t;
#endif

/*==============================================================================
  Exported objects
==============================================================================*/

/*==============================================================================
  Exported functions
==============================================================================*/
#if defined(ARCH_stm32f4) || defined(ARCH_stm32f7)
//==============================================================================
/**
 * @brief Function allocate selected stream.
 *
 * @param [in]  major         DMA peripheral number.
 * @param [in]  stream        stream number.
 *
 * @return On success DMA descriptor number, otherwise 0.
 */
//==============================================================================
extern u32_t _DMA_DDI_reserve(u8_t major, u8_t stream);
#elif defined(ARCH_stm32h7)
//==============================================================================
/**
 * @brief Function allocate selected stream.
 *
 * @param [in]  major_mask    DMA peripheral mask.
 * @param [in]  stream        stream number.
 *
 * @return On success DMA descriptor number, otherwise 0.
 */
//==============================================================================
extern u32_t _DMA_DDI_reserve(u8_t major_mask);
#endif

//==============================================================================
/**
 * @brief Function free allocated stream.
 *
 * @param dmad                  DMA descriptor.
 */
//==============================================================================
extern void _DMA_DDI_release(u32_t dmad);

//==============================================================================
/**
 * @brief Function start transfer. The IRQ flags (TCIE, TEIE) are added
 *        automatically.
 *
 * @param dmad                  DMA descriptor.
 * @param config                DMA configuration.
 *
 * @return One of errno value.
 */
//==============================================================================
extern int _DMA_DDI_transfer(u32_t dmad, _DMA_DDI_config_t *config);

//==============================================================================
/**
 * @brief Function abort configured DMA transfer.
 *
 * @param dmad                  DMA descriptor.
 */
//==============================================================================
extern void _DMA_DDI_abort(u32_t dmad);

//==============================================================================
/**
 * @brief Function reaturn DMA steram according to DMA descriptor.
 *
 * @param dmad                  DMA descriptor.
 * @param stream                stream
 *
 * @return One of errno value.
 */
//==============================================================================
extern int _DMA_DDI_get_stream(u32_t dmad, DMA_Stream_TypeDef **stream);

//==============================================================================
/**
 * @brief Function start memory-to-memory transfer by using free channel.
 *
 * @param dst                   destination address.
 * @param src                   source address.
 * @param size                  block size.
 *
 * @return One of errno value.
 */
//==============================================================================
extern int _DMA_DDI_memcpy(void *dst, const void *src, size_t size);

#if defined(ARCH_stm32h7)
/*------------------------------------------------------------------------------
 * MDMA
 -----------------------------------------------------------------------------*/
//==============================================================================
/**
 * @brief Function allocate selected stream.
 *
 * @param [in]  major_mask    DMA peripheral mask.
 *
 * @return On success DMA descriptor number, otherwise 0.
 */
//==============================================================================
extern u32_t _MDMA_DDI_reserve(void);

//==============================================================================
/**
 * @brief Function free allocated stream.
 *
 * @param dmad                  DMA descriptor.
 */
//==============================================================================
extern void _MDMA_DDI_release(u32_t dmad);

//==============================================================================
/**
 * @brief Function start transfer. The IRQ flags (TCIE, TEIE) are added
 *        automatically.
 *
 * @param dmad                  DMA descriptor.
 * @param config                DMA configuration.
 *
 * @return One of errno value.
 */
//==============================================================================
extern int _MDMA_DDI_transfer(u32_t dmad, _MDMA_DDI_config_t *config);

//==============================================================================
/**
 * @brief Function reaturn DMA steram according to DMA descriptor.
 *
 * @param dmad                  DMA descriptor.
 * @param stream                stream
 *
 * @return One of errno value.
 */
//==============================================================================
extern int _MDMA_DDI_get_channel(u32_t dmad, MDMA_Channel_TypeDef **stream);

//==============================================================================
/**
 * @brief Function start memory-to-memory transfer by using free channel.
 *
 * @param dst                   destination address.
 * @param src                   source address.
 * @param size                  block size.
 *
 * @return One of errno value.
 */
//==============================================================================
extern int _MDMA_DDI_memcpy(void *dst, const void *src, size_t size);
#endif

/*==============================================================================
  Exported inline functions
==============================================================================*/

#ifdef __cplusplus
}
#endif

#endif /* ARCH_stm32f4 || ARCH_stm32f7 || ARCH_stm32h7 */
#endif /* _DMA_DDI_H_ */
/**@}*/
/*==============================================================================
  End of file
==============================================================================*/
