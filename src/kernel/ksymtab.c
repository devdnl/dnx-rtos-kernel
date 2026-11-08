/*==============================================================================
@file    ksymtab.c

@author  Daniel Zorychta

@brief   Kernel symbol table (export table) used by drivers built separately (drvset).

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
#include "kernel/ksymtab.h"
#include <stddef.h>

#if (__OS_ENABLE_DRVSET__ > 0)

/*==============================================================================
  Local macros
==============================================================================*/

/*==============================================================================
  Local object types
==============================================================================*/

/*==============================================================================
  Local function prototypes
==============================================================================*/
/*
 * Symbols are declared as objects with assembler names of exported functions.
 * Real prototypes are not needed (only addresses), so conflicts with
 * compiler built-in functions (memcpy, strlen, ...) are avoided.
 */
/* functions exported under other kernel names (the same interface) */
#define sys_strlcpy _strlcpy
#define sys_strlcat _strlcat

#define _KSYM_STR2(name) #name
#define _KSYM_STR(name)  _KSYM_STR2(name)
#define _KSYM_DECL(name) extern const char _ksymref__##name[] __asm__(_KSYM_STR(name));
_KSYMTAB_LIST(_KSYM_DECL)
#undef _KSYM_DECL

/*==============================================================================
  Local objects
==============================================================================*/

/*==============================================================================
  Exported objects
==============================================================================*/
/* table pointer is stored in vector table entry _KVECTOR_KSYMTAB (kernel/kvectors.h) */
const _ksymtab_t _ksymtab = {
        .magic = _KSYMTAB_MAGIC,
        .version = _KSYMTAB_VERSION,
        .count = _KSYM_COUNT,
        .sym = {
#define _KSYM_ADDR(name) [_KSYM_IDX__##name] = _ksymref__##name,
                _KSYMTAB_LIST(_KSYM_ADDR)
#undef _KSYM_ADDR
        },
};

_Static_assert(offsetof(_ksymtab_t, sym) == _KSYMTAB_SYM_OFFSET, "_KSYMTAB_SYM_OFFSET mismatch");
_Static_assert(_KSYM_COUNT <= 1000, "symbol offset out of stub addressing range");

/*==============================================================================
  External objects
==============================================================================*/

/*==============================================================================
  Function definitions
==============================================================================*/

#endif /* __OS_ENABLE_DRVSET__ */
/*==============================================================================
  End of file
==============================================================================*/
