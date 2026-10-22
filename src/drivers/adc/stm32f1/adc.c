/*==============================================================================
File    adc.c

Author  Daniel Zorychta

Brief   ADC Driver

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
#include "../adc_ioctl.h"
#include "drivers/driver.h"
#include "stm32f1/stm32f10x.h"

/*==============================================================================
  Local macros
==============================================================================*/
#define ADC_SQR1_L_Pos   20
#define ADC_SQR3_SQ1_Pos 0

/*==============================================================================
  Local object types
==============================================================================*/
typedef struct {
        u8_t major;
        u8_t minor;
} ADCM_t;

enum {
#if defined(RCC_APB2ENR_ADC1EN)
        _ADC1,
#endif
#if defined(RCC_AHB2ENR_ADC2EN)
        _ADC2,
#endif
#if defined(RCC_AHB2ENR_ADC3EN)
        _ADC3,
#endif
        _ADC_COUNT
};

typedef struct {
        ADC_t *ADC;
        __IO uint32_t *APBENR;
        __IO uint32_t *APBRSTR;
        const uint32_t RCC_APBENR_ADCEN;
        const uint32_t RCC_APBRSTR_ADCRST;
        const IRQn_Type IRQn;
} ADC_regs_t;

/*==============================================================================
  Local function prototypes
==============================================================================*/
static void enable_ADC(const ADCM_t *hdl);
static void disable_ADC(const ADCM_t *hdl);
static int convert_channel(const ADCM_t *hdl, adc_measurement_t *measurement);

/*==============================================================================
  Local object
==============================================================================*/
MODULE_NAME(ADC);

static const ADC_regs_t ADC_[] = {
#if defined(RCC_APB2ENR_ADC1EN)
        {
                .ADC = ADC1,
                .APBENR = &RCC->APB2ENR,
                .APBRSTR = &RCC->APB2RSTR,
                .RCC_APBENR_ADCEN = RCC_APB2ENR_ADC1EN,
                .RCC_APBRSTR_ADCRST = RCC_APB2RSTR_ADC1RST,
                .IRQn = ADC1_2_IRQn,
        },
#endif
#if defined(RCC_AHB2ENR_ADC2EN)
        {
                .ADC = ADC2,
                .APBENR = &RCC->APB2ENR,
                .APBRSTR = &RCC->APB2RSTR,
                .RCC_APBENR_ADCEN = RCC_APB2ENR_ADC2EN,
                .RCC_APBRSTR_ADCRST = RCC_APB2RSTR_ADC2RST,
                .IRQn = ADC1_2_IRQn,
        },
#endif
#if defined(RCC_AHB2ENR_ADC3EN)
        {
                .ADC = ADC3,
                .APBENR = &RCC->APB2ENR,
                .APBRSTR = &RCC->APB2RSTR,
                .RCC_APBENR_ADCEN = RCC_APB2ENR_ADC3EN,
                .RCC_APBRSTR_ADCRST = RCC_APB2RSTR_ADC3RST,
                .IRQn = ADC3_IRQn,
        },
#endif
};

