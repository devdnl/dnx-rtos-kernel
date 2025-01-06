/*==============================================================================
@file    clk.c

@author  Daniel Zorychta

@brief   File support system clock.

@note    Copyright (C) 2024 Daniel Zorychta <daniel.zorychta@gmail.com>

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

// #pragma GCC optimize("Og")

/*==============================================================================
  Include files
==============================================================================*/
#include "drivers/driver.h"
#include "clk_ioctl.h"
#include "rpcm0/misc/misc.h"
#include "lib/CMSIS/core_cm0plus.h"
#include "hardware/regs/addressmap.h"
#include "hardware/regs/clocks.h"
#include "hardware/structs/clocks.h"
#include "hardware/regs/xosc.h"
#include "hardware/structs/xosc.h"
#include "hardware/regs/pll.h"
#include "hardware/structs/pll.h"
#include "hardware/regs/resets.h"
#include "hardware/structs/resets.h"

/*==============================================================================
  Local symbolic constants/macros
==============================================================================*/
#define XOSC_STARTUP_DELAY ((((XOSC_HZ / 1000) + 128) / 256) * 2)

/*==============================================================================
  Local types, enums definitions
==============================================================================*/

/*==============================================================================
  Local function prototypes
==============================================================================*/
static int xosc_init(void);
static int pll_init(pll_hw_t *pll, u32_t refdiv, u32_t fbdiv, u32_t postdiv1, u32_t postdiv2,
        u32_t reset_flag);
static int set_clk_ref_source(void);
static int set_clk_usb_source(void);
static int set_clk_sys_source(void);
static int set_clk_adc_source(void);
static int set_clk_rtc_source(void);
static int set_clk_peri_source(void);
static int set_clk_gpout_source(clock_num_t clock);

/*==============================================================================
  Local object definitions
==============================================================================*/
MODULE_NAME(CLK);

/*==============================================================================
  Exported object definitions
==============================================================================*/

/*==============================================================================
  Function definitions
==============================================================================*/

//==============================================================================
/**
 * @brief Initialize device
 *
 * @param[out]          **device_handle        device allocated memory
 * @param[in ]            major                major device number
 * @param[in ]            minor                minor device number
 * @param[in ]            config               optional module configuration
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
API_MOD_INIT(CLK, void **device_handle, u8_t major, u8_t minor, const void *config)
{
        UNUSED_ARG2(device_handle, config);

        if ((major != 0) or (minor != 0)) {
                return ENODEV;
        }

        int err = ESUCC;

        /*
         * Clocks
         */
        if (__CLK_XOSC_ENABLE__) {
                if (xosc_init() != 0) {
                        dev_dbg2(0, 0, "XOSC start fail!");
                        err = ETIME;
                }
        }

        /*
         * PLLs
         */
        if (__CLK_SYS_PLL_ENABLE__) {
                if (pll_init(pll_sys_hw, __CLK_SYS_PLL_REFDIV__, __CLK_SYS_PLL_FBDIV__,
                            __CLK_SYS_PLL_POSTDIV1__, __CLK_SYS_PLL_POSTDIV2__, RESETS_RESET_PLL_SYS_BITS)) {
                        dev_dbg2(0, 0, "SYS_PLL start fail!");
                        err = ETIME;
                }
        }

        if (__CLK_USB_PLL_ENABLE__) {
                if (pll_init(pll_usb_hw, __CLK_USB_PLL_REFDIV__, __CLK_USB_PLL_FBDIV__,
                            __CLK_USB_PLL_POSTDIV1__, __CLK_USB_PLL_POSTDIV2__, RESETS_RESET_PLL_USB_BITS)) {
                        dev_dbg2(0, 0, "USB_PLL start fail!");
                        err = ETIME;
                }
        }

        if (err) {
                return err;
        }

        /*
         * Clock mux
         */
        if (set_clk_ref_source() != 0) {
                dev_dbg2(0, 0, "cannot set clk_ref source!");
        }

        if (set_clk_usb_source() != 0) {
                dev_dbg2(0, 0, "cannot set clk_usb source!");
        }

        if (set_clk_adc_source() != 0) {
                dev_dbg2(0, 0, "cannot set clk_adc source!");
        }

        if (set_clk_rtc_source() != 0) {
                dev_dbg2(0, 0, "cannot set clk_rtc source!");
        }

        if (set_clk_sys_source() != 0) {
                dev_dbg2(0, 0, "cannot set clk_sys source!");
        }

        if (set_clk_peri_source() != 0) {
                dev_dbg2(0, 0, "cannot set clk_peri source!");
        }

        /*
         * GPIO out clocks
         */
        if (__CLK_GPOUT0_ENABLE__) {
                if (set_clk_gpout_source(clk_gpout0) != 0) {
                        dev_dbg2(0, 0, "cannot set clk_gpout0 source!");
                }
        }

        if (__CLK_GPOUT1_ENABLE__) {
                if (set_clk_gpout_source(clk_gpout1) != 0) {
                        dev_dbg2(0, 0, "cannot set clk_gpout1 source!");
                }
        }

        if (__CLK_GPOUT2_ENABLE__) {
                if (set_clk_gpout_source(clk_gpout2) != 0) {
                        dev_dbg2(0, 0, "cannot set clk_gpout2 source!");
                }
        }

        if (__CLK_GPOUT3_ENABLE__) {
                if (set_clk_gpout_source(clk_gpout3) != 0) {
                        dev_dbg2(0, 0, "cannot set clk_gpout3 source!");
                }
        }

        sys_update_system_clocks();

        return err;
}

