/*==============================================================================
@file    vfprintf.c

@author  Daniel Zorychta

@brief   Print functions.

@note    Copyright (C) 2026 Daniel Zorychta <daniel.zorychta@gmail.com>

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
#include "config.h"
#include "lib/misc.h"
#include "lib/vfprintf.h"
#include "lib/vsnprintf.h"
#include "lib/cast.h"
#include "mm/mm.h"
#include "kernel/errno.h"

/*==============================================================================
  Local macros
==============================================================================*/

/*==============================================================================
  Local object types
==============================================================================*/

/*==============================================================================
  Local function prototypes
==============================================================================*/

/*==============================================================================
  Local objects
==============================================================================*/

/*==============================================================================
  Exported objects
==============================================================================*/

/*==============================================================================
  External objects
==============================================================================*/

/*==============================================================================
  Function definitions
==============================================================================*/

//==============================================================================
/**
 * @brief Function write to file formatted string. The message is written by
 *        single write operation.
 *
 * @param file                file
 * @param format              formated text
 * @param arg                 arguments
 *
 * @return Number of characters written to the file.
 */
//==============================================================================
int _vfprintf(kfile_t *file, const char *format, va_list arg)
{
        int n = 0;

        if (file && format) {
                va_list carg;
                va_copy(carg, arg);
                int len = _vsnprintf(NULL, 0, format, carg);
                va_end(carg);

                if (len > 0) {
                        char *str = NULL;
                        int err = _kmalloc(_MM_KRN, len + 1, NULL, _MM_FLAG__DMA, _MM_FLAG__DMA, cast(void *, &str));
                        if (!err && str) {
                                _vsnprintf(str, len + 1, format, arg);

                                size_t wrcnt = 0;
                                if (_vfs_fwrite(str, len, &wrcnt, file) == ESUCC) {
                                        n = wrcnt;
                                }

                                _kfree(_MM_KRN, cast(void *, &str));
                        }
                }
        }

        return n;
}

/*==============================================================================
  End of file
==============================================================================*/
