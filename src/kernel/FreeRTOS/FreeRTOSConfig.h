/*
 * FreeRTOS Kernel V11.1.0
 * Copyright (C) 2021 Amazon.com, Inc. or its affiliates. All Rights Reserved.
 *
 * SPDX-License-Identifier: MIT
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * https://www.FreeRTOS.org
 * https://github.com/FreeRTOS
 *
 */

#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

#include <stdbool.h>
#include "config.h"     /* general configuration  */
#if defined(_DNX_HOST_)
/* files compiled with host headers (POSIX simulator) cannot use dnx headers */
#define _CPUCTL_CORE_NUM 1
#else
#include "cpu/cpuctl.h" /* CPU vector definitions */
#include "mm/mm.h"      /* memory management      */
#endif

/*-------------------------------------------------------------
 * Used prototypes from external modules
 *-----------------------------------------------------------*/
extern void _task_switched_out(void *, void *, void *);
extern void _task_switched_in(void *, void *, void *);

/* Application specific definitions */
#define configUSE_PREEMPTION                      1
#define configUSE_PORT_OPTIMISED_TASK_SELECTION   0
#define configUSE_TICKLESS_IDLE                   0
#define configCPU_CLOCK_HZ                        _CPU_START_FREQUENCY_
#define configTICK_RATE_HZ                        __OS_TASK_SCHED_FREQ__
#define configMAX_PRIORITIES                      9
#define configMINIMAL_STACK_SIZE                  __OS_TASK_MIN_STACK_DEPTH__
#define configTOTAL_HEAP_SIZE                     0
#define configMAX_TASK_NAME_LEN                   1
#define configUSE_16_BIT_TICKS                    0
#define configIDLE_SHOULD_YIELD                   0
#define configUSE_MUTEXES                         1
#define configUSE_RECURSIVE_MUTEXES               1
#define configUSE_COUNTING_SEMAPHORES             1
#define configUSE_ALTERNATIVE_API                 0
#define configQUEUE_REGISTRY_SIZE                 0
#define configUSE_QUEUE_SETS                      0
#define configUSE_TIME_SLICING                    1
#define configUSE_NEWLIB_REENTRANT                0
#define configENABLE_BACKWARD_COMPATIBILITY       0
#define configUSE_APPLICATION_TASK_TAG            0
#define configSUPPORT_STATIC_ALLOCATION           1
#define configSUPPORT_DYNAMIC_ALLOCATION          1
#define configUSE_LIST_DATA_INTEGRITY_CHECK_BYTES 0
#define configNUM_THREAD_LOCAL_STORAGE_POINTERS   2
#define configENABLE_MPU                          0
#define configNUMBER_OF_CORES                     _CPUCTL_CORE_NUM
#define configUSE_MPU_WRAPPERS_V1                 0
#define configENABLE_ACCESS_CONTROL_LIST          0
#define configUSE_PICOLIBC_TLS                    0
#define configUSE_C_RUNTIME_TLS_SUPPORT           0
#define configCHECK_HANDLER_INSTALLATION          0
#define configUSE_PASSIVE_IDLE_HOOK               1

#if configNUMBER_OF_CORES > 1
#define configUSE_CORE_AFFINITY                   1
#endif

#if defined(ARCH_posix)
#define configSTACK_DEPTH_TYPE                    uint32_t
#endif

/* Hook function related definitions. */
#define configUSE_IDLE_HOOK            1
#define configUSE_TICK_HOOK            1
#define configCHECK_FOR_STACK_OVERFLOW 2
#define configUSE_MALLOC_FAILED_HOOK   0

/* Run time and task stats gathering related definitions. */
#define configGENERATE_RUN_TIME_STATS        0
#define configUSE_TRACE_FACILITY             0
#define configUSE_STATS_FORMATTING_FUNCTIONS 0

/* Co-routine related definitions. */
#define configUSE_CO_ROUTINES           0
#define configMAX_CO_ROUTINE_PRIORITIES 1

/* Software timer related definitions. */
#define configUSE_TIMERS             0
#define configTIMER_TASK_PRIORITY    3
#define configTIMER_QUEUE_LENGTH     10
#define configTIMER_TASK_STACK_DEPTH configMINIMAL_STACK_SIZE

/* Interrupt nesting behaviour configuration. */
#define configKERNEL_INTERRUPT_PRIORITY       _CPU_IRQ_RTOS_KERNEL_PRIORITY_
#define configMAX_SYSCALL_INTERRUPT_PRIORITY  _CPU_IRQ_RTOS_APICALL_PRIORITY_
#define configMAX_API_CALL_INTERRUPT_PRIORITY _CPU_IRQ_RTOS_APICALL_PRIORITY_

/* FreeRTOS MPU specific definitions. */
#define configINCLUDE_APPLICATION_DEFINED_PRIVILEGED_FUNCTIONS 0

/* Optional functions - most linkers will remove unused functions anyway. */
#define INCLUDE_vTaskPrioritySet               1
#define INCLUDE_uxTaskPriorityGet              1
#define INCLUDE_vTaskDelete                    1
#define INCLUDE_vTaskSuspend                   1
#define INCLUDE_xTaskResumeFromISR             1
#define INCLUDE_vTaskDelayUntil                1
#define INCLUDE_vTaskDelay                     1
#define INCLUDE_xTaskGetSchedulerState         1
#define INCLUDE_xTaskGetCurrentTaskHandle      1
#define INCLUDE_xSemaphoreGetMutexHolder       1
#define INCLUDE_uxTaskGetStackHighWaterMark    1
#define INCLUDE_xTaskGetIdleTaskHandle         1
#define INCLUDE_xTimerGetTimerDaemonTaskHandle 0
#define INCLUDE_pcTaskGetTaskName              0
#define INCLUDE_eTaskGetState                  0
#define INCLUDE_xEventGroupSetBitFromISR       0
#define INCLUDE_xTimerPendFunctionCall         0

#define traceTASK_SWITCHED_OUT()                                                        \
        _task_switched_out(pxCurrentTCB, pxCurrentTCB->pvThreadLocalStoragePointers[0], \
                pxCurrentTCB->pvThreadLocalStoragePointers[1])

#define traceTASK_SWITCHED_IN()                                                        \
        _task_switched_in(pxCurrentTCB, pxCurrentTCB->pvThreadLocalStoragePointers[0], \
                pxCurrentTCB->pvThreadLocalStoragePointers[1])

#if __OS_ENABLE_SYS_ASSERT__ > 0
extern void _assert_hook(bool assert, const char *msg);
#define configASSERT(x) _assert_hook(x, __func__)
#endif

#endif /* FREERTOS_CONFIG_H */
