/*==============================================================================
@file    misc.h

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

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/*==============================================================================
  Include files
==============================================================================*/
#include "config.h"
#include "lib/sys/types.h"
#include "hardware/structs/clocks.h"

/*==============================================================================
  Exported symbolic constants/macros
==============================================================================*/

/*==============================================================================
  Exported types, enums definitions
==============================================================================*/

/*==============================================================================
  Exported objects
==============================================================================*/
extern u32_t clocks__get_gpclk_frequency(u8_t clock);
extern u32_t clocks__get_usb_pll_frequency(void);
extern u32_t clocks__get_sys_pll_frequency(void);
extern u32_t clocks__get_xosc_frequency(void);
extern u32_t clocks__get_rosc_frequency(void);
extern u32_t clocks__get_clk_gpout_frequency(u8_t clock);
extern u32_t clocks__get_clk_adc_frequency(void);
extern u32_t clocks__get_clk_usb_frequency(void);
extern u32_t clocks__get_clk_rtc_frequency(void);
extern u32_t clocks__get_clk_peri_frequency(void);
extern u32_t clocks__get_clk_sys_frequency(void);
extern u32_t clocks__get_clk_ref_aux_frequency(void);
extern u32_t clocks__get_clk_ref_frequency(void);
extern u32_t clocks__get_frequency(clock_num_t clock);
extern int   clocks__configure_internal(clock_num_t clock, uint32_t src, uint32_t auxsrc, uint32_t div);

/*==============================================================================
  Exported function prototypes
==============================================================================*/

#ifdef __cplusplus
}
#endif

/*==============================================================================
  End of file
==============================================================================*/
