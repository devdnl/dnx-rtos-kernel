/*==============================================================================
File     strchrrep.c

Author   Daniel Zorychta

Brief    String character replace.

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
#include "lib/strchrrep.h"

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
 * @brief  Function replace characters in string.
 *
 * @param  str          string (in/out buffer)
 * @param  from         find character
 * @param  to           replace character
 *
 * @return Number of replaced characters.
 */
//==============================================================================
int _strchrrep(char *str, char from, char to)
{
        int repl = 0;

        while (str && *str != '\0') {
                if (*str == from) {
                        *str = to;
                        repl++;
                }
                str++;
        }

        return repl;
}

/*==============================================================================
  End of file
==============================================================================*/
