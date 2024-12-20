/*==============================================================================
@file    misc.c

@author  Daniel Zorychta

@brief   Miscellaneous CPU functions.

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

#pragma GCC optimize("Og")

/*==============================================================================
  Include files
==============================================================================*/
#include "misc.h"
#include "lib/cast.h"
#include "lib/misc.h"
#include "hardware/regs/clocks.h"
#include "hardware/regs/pll.h"
#include "hardware/regs/addressmap.h"
#include "hardware/structs/clocks.h"
#include "hardware/structs/pll.h"
#include "hardware/regs/xosc.h"
#include "hardware/structs/xosc.h"
#include "lib/CMSIS/core_cm0plus.h"

/*==============================================================================
  Local symbolic constants/macros
==============================================================================*/

/*==============================================================================
  Local types, enums definitions
==============================================================================*/

/*==============================================================================
  Local function prototypes
==============================================================================*/

/*==============================================================================
  Local object definitions
==============================================================================*/

/*==============================================================================
  External object definitions
==============================================================================*/

/*==============================================================================
  Function definitions
==============================================================================*/

//==============================================================================
/**
 * @brief  Calculate PLL frequency.
 *
 * @param  fref         reference frequency
 * @param  refdiv       reference frequency divider
 * @param  fbdiv        feedback divider
 * @param  postdiv1     post divider 1
 * @param  postdiv2     post divider 2
 *
 * @return Calculated frequency.
 */
//==============================================================================
static u32_t calculate_pll_freq(u32_t fref, u32_t refdiv, u32_t fbdiv, u32_t postdiv1, u32_t postdiv2)
{
        return (fref / refdiv) * fbdiv / (postdiv1 * postdiv2);
}

//==============================================================================
/**
 * @brief  Return GPCLK0-1 frequency.
 *
 * @return Frequency in Hz.
 */
//==============================================================================
u32_t clocks__get_gpclk_frequency(u8_t clock)
{
        if (clock == 0) {
                return __CLK_GPCLK0_FREQ__;

        } else if (clock == 1) {
                return __CLK_GPCLK1_FREQ__;

        } else {
                return 0;
        }
}

//==============================================================================
/**
 * @brief  Return USB PLL frequency.
 *
 * @return Frequency in Hz.
 */
//==============================================================================
u32_t clocks__get_usb_pll_frequency(void)
{
        if (pll_usb_hw->pwr & (PLL_PWR_VCOPD_BITS | PLL_PWR_POSTDIVPD_BITS | PLL_PWR_PD_BITS)) {
                return 0;
        }

        u32_t fref     = clocks__get_xosc_frequency();
        u32_t refdiv   = (pll_usb_hw->cs & PLL_CS_REFDIV_BITS) >> PLL_CS_REFDIV_LSB;
        u32_t fbdiv    = (pll_usb_hw->fbdiv_int & PLL_FBDIV_INT_BITS) >> PLL_FBDIV_INT_LSB;
        u32_t postdiv1 = (pll_usb_hw->prim & PLL_PRIM_POSTDIV1_BITS) >> PLL_PRIM_POSTDIV1_LSB;
        u32_t postdiv2 = (pll_usb_hw->prim & PLL_PRIM_POSTDIV2_BITS) >> PLL_PRIM_POSTDIV2_LSB;

        if (pll_usb_hw->cs & PLL_CS_BYPASS_BITS) {
                return fref;

        } else {
                return calculate_pll_freq(fref, refdiv, fbdiv, postdiv1, postdiv2);
        }
}

//==============================================================================
/**
 * @brief  Return SYS PLL frequency.
 *
 * @return Frequency in Hz.
 */
