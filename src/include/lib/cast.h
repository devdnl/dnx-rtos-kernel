/*==============================================================================
@file    cast.h

@author  Daniel Zorychta

@brief   Casting macros.

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
\addtogroup dnx-misc-h
*/
/**@{*/

#ifndef _LIB_CAST_H_
#define _LIB_CAST_H_

/*==============================================================================
  Include files
==============================================================================*/

#ifdef __cplusplus
extern "C" {
#endif

/*==============================================================================
  Exported macros
==============================================================================*/
//==============================================================================
/**
 * @brief The macro casts variable <i>var</i> to type <i>void*</i>.
 * Use this macro to remove <b>const</b> attribute.
 *
 * @param type          casting type
 * @param var           variable to cast
 *
 * @b Example
 * @code
   // ...

   void func(char *str)
   {
          // ...
   }

   const char *str = "test";

   func(const_cast(str));

   // ...
 * @endcode
 */
//==============================================================================
#ifndef __cplusplus
#define const_cast(var) ((void *)(var))
#endif

//==============================================================================
/**
 * @brief The macro casts variable <i>var</i> to type <i>type</i>.
 * Use this macro to cast selected variable to any type.
 *
 * @param type          casting type
 * @param var           variable to cast
 *
 * @b Example
 * @code
   // ...

   void   *data   = calloc(1, 10);
   type_t *buffer = cast(type_t*, data);

   // ...
 * @endcode
 */
//==============================================================================
#define cast(type, var) ((type)(var))

/*==============================================================================
  Exported object types
==============================================================================*/

/*==============================================================================
  Exported objects
==============================================================================*/

/*==============================================================================
  Exported functions
==============================================================================*/

/*==============================================================================
  Exported inline functions
==============================================================================*/

#ifdef __cplusplus
}
#endif

#endif /* _LIB_CAST_H_ */
/**@}*/
/*==============================================================================
  End of file
==============================================================================*/