//==============================================================================
/**
 * @brief Release device
 *
 * @param[in ]          *device_handle          device allocated memory
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
API_MOD_RELEASE(CLK, void *device_handle)
{
        UNUSED_ARG1(device_handle);

        sys_update_system_clocks();

        return ESUCC;
}

//==============================================================================
/**
 * @brief Open device
 *
 * @param[in ]          *device_handle          device allocated memory
 * @param[in ]           flags                  file operation flags (O_RDONLY, O_WRONLY, O_RDWR)
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
API_MOD_OPEN(CLK, void *device_handle, u32_t flags)
{
        UNUSED_ARG1(device_handle);
        UNUSED_ARG1(flags);

        return ESUCC;
}

//==============================================================================
/**
 * @brief Close device
 *
 * @param[in ]          *device_handle          device allocated memory
 * @param[in ]           force                  device force close (true)
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
API_MOD_CLOSE(CLK, void *device_handle, bool force)
{
        UNUSED_ARG1(device_handle);
        UNUSED_ARG1(force);

        return ESUCC;
}

//==============================================================================
/**
 * @brief Write data to device
 *
 * @param[in ]          *device_handle          device allocated memory
 * @param[in ]          *src                    data source
 * @param[in ]           count                  number of bytes to write
 * @param[in ][out]     *fpos                   file position
 * @param[out]          *wrcnt                  number of written bytes
 * @param[in ]           fattr                  file attributes
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
API_MOD_WRITE(CLK, void *device_handle, const u8_t *src, size_t count, fpos_t *fpos, size_t *wrcnt,
        struct vfs_fattr fattr)
{
        UNUSED_ARG1(device_handle);
        UNUSED_ARG1(src);
        UNUSED_ARG1(count);
        UNUSED_ARG1(fpos);
        UNUSED_ARG1(wrcnt);
        UNUSED_ARG1(fattr);

        return ENOTSUP;
}

//==============================================================================
/**
 * @brief Read data from device
 *
 * @param[in ]          *device_handle          device allocated memory
 * @param[out]          *dst                    data destination
 * @param[in ]           count                  number of bytes to read
 * @param[in ][out]     *fpos                   file position
 * @param[out]          *rdcnt                  number of read bytes
 * @param[in ]           fattr                  file attributes
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
API_MOD_READ(CLK, void *device_handle, u8_t *dst, size_t count, fpos_t *fpos, size_t *rdcnt,
        struct vfs_fattr fattr)
{
        UNUSED_ARG1(device_handle);
        UNUSED_ARG1(dst);
        UNUSED_ARG1(count);
        UNUSED_ARG1(fpos);
        UNUSED_ARG1(rdcnt);
        UNUSED_ARG1(fattr);

        return ENOTSUP;
}

//==============================================================================
/**
 * @brief IO control
 *
 * @param[in ]          *device_handle          device allocated memory
 * @param[in ]           request                request
 * @param[in ][out]     *arg                    request's argument
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
API_MOD_IOCTL(CLK, void *device_handle, int request, void *arg)
{
        UNUSED_ARG1(device_handle);

        int err = EINVAL;

        if (arg) {
                if (request == IOCTL_CLK__GET_CLK_INFO) {
                        CLK_info_t *clkinf = arg;
                        switch (clkinf->iterator) {
                        case 0:
                                clkinf->freq_Hz = clocks__get_clk_sys_frequency();
                                clkinf->name    = "CPUCLK";
                                break;

                        case 1:
                                clkinf->freq_Hz = clocks__get_gpclk_frequency(0);
                                clkinf->name    = "GPCLK0";
                                break;

                        case 2:
                                clkinf->freq_Hz = clocks__get_gpclk_frequency(1);
                                clkinf->name    = "GPCLK1";
                                break;

                        case 3:
                                clkinf->freq_Hz = clocks__get_sys_pll_frequency();
                                clkinf->name    = "SYS_PLL";
                                break;

                        case 4:
                                clkinf->freq_Hz = clocks__get_usb_pll_frequency();
                                clkinf->name    = "USB_PLL";
                                break;

                        case 5:
                                clkinf->freq_Hz = clocks__get_xosc_frequency();
                                clkinf->name    = "XOSC";
                                break;

                        case 6:
                                clkinf->freq_Hz = clocks__get_rosc_frequency();
                                clkinf->name    = "ROSC";
                                break;

                        case 7:
                                clkinf->freq_Hz = clocks__get_clk_adc_frequency();
                                clkinf->name    = "clk_adc";
                                break;

                        case 8:
                                clkinf->freq_Hz = clocks__get_clk_usb_frequency();
                                clkinf->name    = "clk_usb";
                                break;

                        case 9:
                                clkinf->freq_Hz = clocks__get_clk_rtc_frequency();
                                clkinf->name    = "clk_rtc";
                                break;

                        case 10:
                                clkinf->freq_Hz = clocks__get_clk_peri_frequency();
                                clkinf->name    = "clk_peri";
                                break;

                        case 11:
                                clkinf->freq_Hz = clocks__get_clk_sys_frequency();
                                clkinf->name    = "clk_sys";
                                break;

                        case 12:
                                clkinf->freq_Hz = clocks__get_clk_ref_frequency();
                                clkinf->name    = "clk_ref";
                                break;

                        case 13:
                                clkinf->freq_Hz = clocks__get_clk_ref_aux_frequency();
                                clkinf->name    = "clk_ref_aux";
                                break;

                        case 14:
                                clkinf->freq_Hz = clocks__get_clk_gpout_frequency(0);
                                clkinf->name    = "clk_gpout0";
                                break;

                        case 15:
                                clkinf->freq_Hz = clocks__get_clk_gpout_frequency(1);
                                clkinf->name    = "clk_gpout1";
                                break;

                        case 16:
                                clkinf->freq_Hz = clocks__get_clk_gpout_frequency(2);
                                clkinf->name    = "clk_gpout2";
                                break;

                        case 17:
                                clkinf->freq_Hz = clocks__get_clk_gpout_frequency(3);
                                clkinf->name    = "clk_gpout3";
                                break;

                        case 18:
                                clkinf->freq_Hz = clocks__get_clk_sys_frequency();
                                clkinf->name    = "CPU0CLK";
                                break;

                        case 19:
                                clkinf->freq_Hz = clocks__get_clk_sys_frequency();
                                clkinf->name    = "CPU1CLK";
                                break;

                        default:
                                clkinf->freq_Hz = 0;
                                clkinf->name    = NULL;
                                break;
                        }

                        clkinf->iterator++;

                        err = ESUCC;
                } else {
                        err = EBADRQC;
                }
        }

        return err;
}

//==============================================================================
/**
 * @brief Flush device
 *
 * @param[in ]          *device_handle          device allocated memory
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
API_MOD_FLUSH(CLK, void *device_handle)
{
        UNUSED_ARG1(device_handle);

        return ESUCC;
}

//==============================================================================
/**
 * @brief Device information
 *
 * @param[in ]          *device_handle          device allocated memory
 * @param[out]          *device_stat            device status
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
API_MOD_STAT(CLK, void *device_handle, struct vfs_dev_stat *device_stat)
{
        UNUSED_ARG1(device_handle);

        device_stat->st_size  = 0;
        device_stat->st_major = 0;
        device_stat->st_minor = 0;

        return ESUCC;
}

//==============================================================================
/**
 * @brief  Initialize XOSC oscillator.
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
static int xosc_init(void)
{
        xosc_hw->ctrl    = XOSC_CTRL_FREQ_RANGE_VALUE_1_15MHZ;
        xosc_hw->startup = XOSC_STARTUP_DELAY;

        SET_BIT(xosc_hw->ctrl, XOSC_CTRL_ENABLE_VALUE_ENABLE << XOSC_CTRL_ENABLE_LSB);

        clock_t tref = sys_get_uptime_ms();

        while (not(xosc_hw->status & XOSC_STATUS_STABLE_BITS)) {
                if (sys_get_uptime_ms() - tref > 250) {
                        xosc_hw->ctrl = (XOSC_CTRL_ENABLE_VALUE_DISABLE << XOSC_CTRL_ENABLE_LSB);
                        return ETIME;
                }
        }

        return ESUCC;
}

//==============================================================================
/**
 * @brief  Initialize PLL.
 *
 * @param  pll          pll instance
 * @param  refdiv       reference frequency divider
 * @param  fbdiv        feedback divider
 * @param  postdiv1     post divider 1
 * @param  postdiv2     post divider 2
 * @param  reset_flag   reset flag
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
static int pll_init(pll_hw_t *pll, u32_t refdiv, u32_t fbdiv, u32_t postdiv1, u32_t postdiv2,
        u32_t reset_flag)
{
        if ((refdiv < 1) or (refdiv > 63)) {
                return EINVAL;
        }

        if ((postdiv1 < 1) or (postdiv1 > 7)) {
                return EINVAL;
        }

        if ((postdiv2 < 1) or (postdiv2 > 7)) {
                return EINVAL;
        }

        SET_BIT(resets_hw->reset, reset_flag);
        CLEAR_BIT(resets_hw->reset, reset_flag);
        while (not(resets_hw->reset_done & reset_flag)) {
        }

        pll->cs        = (refdiv << PLL_CS_REFDIV_LSB) & PLL_CS_REFDIV_BITS;
        pll->fbdiv_int = (fbdiv << PLL_FBDIV_INT_LSB) & PLL_FBDIV_INT_BITS;

        CLEAR_BIT(pll->pwr, PLL_PWR_PD_BITS | PLL_PWR_VCOPD_BITS);

        clock_t tref = sys_get_uptime_ms();

        while (not(pll->cs & PLL_CS_LOCK_BITS)) {
                if (sys_get_uptime_ms() - tref > 250) {
                        SET_BIT(pll->pwr, PLL_PWR_PD_BITS | PLL_PWR_VCOPD_BITS);
                        SET_BIT(resets_hw->reset, reset_flag);
                        return ETIME;
                }
        }

        pll->prim = ((postdiv1 << PLL_PRIM_POSTDIV1_LSB) & PLL_PRIM_POSTDIV1_BITS) |
                ((postdiv2 << PLL_PRIM_POSTDIV2_LSB) & PLL_PRIM_POSTDIV2_BITS);

        CLEAR_BIT(pll->pwr, PLL_PWR_POSTDIVPD_BITS);

        return 0;
}

//==============================================================================
/**
 * @brief  Set clk_ref source.
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
static int set_clk_ref_source(void)
{
        u32_t src = (__CLK_REF_CLK_SRC__ & CLOCKS_CLK_REF_CTRL_SRC_BITS) >> CLOCKS_CLK_REF_CTRL_SRC_LSB;

        u32_t auxsrc = (__CLK_REF_CLK_SRC__ & CLOCKS_CLK_REF_CTRL_AUXSRC_BITS) >>
                CLOCKS_CLK_REF_CTRL_AUXSRC_LSB;

        u32_t div = (__CLK_REF_CLK_DIV__ << CLOCKS_CLK_REF_DIV_INT_LSB);

        return clocks__configure_internal(clk_ref, src, auxsrc, div);
}

//==============================================================================
/**
 * @brief  Set clk_usb source.
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
static int set_clk_usb_source(void)
{
        u32_t src = 0;

        u32_t auxsrc = (__CLK_USB_CLK_SRC__ & CLOCKS_CLK_USB_CTRL_AUXSRC_BITS) >>
                CLOCKS_CLK_USB_CTRL_AUXSRC_LSB;

        u32_t div = (__CLK_USB_CLK_DIV__ << CLOCKS_CLK_USB_DIV_INT_LSB);

        return clocks__configure_internal(clk_usb, src, auxsrc, div);
}

//==============================================================================
/**
 * @brief  Set clk_adc source.
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
static int set_clk_adc_source(void)
{
        u32_t src = 0;

        u32_t auxsrc = (__CLK_ADC_CLK_SRC__ & CLOCKS_CLK_ADC_CTRL_AUXSRC_BITS) >>
                CLOCKS_CLK_ADC_CTRL_AUXSRC_LSB;

        u32_t div = (__CLK_ADC_CLK_DIV__ << CLOCKS_CLK_ADC_DIV_INT_LSB);

        return clocks__configure_internal(clk_adc, src, auxsrc, div);
}

//==============================================================================
/**
 * @brief  Set clk_rtc source.
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
static int set_clk_rtc_source(void)
{
        u32_t src = 0;

        u32_t auxsrc = (__CLK_RTC_CLK_SRC__ & CLOCKS_CLK_RTC_CTRL_AUXSRC_BITS) >>
                CLOCKS_CLK_RTC_CTRL_AUXSRC_LSB;

        u32_t div = (__CLK_RTC_CLK_DIV__ << CLOCKS_CLK_RTC_DIV_INT_LSB);

        return clocks__configure_internal(clk_rtc, src, auxsrc, div);
}

//==============================================================================
/**
 * @brief  Set clk_sys source.
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
static int set_clk_sys_source(void)
{
        u32_t src = (__CLK_SYS_CLK_SRC__ & CLOCKS_CLK_SYS_CTRL_SRC_BITS) >> CLOCKS_CLK_SYS_CTRL_SRC_LSB;

        u32_t auxsrc = (__CLK_SYS_CLK_SRC__ & CLOCKS_CLK_SYS_CTRL_AUXSRC_BITS) >>
                CLOCKS_CLK_SYS_CTRL_AUXSRC_LSB;

        u32_t div = (__CLK_SYS_CLK_DIV__ << CLOCKS_CLK_SYS_DIV_INT_LSB);

        return clocks__configure_internal(clk_sys, src, auxsrc, div);
}

//==============================================================================
/**
 * @brief  Set clk_peri source.
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
static int set_clk_peri_source(void)
{
        u32_t src = 0;

        u32_t auxsrc = (__CLK_PERI_CLK_SRC__ & CLOCKS_CLK_PERI_CTRL_AUXSRC_BITS) >>
                CLOCKS_CLK_PERI_CTRL_AUXSRC_LSB;

        u32_t div = 1;

        return clocks__configure_internal(clk_peri, src, auxsrc, div);
}

//==============================================================================
/**
 * @brief  Set clk_gpout0-3 source.
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
static int set_clk_gpout_source(clock_num_t clock)
{
        static const u32_t GPOUT_CLK_SRC[] = {
                __CLK_GPOUT0_CLK_SRC__,
                __CLK_GPOUT1_CLK_SRC__,
                __CLK_GPOUT2_CLK_SRC__,
                __CLK_GPOUT3_CLK_SRC__,
        };

        static const u32_t GPOUT_CLK_DIV[] = {
                __CLK_GPOUT0_CLK_DIV__ << CLOCKS_CLK_GPOUT0_DIV_INT_LSB,
                __CLK_GPOUT1_CLK_DIV__ << CLOCKS_CLK_GPOUT0_DIV_INT_LSB,
                __CLK_GPOUT2_CLK_DIV__ << CLOCKS_CLK_GPOUT0_DIV_INT_LSB,
                __CLK_GPOUT3_CLK_DIV__ << CLOCKS_CLK_GPOUT0_DIV_INT_LSB,
        };

        u32_t src = 0;

        u32_t auxsrc = (GPOUT_CLK_SRC[clock] & CLOCKS_CLK_GPOUT0_CTRL_AUXSRC_BITS) >>
                CLOCKS_CLK_GPOUT0_CTRL_AUXSRC_LSB;

        u32_t div = GPOUT_CLK_DIV[clock];

        return clocks__configure_internal(clock, src, auxsrc, div);
}

/*==============================================================================
  End of file
==============================================================================*/