//==============================================================================
u32_t clocks__get_sys_pll_frequency(void)
{
        if (pll_sys_hw->pwr & (PLL_PWR_VCOPD_BITS | PLL_PWR_POSTDIVPD_BITS | PLL_PWR_PD_BITS)) {
                return 0;
        }

        u32_t fref     = clocks__get_xosc_frequency();
        u32_t refdiv   = (pll_sys_hw->cs & PLL_CS_REFDIV_BITS) >> PLL_CS_REFDIV_LSB;
        u32_t fbdiv    = (pll_sys_hw->fbdiv_int & PLL_FBDIV_INT_BITS) >> PLL_FBDIV_INT_LSB;
        u32_t postdiv1 = (pll_sys_hw->prim & PLL_PRIM_POSTDIV1_BITS) >> PLL_PRIM_POSTDIV1_LSB;
        u32_t postdiv2 = (pll_sys_hw->prim & PLL_PRIM_POSTDIV2_BITS) >> PLL_PRIM_POSTDIV2_LSB;

        if (pll_sys_hw->cs & PLL_CS_BYPASS_BITS) {
                return fref;

        } else {
                return calculate_pll_freq(fref, refdiv, fbdiv, postdiv1, postdiv2);
        }
}

//==============================================================================
/**
 * @brief  Return XOSC frequency.
 *
 * @return Frequency in Hz.
 */
//==============================================================================
u32_t clocks__get_xosc_frequency(void)
{
        if (xosc_hw->status & XOSC_STATUS_STABLE_BITS) {
                return __CPU_OSC_FREQ__;
        } else {
                return 0;
        }
}

//==============================================================================
/**
 * @brief  Return XOSC frequency.
 *
 * @return Frequency in Hz.
 */
//==============================================================================
u32_t clocks__get_rosc_frequency(void)
{
        return 6000000UL;
}

//==============================================================================
/**
 * @brief  Return GPIO Muxing clock frequency (clk_gpout0_3).
 *
 * @return Frequency in Hz.
 */
//==============================================================================
u32_t clocks__get_clk_gpout_frequency(u8_t clock)
{
        if (clock > (clk_gpout3 - clk_gpout0)) {
                return 0;
        }

        u32_t auxsrc = (clocks_hw->clk[clk_gpout0 + clock].ctrl & CLOCKS_CLK_GPOUT0_CTRL_AUXSRC_BITS) >>
                CLOCKS_CLK_GPOUT0_CTRL_AUXSRC_LSB;
        u32_t div  = clocks_hw->clk[clk_gpout0 + clock].div;
        u32_t freq = 0;

        switch (auxsrc) {
        case CLOCKS_CLK_GPOUT0_CTRL_AUXSRC_VALUE_CLKSRC_PLL_SYS:
                freq = clocks__get_sys_pll_frequency();
                break;
        case CLOCKS_CLK_GPOUT0_CTRL_AUXSRC_VALUE_CLKSRC_GPIN0:
                freq = clocks__get_gpclk_frequency(0);
                break;
        case CLOCKS_CLK_GPOUT0_CTRL_AUXSRC_VALUE_CLKSRC_GPIN1:
                freq = clocks__get_gpclk_frequency(1);
                break;
        case CLOCKS_CLK_GPOUT0_CTRL_AUXSRC_VALUE_CLKSRC_PLL_USB:
                freq = clocks__get_usb_pll_frequency();
                break;
        case CLOCKS_CLK_GPOUT0_CTRL_AUXSRC_VALUE_ROSC_CLKSRC:
                freq = clocks__get_rosc_frequency();
                break;
        case CLOCKS_CLK_GPOUT0_CTRL_AUXSRC_VALUE_XOSC_CLKSRC:
                freq = clocks__get_xosc_frequency();
                break;
        case CLOCKS_CLK_GPOUT0_CTRL_AUXSRC_VALUE_CLK_SYS:
                freq = clocks__get_clk_sys_frequency();
                break;
        case CLOCKS_CLK_GPOUT0_CTRL_AUXSRC_VALUE_CLK_USB:
                freq = clocks__get_clk_usb_frequency();
                break;
        case CLOCKS_CLK_GPOUT0_CTRL_AUXSRC_VALUE_CLK_ADC:
                freq = clocks__get_clk_adc_frequency();
                break;
        case CLOCKS_CLK_GPOUT0_CTRL_AUXSRC_VALUE_CLK_RTC:
                freq = clocks__get_clk_rtc_frequency();
                break;
        case CLOCKS_CLK_GPOUT0_CTRL_AUXSRC_VALUE_CLK_REF:
                freq = clocks__get_clk_ref_frequency();
                break;
        default:
                freq = 0;
        }

        return (cast(u64_t, freq) * 256) / div;
}