static ADCM_t *ADC[_ADC_COUNT];

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
API_MOD_INIT(ADC, void **device_handle, u8_t major, u8_t minor, const void *config)
{
        int err = EFAULT;

        if ((major < _ADC_COUNT) && (minor == 0)) {
                err = sys_zalloc(sizeof(ADCM_t), device_handle);
                if (!err) {
                        ADCM_t *hdl = *device_handle;
                        hdl->major = major;
                        hdl->minor = minor;

                        if (config) {
                        }

                        if (not err) {
                                enable_ADC(hdl);
                                ADC[major] = hdl;

                        } else {
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
API_MOD_RELEASE(ADC, void *device_handle)
{
        ADCM_t *hdl = device_handle;

        disable_ADC(hdl);
        ADC[hdl->major] = NULL;

        int err = sys_free(&device_handle);
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
API_MOD_OPEN(ADC, void *device_handle, u32_t flags)
{
        UNUSED_ARG2(flags, device_handle);

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
API_MOD_CLOSE(ADC, void *device_handle, bool force)
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
 * @param[out]          *wrctr                  number of written bytes
 * @param[in ]           fattr                  file attributes
 *
 * @return One of errno value (errno.h).
 */
//==============================================================================
API_MOD_WRITE(ADC, void *device_handle, const u8_t *src, size_t count, fpos_t *fpos, size_t *wrctr,
        struct vfs_fattr fattr)
{
        UNUSED_ARG6(device_handle, src, count, fpos, wrctr, fattr);

        return ENOTSUP;
}

//==============================================================================
/**
 * @brief Read data from device.
 *
 * @param[in ]          *device_handle          device allocated memory
 * @param[out]          *dst                    data destination
 * @param[in ]           count                  number of bytes to read
 * @param[in ][out]     *fpos                   file position
 * @param[out]          *rdctr                  number of read bytes
 * @param[in ]           fattr                  file attributes
 *
 * @return One of errno value (errno.h).
 */
//==============================================================================
API_MOD_READ(ADC, void *device_handle, u8_t *dst, size_t count, fpos_t *fpos, size_t *rdctr, struct vfs_fattr fattr)
{
        UNUSED_ARG6(device_handle, dst, count, fpos, rdctr, fattr);

        return ENOTSUP;
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
API_MOD_IOCTL(ADC, void *device_handle, int request, void *arg)
{
        ADCM_t *hdl = device_handle;

        int err = EINVAL;

        switch (request) {
        case IOCTL_ADC__READ_CHANNEL:
                if (not arg) {
                        break;
                }

                adc_measurement_t *measurement = arg;
                if (measurement->channel > 17) {
                        dev_dbg(hdl, "Channel out of range: %u of %u", measurement->channel, 17);
                        break;
                }

                err = convert_channel(hdl, measurement);
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
API_MOD_FLUSH(ADC, void *device_handle)
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
API_MOD_STAT(ADC, void *device_handle, struct vfs_dev_stat *device_stat)
{
        UNUSED_ARG1(device_handle);

        device_stat->st_size = 0;

        return ESUCC;
}

//==============================================================================
/**
 * @brief   Enables ADC.
 *
 * @param   hdl        ADC device instance.
 */
//==============================================================================
static void enable_ADC(const ADCM_t *hdl)
{
        const ADC_regs_t *ADC_if = &ADC_[hdl->major];

        SET_BIT(*ADC_if->APBRSTR, ADC_if->RCC_APBRSTR_ADCRST);
        CLEAR_BIT(*ADC_if->APBRSTR, ADC_if->RCC_APBRSTR_ADCRST);
        SET_BIT(*ADC_if->APBENR, ADC_if->RCC_APBENR_ADCEN);
        ADC_if->ADC->CR2 = ADC_CR2_ADON;
}

//==============================================================================
/**
 * @brief   Disables ADC.
 *
 * @param   hdl        ADC device instance.
 */
//==============================================================================
static void disable_ADC(const ADCM_t *hdl)
{
        const ADC_regs_t *ADC_if = &ADC_[hdl->major];

        CLEAR_BIT(ADC_if->ADC->CR2, ADC_CR2_ADON);
        SET_BIT(*ADC_if->APBRSTR, ADC_if->RCC_APBRSTR_ADCRST);
        CLEAR_BIT(*ADC_if->APBRSTR, ADC_if->RCC_APBRSTR_ADCRST);
        CLEAR_BIT(*ADC_if->APBENR, ADC_if->RCC_APBENR_ADCEN);
}

//==============================================================================
/**
 * @brief
 *
 * @param  ?
 *
 * @return ?
 */
//==============================================================================
static int convert_channel(const ADCM_t *hdl, adc_measurement_t *measurement)
{
        int err = 0;
        const ADC_regs_t *ADC_if = &ADC_[hdl->major];

        // convertion time parameters
        ADC_if->ADC->SMPR1 = 04444444444; // 239.5 cycle convertion for all channels
        ADC_if->ADC->SMPR2 = 04444444444; // 239.5 cycle convertion for all channels

        // convertion sequence
        ADC_if->ADC->SQR1 = (1 << ADC_SQR1_L_Pos);
        ADC_if->ADC->SQR2 = 0;
        ADC_if->ADC->SQR3 = (measurement->channel << ADC_SQR3_SQ1_Pos);

        // start conversion
        ADC_if->ADC->CR1 = 0;
        ADC_if->ADC->CR2 = ADC_CR2_ADON;
        SET_BIT(ADC_if->ADC->CR2, ADC_CR2_SWSTART);

        u32_t cycles = 100;
        while (not(ADC_if->ADC->SR & ADC_SR_EOC)) {
                sys_sleep_ms(10);
                if (--cycles == 0) {
                        err = ETIME;
                        break;
                }
        }

        measurement->raw_data = ADC_if->ADC->DR;

        return err;
}

//==============================================================================
/**
 * @brief   Disables ADC IRQ.
 *
 * @param   hdl        ADC device instance.
 */
//==============================================================================
static void IRQ_disable(ADCM_t *hdl)
{
        ADC_regs_t const *ADC_if = &ADC_[hdl->major];
        NVIC_DisableIRQ(ADC_if->IRQn);
}

//==============================================================================
/**
 * @brief   Initialisation of ADC IRQ.
 *
 * @param   hdl        ADC device instance.
 */
//==============================================================================
static void IRQ_init(ADCM_t *hdl)
{
        ADC_regs_t const *ADC_if = &ADC_[hdl->major];

        NVIC_EnableIRQ(ADC_if->IRQn);
        NVIC_SetPriority(ADC_if->IRQn, _CPU_IRQ_SAFE_PRIORITY_);
}

//==============================================================================
/**
 * @brief  ADC1 and ADC2 IRQ handlers.
 */
//==============================================================================
void ADC1_2_IRQHandler(void)
{
}

//==============================================================================
/**
 * @brief  ADC3 IRQ handler.
 */
//==============================================================================
void ADC3_IRQHandler(void)
{
}

/*==============================================================================
  End of file
==============================================================================*/
