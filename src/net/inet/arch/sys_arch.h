/*==============================================================================
@file    sys_arch.h

@author  Daniel Zorychta

@brief

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

#ifndef _SYS_ARCH_H_
#define _SYS_ARCH_H_

#ifdef __cplusplus
extern "C" {
#endif

/*==============================================================================
  Include files
==============================================================================*/
#include "kernel/sysfunc.h"

/*==============================================================================
  Exported macros
=============================================================================*/
#define SYS_MBOX_NULL NULL
#define SYS_SEM_NULL  NULL

/*==============================================================================
  Exported object types
==============================================================================*/
typedef tid_t sys_thread_t;
typedef int sys_prot_t;
typedef kmtx_t *sys_mutex_t;
typedef ksem_t *sys_sem_t;
typedef kqueue_t *sys_mbox_t;

/*==============================================================================
  Exported objects
==============================================================================*/

/*==============================================================================
  Exported functions
==============================================================================*/
extern void lwip_sys_free(void *mem);
extern void *lwip_sys_malloc(size_t blksz);
extern void *lwip_sys_calloc(size_t n, size_t blksz);
extern int lwip_sys_rand(void);
extern int lwip_sys_sem_valid(sys_sem_t *sem);
extern void lwip_sys_sem_set_invalid(sys_sem_t *sem);
extern int lwip_sys_mbox_valid(sys_mbox_t *mbox);
extern u32_t lwip_sys_arch_mbox_tryfetch(sys_mbox_t *mbox, void **msg);
extern int lwip_sys_mbox_valid(sys_mbox_t *mbox);
extern void lwip_sys_mbox_set_invalid(sys_mbox_t *mbox);
extern u32_t lwip_sys_hook_tcp_isn(void *local_ip, u16_t local_port, void *remote_ip, u16_t remote_port);

/*==============================================================================
  Exported inline functions
==============================================================================*/

#ifdef __cplusplus
}
#endif

#endif /* _SYS_ARCH_H_ */
/*==============================================================================
  End of file
==============================================================================*/