//==============================================================================
/**
 * @brief  Return ADC clock frequency (clk_adc).
 *
 * @return Frequency in Hz.
 */
//==============================================================================
u32_t clocks__get_clk_adc_frequency(void)
{
        if (!(clocks_hw->clk[clk_adc].ctrl & CLOCKS_CLK_ADC_CTRL_ENABLE_BITS)) {
                return 0;
        }

        u32_t auxsrc = (clocks_hw->clk[clk_adc].ctrl & CLOCKS_CLK_ADC_CTRL_AUXSRC_BITS) >>
                CLOCKS_CLK_ADC_CTRL_AUXSRC_LSB;
        u32_t div = (clocks_hw->clk[clk_adc].div & CLOCKS_CLK_ADC_DIV_INT_BITS) >> CLOCKS_CLK_ADC_DIV_INT_LSB;
        u32_t freq = 0;

        switch (auxsrc) {
        case CLOCKS_CLK_ADC_CTRL_AUXSRC_VALUE_CLKSRC_PLL_USB:
                freq = clocks__get_usb_pll_frequency();
                break;
        case CLOCKS_CLK_ADC_CTRL_AUXSRC_VALUE_CLKSRC_PLL_SYS:
                freq = clocks__get_sys_pll_frequency();
                break;
        case CLOCKS_CLK_ADC_CTRL_AUXSRC_VALUE_ROSC_CLKSRC_PH:
                freq = clocks__get_rosc_frequency();
                break;
        case CLOCKS_CLK_ADC_CTRL_AUXSRC_VALUE_XOSC_CLKSRC:
                freq = clocks__get_xosc_frequency();
                break;
        case CLOCKS_CLK_ADC_CTRL_AUXSRC_VALUE_CLKSRC_GPIN0:
                freq = clocks__get_gpclk_frequency(0);
                break;
        case CLOCKS_CLK_ADC_CTRL_AUXSRC_VALUE_CLKSRC_GPIN1:
                freq = clocks__get_gpclk_frequency(1);
                break;
        default:
                freq = 0;
        }

        return freq / div;
}

//==============================================================================
/**
 * @brief  Return USB clock frequency (clk_usb).
 *
 * @return Frequency in Hz.
 */
//==============================================================================
u32_t clocks__get_clk_usb_frequency(void)
{
        if (!(clocks_hw->clk[clk_usb].ctrl & CLOCKS_CLK_USB_CTRL_ENABLE_BITS)) {
                return 0;
        }

        u32_t auxsrc = (clocks_hw->clk[clk_usb].ctrl & CLOCKS_CLK_USB_CTRL_AUXSRC_BITS) >>
                CLOCKS_CLK_USB_CTRL_AUXSRC_LSB;
        u32_t div = (clocks_hw->clk[clk_usb].div & CLOCKS_CLK_USB_DIV_INT_BITS) >> CLOCKS_CLK_USB_DIV_INT_LSB;
        u32_t freq = 0;

        switch (auxsrc) {
        case CLOCKS_CLK_USB_CTRL_AUXSRC_VALUE_CLKSRC_PLL_USB:
                freq = clocks__get_usb_pll_frequency();
                break;
        case CLOCKS_CLK_USB_CTRL_AUXSRC_VALUE_CLKSRC_PLL_SYS:
                freq = clocks__get_sys_pll_frequency();
                break;
        case CLOCKS_CLK_USB_CTRL_AUXSRC_VALUE_ROSC_CLKSRC_PH:
                freq = clocks__get_rosc_frequency();
                break;
        case CLOCKS_CLK_USB_CTRL_AUXSRC_VALUE_XOSC_CLKSRC:
                freq = clocks__get_xosc_frequency();
                break;
        case CLOCKS_CLK_USB_CTRL_AUXSRC_VALUE_CLKSRC_GPIN0:
                freq = clocks__get_gpclk_frequency(0);
                break;
        case CLOCKS_CLK_USB_CTRL_AUXSRC_VALUE_CLKSRC_GPIN1:
                freq = clocks__get_gpclk_frequency(1);
                break;
        default:
                freq = 0;
        }

        return freq / div;
}

