/*==============================================================================
@file    ksymtab.h

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

/*
 * The kernel symbol table is an array of function addresses located in flash
 * memory. Drivers and file systems built separately from the kernel (drvset)
 * call kernel functions through this table: each exported function has a
 * fixed index. The table pointer is stored in the vector table entry
 * _KVECTOR_KSYMTAB (kernel/kvectors.h, offset 0x3F8 from the vector table
 * start), so drvset finds the table without any RAM.
 *
 * RULES:
 * - new symbols are appended only at the end of _KSYMTAB_LIST,
 * - an exported symbol is never removed or moved (removed function is replaced
 *   by a stub that keeps the index),
 * - _KSYMTAB_VERSION is incremented when the interface of an existing symbol
 *   changes (incompatible change, old drvset is rejected).
 */

#ifndef _KSYMTAB_H_
#define _KSYMTAB_H_

/*==============================================================================
  Include files
==============================================================================*/
#include "kernel/kvectors.h"

/*==============================================================================
  Exported macros
==============================================================================*/
/** table magic number ("KSYM") */
#define _KSYMTAB_MAGIC 0x4D59534BUL

/** interface version, incremented on incompatible change of exported symbol */
#define _KSYMTAB_VERSION 1

/** offset of symbol address array in the table (in bytes) */
#define _KSYMTAB_SYM_OFFSET 8

