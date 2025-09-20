/*==============================================================================
@file    kworker.c

@author  Daniel Zorychta

@brief   Kernel worker process.

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

#pragma GCC optimize("Ofast")

/*==============================================================================
  Include files
==============================================================================*/
#include <string.h>
#include "config.h"
#include "fs/fsctrl.h"
#include "fs/vfs.h"
#include "drivers/drvctrl.h"
#include "kernel/syscall.h"
#include "kernel/kwrapper.h"
#include "kernel/printk.h"
#include "kernel/errno.h"
#include "kernel/time.h"
#include "kernel/khooks.h"
#include "kernel/sysfunc.h"
#include "lib/sys/types.h"
#include "lib/cast.h"
#include "lib/unarg.h"
#include "lib/misc.h"

/*==============================================================================
  Local macros
==============================================================================*/
#define FS_CACHE_SYNC_PERIOD_MS (1000 * __OS_SYSTEM_CACHE_SYNC_PERIOD__)

/* Stack size declared by linker script */
#define STACK_SIZE ((size_t) & __stack_size - 512)

/* Stack start declared by linker script */
#define STACK_START ((void *)&__stack_start)

/*==============================================================================
  Local object types
==============================================================================*/

/*==============================================================================
  Local function prototypes
==============================================================================*/

/*==============================================================================
  Local objects
==============================================================================*/
static _mm_region_t stack_region;

static const _process_attr_t kworker_attr = {
        .at_init      = NULL,
        .at_init_ctx  = NULL,
        .cwd          = "/",
        .app_ctx_size = 128,
        .fd_stdin     = -1,
        .fd_stdout    = -1,
        .fd_stderr    = -1,
        .p_stdin      = NULL,
        .p_stdout     = NULL,
        .p_stderr     = NULL,
        .priority     = _PRIORITY_NORMAL,
        .detached     = true,
};

/*==============================================================================
  Exported objects
==============================================================================*/
_process_t *_kworker_proc;

/*==============================================================================
  External objects
==============================================================================*/
/** pointer to stack size value */
extern void *__stack_size;

/** pointer to stack start */
extern void *__stack_start;

/*==============================================================================
  Function definitions
==============================================================================*/

//==============================================================================
/**
 * @brief  Initialize kworker.
 */
//==============================================================================
int _kworker_init(void)
{
        int err = _process_create("kworker", &kworker_attr, NULL);
        if (!err) {
                err = _process_create(__OS_INIT_PROG__, &kworker_attr, NULL);
        }

        return err;
}

//==============================================================================
/**
 * @brief  Main kworker process [KERNELSPACE].
 *
 * @param  argc         argument count
 * @param  argv         arguments
 *
 * @return Never exit (0)
 */
//==============================================================================
int _kworker_process(int argc, char *argv[])
{
        UNUSED_ARG2(argc, argv);

        _mm_register_region(&stack_region, STACK_START, STACK_SIZE, _CPUCTL_STACK_REGION_FLAGS, "SRAM");

        _task_get_process_container(_THIS_TASK, &_kworker_proc, NULL);
        _task_set_priority(_THIS_TASK, _PRIORITY_HIGHEST);

        clock_t sync_period_ref = _kernel_get_uptime_ms();

        for (;;) {
                _sleep_ms(1000);

                if ((_kernel_get_uptime_ms() - sync_period_ref) >= FS_CACHE_SYNC_PERIOD_MS) {
                        _vfs_sync();
                        sync_period_ref = _kernel_get_uptime_ms();
                }

                _process_clean_up_killed_processes();
        }

        return -1;
}

/*==============================================================================
  End of file
==============================================================================*/