//==============================================================================
/**
 * @brief  Return RTC clock frequency (clk_rtc).
 *
 * @return Frequency in Hz.
 */
//==============================================================================
u32_t clocks__get_clk_rtc_frequency(void)
{
        if (!(clocks_hw->clk[clk_rtc].ctrl & CLOCKS_CLK_RTC_CTRL_ENABLE_BITS)) {
                return 0;
        }

        u32_t auxsrc = (clocks_hw->clk[clk_rtc].ctrl & CLOCKS_CLK_RTC_CTRL_AUXSRC_BITS) >>
                CLOCKS_CLK_RTC_CTRL_AUXSRC_LSB;
        u32_t div  = (clocks_hw->clk[clk_rtc].div & CLOCKS_CLK_RTC_DIV_BITS);
        u32_t freq = 0;

        switch (auxsrc) {
        case CLOCKS_CLK_RTC_CTRL_AUXSRC_VALUE_CLKSRC_PLL_USB:
                freq = clocks__get_usb_pll_frequency();
                break;
        case CLOCKS_CLK_RTC_CTRL_AUXSRC_VALUE_CLKSRC_PLL_SYS:
                freq = clocks__get_sys_pll_frequency();
                break;
        case CLOCKS_CLK_RTC_CTRL_AUXSRC_VALUE_ROSC_CLKSRC_PH:
                freq = clocks__get_rosc_frequency();
                break;
        case CLOCKS_CLK_RTC_CTRL_AUXSRC_VALUE_XOSC_CLKSRC:
                freq = clocks__get_xosc_frequency();
                break;
        case CLOCKS_CLK_RTC_CTRL_AUXSRC_VALUE_CLKSRC_GPIN0:
                freq = clocks__get_gpclk_frequency(0);
                break;
        case CLOCKS_CLK_RTC_CTRL_AUXSRC_VALUE_CLKSRC_GPIN1:
                freq = clocks__get_gpclk_frequency(1);
                break;
        default:
                freq = 0;
        }

        return (cast(u64_t, freq) * 256) / div;
}

//==============================================================================
/**
 * @brief  Return UART/SPI clock frequency (clk_peri).
 *
 * @return Frequency in Hz.
 */
//==============================================================================
u32_t clocks__get_clk_peri_frequency(void)
{
        if (!(clocks_hw->clk[clk_peri].ctrl & CLOCKS_CLK_PERI_CTRL_ENABLE_BITS)) {
                return 0;
        }

        u32_t auxsrc = (clocks_hw->clk[clk_peri].ctrl & CLOCKS_CLK_PERI_CTRL_AUXSRC_BITS) >>
                CLOCKS_CLK_PERI_CTRL_AUXSRC_LSB;
        u32_t freq = 0;

        switch (auxsrc) {
        case CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLK_SYS:
                freq = clocks__get_clk_sys_frequency();
                break;
        case CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLKSRC_PLL_SYS:
                freq = clocks__get_sys_pll_frequency();
                break;
        case CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLKSRC_PLL_USB:
                freq = clocks__get_usb_pll_frequency();
                break;
        case CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_ROSC_CLKSRC_PH:
                freq = clocks__get_rosc_frequency();
                break;
        case CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_XOSC_CLKSRC:
                freq = clocks__get_xosc_frequency();
                break;
        case CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLKSRC_GPIN0:
                freq = clocks__get_gpclk_frequency(0);
                break;
        case CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLKSRC_GPIN1:
                freq = clocks__get_gpclk_frequency(1);
                break;
        default:
                freq = 0;
        }

        return freq;
}

//==============================================================================
/**
 * @brief  Return CPU/BUS/memory clock frequency (clk_sys).
 *
 * @return Frequency in Hz.
 */
