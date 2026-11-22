/*==============================================================================
@file    assert.h

@author  Daniel Zorychta

@brief   This file provides assert macro.

@note    Copyright (C) 2026 Daniel Zorychta <daniel.zorychta@gmail.com>

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

/**
\defgroup assert-h <assert.h>

The library provides assert macro.

*/
/**@{*/

#ifndef _ASSERT_H_
#define _ASSERT_H_

#ifdef __cplusplus
extern "C" {
#endif

/*==============================================================================
  Include files
==============================================================================*/
#include <config.h>
#include <stdlib.h>

/*==============================================================================
  Exported macros
==============================================================================*/
//==============================================================================
/**
 * @brief Macro creates assertion functionality.
 *
 * Aborts the program if user-defined condition is not <b>true</b>. Macro can be
 * disabled if <b>NDEBUG</b> macro is defined.
 *
 * @param ignore        user-defined condition
 *
 * @b Example @b 1
 * @code
        #include <assert.h>

        // ...

        void some_function(int *number)
        {
                assert(number != NULL);      // pointer to number must be not NULL

                // ...
        }

        // ...
   @endcode
 *
 * @b Example @b 2
 * @code
         #define NDEBUG
         #include <assert.h>

         // all assert macros are disabled

         assert(false); // has no effect for program flow

         // ...
   @endcode
 */
//==============================================================================
#if !defined(NDEBUG)
#ifdef DOXYGEN
#define assert(ignore)
#else
#define assert(ignore)                                                                           \
        do {                                                                                     \
                if ((ignore))                                                                    \
                        break;                                                                   \
                printf("Assertion failed: %s, file %s, line %d\n", #ignore, __FILE__, __LINE__); \
                abort();                                                                         \
        } while (0)
#endif
#endif

#ifndef DOXYGEN
#define static_assert(_expr, _msg) _Static_assert(_expr, _msg)
#endif

/*==============================================================================
  Exported object types
==============================================================================*/

/*==============================================================================
  Exported objects
==============================================================================*/

/*==============================================================================
  Exported functions
==============================================================================*/

#ifdef __cplusplus
}
#endif

#endif /* _ASSERT_H_ */

/**@}*/
/*==============================================================================
  End of file
==============================================================================*/
