/*==============================================================================
File     strlcpy.c

Author   Daniel Zorychta

Brief    String functions.

         Copyright (C) 2026 Daniel Zorychta <daniel.zorychta@gmail.com>

         This program is free software; you can redistribute it and/or modify
         it under the terms of the GNU General Public License version 2 as
         published by the Free Software Foundation.

         The dnx RTOS is distributed in the hope that it will be useful,
         but WITHOUT ANY WARRANTY; without even the implied warranty of
         MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
         GNU General Public License for more details.

         Programs using the kernel by system calls are not covered by the license,
         drivers and file systems of drvset partition: exception, see the license
         file: doc/license.txt.


==============================================================================*/

/*==============================================================================
  Include files
==============================================================================*/
#include <string.h>
#include "lib/strlcpy.h"

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
 * @brief The _strlcpy() function copy strings.
 *
 * The sys_strlcpy() function copies up to size - 1 characters from the NUL-terminated
 * string src to dst, NUL-terminating the result.
 *
 * Note, however, that if _strlcat() traverses size characters without finding
 * a NUL, the length of the string is considered to be size and the destination
 * string will not be NUL-terminated (since there was no space for the NUL).
 *
 * @param  dst  destination buffer
 * @param  src  source buffer
 * @param  size destination buffer size
 *
 * @return The _strlcpy() function return the total length of the string they
 *         tried to create. For _strlcpy() that means the length of src.
 */
//==============================================================================
size_t _strlcpy(char *dst, const char *src, size_t size)
{
        char *d = dst;
        const char *s = src;
        size_t n = size;

        /* Copy as many bytes as will fit */
        if (n != 0 && --n != 0) {
                do {
                        if ((*d++ = *s++) == 0) {
                                break;
                        }
                } while (--n != 0);
        }

        /* Not enough room in dst, add NUL and traverse rest of src */
        if (n == 0) {
                if (size != 0) {
                        *d = '\0';
                }
                while (*s++)
                        ;
        }

        /* count does not include NUL */
        return (s - src - 1);
}

/*==============================================================================
  End of file
==============================================================================*/