//==============================================================================
u32_t clocks__get_clk_sys_frequency(void)
{
        u32_t div  = (clocks_hw->clk[clk_sys].div & CLOCKS_CLK_SYS_DIV_BITS);
        u32_t freq = 0;

        if (clocks_hw->clk[clk_sys].ctrl & CLOCKS_CLK_SYS_CTRL_SRC_BITS) {
                u32_t auxsrc = (clocks_hw->clk[clk_sys].ctrl & CLOCKS_CLK_SYS_CTRL_AUXSRC_BITS) >>
                        CLOCKS_CLK_SYS_CTRL_AUXSRC_LSB;

                switch (auxsrc) {
                case CLOCKS_CLK_SYS_CTRL_AUXSRC_VALUE_CLKSRC_PLL_SYS:
                        freq = clocks__get_sys_pll_frequency();
                        break;
                case CLOCKS_CLK_SYS_CTRL_AUXSRC_VALUE_CLKSRC_PLL_USB:
                        freq = clocks__get_usb_pll_frequency();
                        break;
                case CLOCKS_CLK_SYS_CTRL_AUXSRC_VALUE_ROSC_CLKSRC:
                        freq = clocks__get_rosc_frequency();
                        break;
                case CLOCKS_CLK_SYS_CTRL_AUXSRC_VALUE_XOSC_CLKSRC:
                        freq = clocks__get_xosc_frequency();
                        break;
                case CLOCKS_CLK_SYS_CTRL_AUXSRC_VALUE_CLKSRC_GPIN0:
                        freq = clocks__get_gpclk_frequency(0);
                        break;
                case CLOCKS_CLK_SYS_CTRL_AUXSRC_VALUE_CLKSRC_GPIN1:
                        freq = clocks__get_gpclk_frequency(1);
                        break;
                default:
                        freq = 0;
                }

        } else {
                freq = clocks__get_clk_ref_frequency();
        }

        return (cast(u64_t, freq) * 256) / div;
}

//==============================================================================
/**
 * @brief  Return Watchdog/Timers clock frequency (clk_ref_aux).
 *
 * @return Frequency in Hz.
 */
//==============================================================================
u32_t clocks__get_clk_ref_aux_frequency(void)
{
        u32_t auxsrc = (clocks_hw->clk[clk_ref].ctrl & CLOCKS_CLK_REF_CTRL_AUXSRC_BITS) >>
                CLOCKS_CLK_REF_CTRL_AUXSRC_LSB;
        u32_t div = (clocks_hw->clk[clk_ref].div & CLOCKS_CLK_REF_DIV_INT_BITS) >> CLOCKS_CLK_REF_DIV_INT_LSB;
        u32_t freq = 0;

        switch (auxsrc) {
        case CLOCKS_CLK_REF_CTRL_AUXSRC_VALUE_CLKSRC_PLL_USB:
                freq = clocks__get_usb_pll_frequency();
                break;
        case CLOCKS_CLK_REF_CTRL_AUXSRC_VALUE_CLKSRC_GPIN0:
                freq = clocks__get_gpclk_frequency(0);
                break;
        case CLOCKS_CLK_REF_CTRL_AUXSRC_VALUE_CLKSRC_GPIN1:
                freq = clocks__get_gpclk_frequency(1);
                break;
        default:
                freq = 0;
        }

        return freq / div;
}

//==============================================================================
/**
 * @brief  Return Watchdog/Timers clock frequency (clk_ref).
 *
 * @return Frequency in Hz.
 */
//==============================================================================
u32_t clocks__get_clk_ref_frequency(void)
{
        u32_t auxsrc = (clocks_hw->clk[clk_ref].ctrl & CLOCKS_CLK_REF_CTRL_SRC_BITS) >>
                CLOCKS_CLK_REF_CTRL_SRC_LSB;
        u32_t div = (clocks_hw->clk[clk_ref].div & CLOCKS_CLK_REF_DIV_INT_BITS) >> CLOCKS_CLK_REF_DIV_INT_LSB;
        u32_t freq = 0;

        switch (auxsrc) {
        case CLOCKS_CLK_REF_CTRL_SRC_VALUE_ROSC_CLKSRC_PH:
                freq = clocks__get_rosc_frequency();
                break;
        case CLOCKS_CLK_REF_CTRL_SRC_VALUE_CLKSRC_CLK_REF_AUX:
                freq = clocks__get_clk_ref_aux_frequency();
                break;
        case CLOCKS_CLK_REF_CTRL_SRC_VALUE_XOSC_CLKSRC:
                freq = clocks__get_xosc_frequency();
                break;
        default:
                freq = 0;
        }

        return freq / div;
}