/** exported symbols (append only!) */
#define _KSYMTAB_LIST(X) \
        /* C library (memory and strings) */ \
        X(memcpy) \
        X(memmove) \
        X(memset) \
        X(memcmp) \
        X(memchr) \
        X(strlen) \
        X(strnlen) \
        X(strcmp) \
        X(strncmp) \
        X(strcpy) \
        X(strncpy) \
        X(strchr) \
        X(strrchr) \
        X(strstr) \
        X(qsort) \
        /* memory and files (sysapi.h) */ \
        X(sys_fopen) \
        X(sys_fclose) \
        X(sys_fwrite) \
        X(sys_fread) \
        X(sys_fseek) \
        X(sys_ftell) \
        X(sys_ioctl) \
        X(sys_fstat) \
        X(sys_fflush) \
        X(sys_clearerr) \
        X(sys_ferror) \
        X(sys_get_free_mem) \
        X(sys_get_mem_size) \
        /* text (sysapi.h) */ \
        X(printk) \
        X(sys_strfmt) \
        X(sys_sscanf) \
        X(sys_strlcpy) \
        X(sys_strlcat) \
        X(sys_strtoi32) \
        X(sys_strtou32) \
        X(sys_opt_get_int) \
        X(sys_opt_get_str_ref) \
        X(sys_opt_get_str_copy) \
        X(sys_opt_get_bool) \
        X(sys_opt_has_flag) \
        X(sys_opt_get_enum) \
        /* time (sysapi.h) */ \
        X(sys_get_uptime_ms) \
        X(sys_sleep_us) \
        X(sys_sleep_ms) \
        X(sys_mktime) \
        X(sys_gmtime_r) \
        X(sys_gettime) \
        /* synchronization (sysapi.h) */ \
        X(sys_semaphore_create) \
        X(sys_semaphore_destroy) \
        X(sys_semaphore_wait) \
        X(sys_semaphore_signal) \
        X(sys_semaphore_wait_from_ISR) \
        X(sys_semaphore_signal_from_ISR) \
        X(sys_flag_create) \
        X(sys_flag_destroy) \
        X(sys_flag_wait) \
        X(sys_flag_set) \
        X(sys_flag_clear) \
        X(sys_mutex_create) \
        X(sys_mutex_destroy) \
        X(sys_mutex_lock) \
        X(sys_mutex_trylock) \
        X(sys_mutex_unlock) \
        X(sys_queue_create) \
        X(sys_queue_destroy) \
        X(sys_queue_reset) \
        X(sys_queue_send) \
        X(sys_queue_send_from_ISR) \
        X(sys_queue_receive) \
        X(sys_queue_receive_from_ISR) \
        X(sys_queue_get_number_of_items) \
        X(sys_queue_get_number_of_items_from_ISR) \
        /* threads and locking (sysapi.h) */ \
        X(sys_thread_create) \
        X(sys_thread_destroy) \
        X(sys_thread_yield) \
        X(sys_thread_yield_from_ISR) \
        X(sys_critical_section_begin) \
        X(sys_critical_section_end) \
        X(sys_scheduler_lock) \
        X(sys_scheduler_unlock) \
        X(sys_spinlock_lock) \
        X(sys_spinlock_unlock) \
        X(sys_spinlock_lock_irq) \
        X(sys_spinlock_unlock_irq) \
        /* caches and other (sysapi.h) */ \
        X(sys_dcache_clean) \
        X(sys_dcache_invalidate) \
        X(sys_dcache_flush) \
        X(sys_icache_invalidate) \
        X(sys_cache_sync_code) \
        X(sys_rand_r) \
        X(sys_assert_hook) \
        /* drivers (driver.h) */ \
        X(drv_mem_alloc) \
        X(drv_mem_free) \
        X(drv_module_get_instance) \
        X(drv_lock) \
        X(drv_unlock) \
        X(drv_check_access) \
        X(drv_is_locked) \
        X(drv_mem_is_dma_capable) \
        X(drv_mem_is_cacheable) \
        X(drv_mem_register_region) \
        X(drv_clock_changed) \
        /* file systems (fs.h) */ \
        X(fs_mem_alloc) \
        X(fs_mem_free) \
        X(fs_llist_create) \
        X(fs_mknod) \
        X(fs_mkdir) \
        X(fs_mkfifo) \
        X(fs_opendir) \
        X(fs_closedir) \
        X(fs_readdir) \
        X(fs_remove) \
        X(fs_rename) \
        X(fs_chmod) \
        X(fs_chown) \
        X(fs_stat) \
        X(fs_statfs) \
        X(fs_sync) \
        X(fs_process_get_stat_pid) \
        X(fs_process_get_stat_seek) \
        X(fs_process_get_count) \
        X(fs_dev_open) \
        X(fs_dev_close) \
        X(fs_dev_write) \
        X(fs_dev_read) \
        X(fs_dev_ioctl) \
        X(fs_dev_flush) \
        X(fs_dev_stat) \
        X(fs_dev_type) \
        X(fs_pipe_create) \
        X(fs_pipe_destroy) \
        X(fs_pipe_get_length) \
        X(fs_pipe_read) \
        X(fs_pipe_write) \
        X(fs_pipe_close) \
        X(fs_pipe_permanent) \
        X(fs_pipe_clear) \
        /* linked list library (fs.h) */ \
        X(sys_llist_destroy) \
        X(sys_llist_empty) \
        X(sys_llist_size) \
        X(sys_llist_push_emplace_front) \
        X(sys_llist_push_front) \
        X(sys_llist_pop_front) \
        X(sys_llist_push_emplace_back) \
        X(sys_llist_push_back) \
        X(sys_llist_pop_back) \
        X(sys_llist_emplace) \
        X(sys_llist_insert) \
        X(sys_llist_erase) \
        X(sys_llist_take) \
        X(sys_llist_take_front) \
        X(sys_llist_take_back) \
        X(sys_llist_clear) \
        X(sys_llist_swap) \
        X(sys_llist_sort) \
        X(sys_llist_unique) \
        X(sys_llist_reverse) \
        X(sys_llist_at) \
        X(sys_llist_contains) \
        X(sys_llist_find_begin) \
        X(sys_llist_find_end) \
        X(sys_llist_front) \
        X(sys_llist_back) \
        X(sys_llist_iterator) \
        X(sys_llist_begin) \
        X(sys_llist_end) \
        X(sys_llist_range) \
        X(sys_llist_iterator_next) \
        X(sys_llist_iterator_prev) \
        X(sys_llist_erase_by_iterator) \
        X(sys_llist_functor_cmp_pointers) \
        X(sys_llist_functor_cmp_strings) \
        /* driver interfaces (DDI) of drivers built into the kernel: optional, */ \
        /* functions of drivers that are not built return error (drivers/drvset.c) */ \
        X(_GPIO_DDI_set_pin) \
        X(_GPIO_DDI_clear_pin) \
        X(_GPIO_DDI_get_pin) \
        X(_GPIO_DDI_set_pin_conf) \
        X(_GPIO_DDI_get_pin_conf) \
        X(_DMA_DDI_reserve) \
        X(_DMA_DDI_release) \
        X(_DMA_DDI_transfer) \
        X(_DMA_DDI_abort) \
        X(_DMA_DDI_get_stream) \
        X(_DMA_DDI_get_channel) \
        X(_DMA_DDI_memcpy) \
        X(_MDMA_DDI_reserve) \
        X(_MDMA_DDI_release) \
        X(_MDMA_DDI_transfer) \
        X(_MDMA_DDI_get_channel) \
        X(_MDMA_DDI_memcpy) \
        X(_IRQ_DDI_attach) \
        X(_IRQ_DDI_detach)