//==============================================================================
/**
 * @brief  Return frequency of selected clock.
 *
 * @param  clock        clock number
 *
 * @return Frequency in Hz.
 */
//==============================================================================
u32_t clocks__get_frequency(clock_num_t clock)
{
        switch (clock) {
        case clk_gpout0:
        case clk_gpout1:
        case clk_gpout2:
        case clk_gpout3:
                return clocks__get_clk_gpout_frequency(clock);
        case clk_ref:
                return clocks__get_clk_ref_frequency();
        case clk_sys:
                return clocks__get_clk_sys_frequency();
        case clk_peri:
                return clocks__get_clk_peri_frequency();
        case clk_usb:
                return clocks__get_clk_usb_frequency();
        case clk_adc:
                return clocks__get_clk_adc_frequency();
        case clk_rtc:
                return clocks__get_clk_rtc_frequency();
        default:
                return 0;
        }
}

//==============================================================================
/**
 * @brief  Change clock source.
 *
 * @param  clock        clock number
 * @param  src          clock source
 * @param  auxsrc       auxilary clock source
 * @param  div          divider
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
int clocks__configure_internal(clock_num_t clock, uint32_t src, uint32_t auxsrc, uint32_t div)
{
        clock_hw_t *clock_hw = &clocks_hw->clk[clock];

        bool has_glitchless_mux = ((clock == clk_sys) or (clock == clk_ref));

        // If increasing divisor, set divisor before source. Otherwise set source
        // before divisor. This avoids a momentary overspeed when e.g. switching
        // to a faster source and increasing divisor to compensate.
        if (div > clock_hw->div) {
                clock_hw->div = div;
        }

        // If switching a glitchless slice (ref or sys) to an aux source, switch
        // away from aux *first* to avoid passing glitches when changing aux mux.
        // Assume (!!!) glitchless source 0 is no faster than the aux source.
        if (has_glitchless_mux and (src == CLOCKS_CLK_SYS_CTRL_SRC_VALUE_CLKSRC_CLK_SYS_AUX)) {
                hw_clear_bits(&clock_hw->ctrl, CLOCKS_CLK_REF_CTRL_SRC_BITS);
                while (!(clock_hw->selected & 1u)) {
                }
        }
        // If no glitchless mux, cleanly stop the clock to avoid glitches
        // propagating when changing aux mux. Note it would be a really bad idea
        // to do this on one of the glitchless clocks (clk_sys, clk_ref).
        else {
                // Disable clock. On clk_ref and clk_sys this does nothing,
                // all other clocks have the ENABLE bit in the same position.
                hw_clear_bits(&clock_hw->ctrl, CLOCKS_CLK_GPOUT0_CTRL_ENABLE_BITS);
                if (clocks__get_frequency(clock) > 0) {
                        // Delay for 3 cycles of the target clock, for ENABLE propagation.
                        // Note XOSC_COUNT is not helpful here because XOSC is not
                        // necessarily running, nor is timer...
                        uint delay_cyc = (clocks__get_frequency(clk_sys) / clocks__get_frequency(clock) + 1);
                        for (uint_fast32_t i = 0; i < (delay_cyc * 3); i++) {
                                __NOP();
                        }
                }
        }

        // Set aux mux first, and then glitchless mux if this clock has one
        hw_write_masked(&clock_hw->ctrl, (auxsrc << CLOCKS_CLK_SYS_CTRL_AUXSRC_LSB),
                CLOCKS_CLK_SYS_CTRL_AUXSRC_BITS);

        if (has_glitchless_mux) {
                hw_write_masked(&clock_hw->ctrl, src << CLOCKS_CLK_REF_CTRL_SRC_LSB,
                        CLOCKS_CLK_REF_CTRL_SRC_BITS);

                while (!(clock_hw->selected & (1u << src))) {
                }
        }

        // Enable clock. On clk_ref and clk_sys this does nothing,
        // all other clocks have the ENABLE bit in the same position.
        hw_set_bits(&clock_hw->ctrl, CLOCKS_CLK_GPOUT0_CTRL_ENABLE_BITS);

        // Now that the source is configured, we can trust that the user-supplied
        // divisor is a safe value.
        clock_hw->div = div;

        return 0;
}

/*==============================================================================
  End of file
==============================================================================*/