/*
 * Optional symbols (driver interfaces) with fallback used when the driver is
 * not built into the kernel: X(name, fallback), fallback: ZERO (returns 0 or
 * nothing), ENODEV (returns ENODEV), MINUS1 (returns -1).
 */
#define _KSYMTAB_OPTIONAL_LIST(X) \
        X(_GPIO_DDI_set_pin, ZERO) \
        X(_GPIO_DDI_clear_pin, ZERO) \
        X(_GPIO_DDI_get_pin, MINUS1) \
        X(_GPIO_DDI_set_pin_conf, ENODEV) \
        X(_GPIO_DDI_get_pin_conf, ENODEV) \
        X(_DMA_DDI_reserve, ZERO) \
        X(_DMA_DDI_release, ZERO) \
        X(_DMA_DDI_transfer, ENODEV) \
        X(_DMA_DDI_abort, ZERO) \
        X(_DMA_DDI_get_stream, ENODEV) \
        X(_DMA_DDI_get_channel, ENODEV) \
        X(_DMA_DDI_memcpy, ENODEV) \
        X(_MDMA_DDI_reserve, ZERO) \
        X(_MDMA_DDI_release, ZERO) \
        X(_MDMA_DDI_transfer, ENODEV) \
        X(_MDMA_DDI_get_channel, ENODEV) \
        X(_MDMA_DDI_memcpy, ENODEV) \
        X(_IRQ_DDI_attach, ENODEV) \
        X(_IRQ_DDI_detach, ENODEV)

#ifndef __ASSEMBLER__

#ifdef __cplusplus
extern "C" {
#endif

/*==============================================================================
  Exported object types
==============================================================================*/
/** symbol indexes */
enum _ksym_idx {
#define _KSYM_IDX(name) _KSYM_IDX__##name,
        _KSYMTAB_LIST(_KSYM_IDX)
#undef _KSYM_IDX
        _KSYM_COUNT
};

/** kernel symbol table */
typedef struct {
        unsigned int magic;       /**< _KSYMTAB_MAGIC */
        unsigned short version;   /**< _KSYMTAB_VERSION */
        unsigned short count;     /**< number of symbols (_KSYM_COUNT) */
        const void *sym[_KSYM_COUNT]; /**< symbol addresses */
} _ksymtab_t;

/*==============================================================================
  Exported objects
==============================================================================*/
#if (__OS_ENABLE_DRVSET__ > 0) && !defined(__DNX_MODULE__)
extern const _ksymtab_t _ksymtab;
#endif

#ifdef __cplusplus
}
#endif

#endif /* __ASSEMBLER__ */

#endif /* _KSYMTAB_H_ */
/*==============================================================================
  End of file
==============================================================================*/
