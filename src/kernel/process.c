/*==============================================================================
@file    process.c

@author  Daniel Zorychta

@brief   This file support processes

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

#pragma GCC optimize("O3")

/*==============================================================================
  Include files
==============================================================================*/
#include "kernel/process.h"
#include "fs/vfs.h"
#include "kernel/khooks.h"
#include "kernel/kpanic.h"
#include "kernel/kwrapper.h"
#include "kernel/kworker.h"
#include "kernel/printk.h"
#include "kernel/sysfunc.h"
#include "lib/cast.h"
#include "lib/llist.h"
#include "lib/misc.h"
#include "mm/shm.h"
#include "net/netm.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/*==============================================================================
  Local symbolic constants/macros
==============================================================================*/
// critical section - mutex wait must set to max possible
#define ATOMIC(_mtx)                                                                                              \
        for (int __ = 0; __ == 0;)                                                                                \
                for (int _e = _mutex_lock(_mtx, _WAIT_FOREVER_MS); _e == 0 && __ == 0; _mutex_unlock(_mtx), __++) \
                        for (bool ___ = true; ___; ___ = false)

#define foreach_resource(_v, _l) for (res_header_t *_v = _l; _v; _v = _v->next)

#define foreach_process(_v, _l) for (_process_t *_v = _l; _v; _v = cast(_process_t *, _v->header.next))

#define NO_SYSCALL UINT8_MAX

#define SHEBANGLEN 64

#define is_proc_valid(proc) \
        (_mm_is_object_in_heap(proc) && (proc->header.self == proc) && (proc->header.type == RES_TYPE_PROCESS))

#define is_tid_in_range(proc, tid) (tid < __OS_PROCESS_MAX_THREADS__)

#define PROC_FLAG_DETACHED (1 << 0)
#define PROC_FLAG_KWORKER  (1 << 1)
#define PROC_FLAG_KILL     (1 << 2)

#define PID_MIN 1
#define PID_MAX 999

#define SHARED_DESCRIPTOR (1 << 15)

/*==============================================================================
  Local types, enums definitions
==============================================================================*/
typedef struct _prog_data pdata_t;

typedef struct {
        void *func;
        void (*at_init)(void *);
        void *at_init_ctx;
        struct _process *proc;
        task_data_t *task_data;
        void *arg;
} task_args_t;

enum {
        STORAGE_IDX_PROC,
        STORAGE_IDX_TASK_DATA,
};

/*==============================================================================
  Local function prototypes
==============================================================================*/
static void process_code(void *mainfn);
static void thread_code(void *args);
static void process_destroy_all_resources(_process_t *proc);
static int resource_destroy(res_header_t *resource);
static int argtab_create(const char *str, u8_t *argc, char **argv[]);
static void argtab_destroy(char **argv);
static int find_program(const char *name, const _program_entry_t **prog);
static int allocate_process_globals(_process_t *proc, size_t app_ctx_size, const _program_entry_t *usrprog);
static int process_apply_attributes(_process_t *proc, const _process_attr_t *attr);
static void process_get_stat(_process_t *proc, _process_stat_t *stat);
static bool process_move_list(_process_t *proc, _process_t **list_from, _process_t **list_to);
static void process_remove_from_list(_process_t *proc, _process_t **list);
static void process_release(_process_t *proc);
static int get_pid(pid_t *pid);
static void destroy_all_tasks(_process_t *proc, bool with_zero);

#if __OS_SYSTEM_SHEBANG_ENABLE__ > 0
static bool is_cmd_path(const char *cmd);
static int analyze_shebang(_process_t *proc, const char *cmd, char **cmdarg);
#endif

/*==============================================================================
  Local object definitions
==============================================================================*/
static pid_t PID_cnt;
static _process_t *active_process_list;
static _process_t *terminate_process_list;
static _process_t *destroy_process_list;
static _process_t *release_process_list;
static _process_t *zombie_process_list;
static _process_t *active_process[_CPUCTL_CORE_NUM];
static kmtx_t *process_mtx;
static kmtx_t *kworker_mtx;
static dchain_t shared_desctab;
static appset_table_t appset_base;

/*==============================================================================
  Exported object definitions
==============================================================================*/
/* error number */
int _errno[_CPUCTL_CORE_NUM] = {ESUCC};

/* global variables */
void *_global[_CPUCTL_CORE_NUM] = {NULL};

/* Application context */
void *_app_ctx[_CPUCTL_CORE_NUM] = {NULL};

/* CPU load calulcation */
CPU_load_t CPU_load[_CPUCTL_CORE_NUM];

/*==============================================================================
  External object definitions
==============================================================================*/
extern u32_t _uptime_counter_sec;

/*==============================================================================
  Function definitions
==============================================================================*/

//==============================================================================
/**
 * @brief  Initialize process module.
 */
//==============================================================================
void _process_init(void)
{
        /*
         * Register main program table
         */
        if (__PROJ_APPSET_TABLE_ADDRESS__ == UINT32_MAX) {
                extern void *__text_end;
                appset_base.table = cast(const _program_table_desc_t *const, &__text_end);
        } else {
                appset_base.table = cast(const _program_table_desc_t *const, __PROJ_APPSET_TABLE_ADDRESS__);
        }

        appset_table_t *at = &appset_base;

        if (at->table->data_init_start and at->table->data_start and at->table->data_size) {
                memcpy(at->table->data_start, at->table->data_init_start, at->table->data_size);
        }

        if (at->table->bss_start and at->table->bss_size) {
                memset(at->table->bss_start, 0, at->table->bss_size);
        }
}

//==============================================================================
/**
 * @brief  Create a new process
 *
 * @param[in]  cmd      command (name + arguments)
 * @param[in]  attr     process attributes (use NULL for default attributes)
 * @param[out] pid      PID of created process (can be NULL)
 *
 * @return One of errno value.
 */
//==============================================================================
int _process_create(const char *cmd, const _process_attr_t *attr, pid_t *pid)
{
        if (!process_mtx) {
                _assert(_mutex_create(KMTX_TYPE_RECURSIVE, &process_mtx) == ESUCC);
        }

        if (!kworker_mtx) {
                _assert(_mutex_create(KMTX_TYPE_RECURSIVE, &kworker_mtx) == ESUCC);
        }

        if (!cmd) {
                return ENOENT;
        }

        if (!attr) {
                return EINVAL;
        }

        char *cmdarg = NULL;
        _process_t *proc = NULL;
        task_args_t *task_args = NULL;

        int err = _kzalloc(_MM_KRN, sizeof(_process_t), _cpuctl_fast_mem, 0, 0, cast(void **, &proc));
        if (err) {
                return err;
        }

        proc->header.self = proc;
        proc->header.type = RES_TYPE_PROCESS;

        err = process_apply_attributes(proc, attr);
        if (err) {
                goto finish;
        }

#if __OS_SYSTEM_SHEBANG_ENABLE__ > 0
        u8_t level = 8;
        const char *c = cmd;
        while (c && level-- && is_cmd_path(c)) {

                char *next = NULL;
                err = analyze_shebang(proc, c, &next);
                if (err) {
                        goto finish;
                }

                if (next == NULL) {
                        // no shebang: current command is the final one
                        break;
                }

                if (cmdarg) {
                        _kfree(_MM_KRN, cast(void **, &cmdarg));
                }

                cmdarg = next;
                c = cmdarg;
        }
#endif

        err = argtab_create(cmdarg ? cmdarg : cmd, &proc->argc, &proc->argv);
        if (err) {
                goto finish;
        }

        if (cmdarg) {
                _kfree(_MM_KRN, cast(void **, &cmdarg));
        }

        err = find_program(proc->argv[0], &proc->pdata);
        if (err) {
                goto finish;
        }

        err = allocate_process_globals(proc, attr->app_ctx_size, proc->pdata);
        if (err) {
                goto finish;
        }

        err = get_pid(&proc->pid);
        if (err) {
                goto finish;
        }

        if (proc->pdata->main != _kworker_process) {
                err = _flag_create(&proc->event);
                if (err) {
                        goto finish;
                }
        } else {
                proc->flag |= PROC_FLAG_KWORKER;
        }

        err = _kzalloc(_MM_KRN, sizeof(*task_args), _cpuctl_fast_mem, 0, 0, cast(void **, &task_args));
        if (err) {
                goto finish;
        }

        ATOMIC (process_mtx) {
                size_t stack_size = *proc->pdata->stack_depth + (__OS_IRQ_STACK_DEPTH__) + (__OS_IO_STACK_DEPTH__);
                stack_size = max(stack_size, _STACK_DEPTH_MINIMAL);

                proc->taskdata[0].stack_size = stack_size;
                proc->taskdata[0].kernelspace = (proc->flag & PROC_FLAG_KWORKER);
                proc->taskdata[0].id = 0;
                proc->taskdata[0].syscalls_per_sec = 0;
                proc->taskdata[0].curr_syscall = NO_SYSCALL;

                task_args->func = proc->pdata->main;
                task_args->proc = proc;
                task_args->task_data = &proc->taskdata[0];
                task_args->at_init = attr->at_init;
                task_args->at_init_ctx = attr->at_init_ctx;

                err = _task_create(process_code, proc->pdata->name, proc->taskdata[0].stack_size, task_args,
                        &proc->taskdata[0].task);
                if (!err) {
                        if (attr) {
                                i16_t priority = min(attr->priority, _PRIORITY_HIGHEST);
                                _task_set_priority(proc->taskdata[0].task, priority);
                        }

                        if (pid) {
                                *pid = proc->pid;
                        }

                        if (active_process_list == NULL) {
                                active_process_list = proc;

                        } else {
                                proc->header.next = cast(res_header_t *, active_process_list);
                                active_process_list = proc;
                        }
                }
        }

finish:
        if (err) {
                if (task_args) {
                        _kfree(_MM_KRN, cast(void **, &task_args));
                }

                if (cmdarg) {
                        _kfree(_MM_KRN, cast(void **, &cmdarg));
                }

                if (proc) {
                        process_destroy_all_resources(proc);
                        proc->header.self = NULL;
                        proc->header.type = RES_TYPE_UNKNOWN;
                        _kfree(_MM_KRN, cast(void **, &proc));
                }
        }

        return err;
}

//==============================================================================
/**
 * Function clean up killed processes. If process has parent then is moved to
 * the zombie list. Only parent can remove zombie process.
 */
//==============================================================================
void _process_clean_up_killed_processes(void)
{
        if (terminate_process_list) {

                int err = _mutex_lock(process_mtx, 5000);
                if (not err) {
                        task_t *self = _task_get_handle();

                        foreach_process (proc, terminate_process_list) {

                                bool running = false;

                                u8_t threads = __OS_PROCESS_MAX_THREADS__;

                                _kernel_scheduler_lock();

                                for (u8_t tid = 0; (tid < threads) and not running; tid++) {
                                        if (not proc->taskdata[tid].task) {
                                                continue;
                                        }

                                        /*
                                         * NOTE: the calling thread belongs to the killed
                                         *       process (clean up is called on syscall
                                         *       entry). It cannot be suspended or destroyed
                                         *       here, it parks itself at kernel space
                                         *       entry. Process is destroyed later.
                                         */
                                        if (proc->taskdata[tid].task == self) {
                                                running = true;
                                                break;
                                        }

                                        /*
                                         * NOTE: not running thread is already parked
                                         *       (suspended at kernel space boundary).
                                         */
                                        if (not proc->taskdata[tid].running) {
                                                continue;
                                        }

                                        if (proc->taskdata[tid].curr_syscall != NO_SYSCALL) {
                                                running = true;

                                        } else {
                                                _task_suspend(proc->taskdata[tid].task);
                                                proc->taskdata[tid].running = false;
                                        }
                                }

                                _kernel_scheduler_unlock();

                                if (not running) {
                                        _printk("PID %u: terminated.", proc->pid);

                                        process_move_list(proc, &terminate_process_list, &destroy_process_list);

                                        // resources are released by kworker
                                        _kworker_wake();
                                }
                        }

                        _mutex_unlock(process_mtx);

                } else {
                        /*
                         * NOTE: process mutex is busy (e.g. other clean up is in
                         *       progress). Clean up is retried at next syscall.
                         */
                }
        }

        /*
         * NOTE: resources (files, dirs, sockets, ...) of killed processes are
         *       released only by kworker and without process mutex. Closing
         *       of a file can block for a long time (e.g. file system mutex or
         *       slow device). If it was done in a syscall of other process,
         *       then that process would be blocked (e.g. top or shell), and if
         *       it was done with process mutex locked then the whole system
         *       would be blocked.
         */
        if (destroy_process_list) {
                _process_t *self_proc = NULL;
                _task_get_process_container_unprotected(_THIS_TASK, &self_proc);

                if (self_proc and (self_proc->flag & PROC_FLAG_KWORKER)) {
                        while (destroy_process_list) {
                                _process_t *proc = NULL;

                                if (_mutex_lock(process_mtx, 5000) != ESUCC) {
                                        // process mutex is busy, clean up is retried later
                                        break;
                                }

                                proc = destroy_process_list;
                                if (proc) {
                                        process_move_list(proc, &destroy_process_list, &release_process_list);
                                }

                                _mutex_unlock(process_mtx);

                                if (proc) {
                                        process_release(proc);
                                }
                        }
                }
        }
}

//==============================================================================
/**
 * Kill selected process. Kill moves process to the process destroy list. There
 * are releases resources used by process.
 *
 * @param  pid          process ID
 *
 * @return One of errno value.
 */
//==============================================================================
int _process_kill(pid_t pid)
{
        int err = ESRCH;

        ATOMIC (process_mtx) {

                foreach_process (proc, active_process_list) {

                        if (proc->pid == pid) {
                                proc->flag |= PROC_FLAG_KILL;

                                process_move_list(proc, &active_process_list, &terminate_process_list);
                                err = ESUCC;
                                break;
                        }
                }
        }

        if (not err) {
                // kworker finishes and releases killed process
                _kworker_wake();
        }

        return err;
}

//==============================================================================
/**
 * Kill selected process. Kill moves process to the process destroy list. There
 * are releases resources used by process.
 *
 * @param  pid          process ID
 *
 * @return One of errno value.
 */
//==============================================================================
void _process_suicide(void)
{
        _process_t *proc;
        _task_get_process_container(_THIS_TASK, &proc, NULL);

        ATOMIC (process_mtx) {

                proc->flag |= PROC_FLAG_KILL;

                process_move_list(proc, &active_process_list, &terminate_process_list);
        }

        // kworker finishes and releases killed process
        _kworker_wake();
}

//==============================================================================
/**
 * @brief  Function return information of kill process request.
 *
 * @return True: kill requested, false: kill not requsted.
 */
//==============================================================================
bool _process_is_kill_requested(void)
{
        _process_t *proc;
        _task_get_process_container_unprotected(_THIS_TASK, &proc);
        return (not proc) or (proc->flag & PROC_FLAG_KILL);
}

//==============================================================================
/**
 * Function remove zombie process.
 *
 * @param  proc         zombie process object
 * @param  status       program status
 */
//==============================================================================
void _process_remove_zombie(_process_t *proc, int *status)
{
        _assert(is_proc_valid(proc));

        ATOMIC (process_mtx) {
                _process_t *prev = NULL;
                foreach_process (p, zombie_process_list) {
                        if (p == proc) {

                                if (prev) {
                                        prev->header.next = proc->header.next;
                                } else {
                                        zombie_process_list = cast(_process_t *, proc->header.next);
                                }

                                if (status) {
                                        *status = proc->taskdata[0].status;
                                }

                                _flag_destroy(proc->event);
                                _kfree(_MM_KRN, cast(void *, &proc));

                                break;
                        } else {
                                prev = p;
                        }
                }
        }
}

//==============================================================================
/**
 * Function exit from process execution. Is called when main() function execute
 * return function or if user call exit() function.
 *
 * @param  proc         process container
 * @param  status       status (exit code)
 */
//==============================================================================
void _process_exit(_process_t *proc, int status)
{
        if (is_proc_valid(proc)) {
                proc->flag |= PROC_FLAG_KILL;

                _task_set_priority(_THIS_TASK, _PRIORITY_NORMAL);

                bool running = false;
                u8_t threads = __OS_PROCESS_MAX_THREADS__;

                /*
                 * NOTE: own thread is found by task handle, because curr_task
                 *       is the last switched in thread of the process and it
                 *       can be other thread (e.g. running on other CPU core).
                 */
                bool release = false;
                tid_t self_tid = proc->curr_task;
                task_t *self = _task_get_handle();
                for (tid_t tid = 0; tid < threads; tid++) {
                        if (proc->taskdata[tid].task == self) {
                                self_tid = tid;
                                break;
                        }
                }

                do {
                        running = false;

                        _kernel_scheduler_lock();

                        for (u8_t tid = 0; (tid < threads) and not running; tid++) {
                                if (self_tid == tid) {
                                        continue;
                                }

                                if (not proc->taskdata[tid].task) {
                                        continue;
                                }

                                /*
                                 * NOTE: not running thread is already parked
                                 *       (suspended at kernel space boundary).
                                 */
                                if (not proc->taskdata[tid].running) {
                                        continue;
                                }

                                if (proc->taskdata[tid].curr_syscall != NO_SYSCALL) {
                                        running = true;

                                } else {
                                        _task_suspend(proc->taskdata[tid].task);
                                        proc->taskdata[tid].running = false;
                                }
                        }

                        _kernel_scheduler_unlock();

                        if (running) {
                                _sleep_ms(100);
                        }

                } while (running);

                proc->taskdata[0].status = status;

                kfile_t *stdin = cast(kfile_t *, proc->desctab.resource[0]);
                kfile_t *stdout = cast(kfile_t *, proc->desctab.resource[1]);
                kfile_t *stderr = cast(kfile_t *, proc->desctab.resource[2]);

                va_list none;
                if (stdin and (stdin->header.type == RES_TYPE_FILE)) {
                        _vfs_vfioctl(stdin, IOCTL_VFS__DEFAULT_RD_MODE, none);
                        _vfs_vfioctl(stdin, IOCTL_VFS__DEFAULT_WR_MODE, none);
                }

                if (stdout and (stdout->header.type == RES_TYPE_FILE)) {
                        _vfs_vfioctl(stdout, IOCTL_VFS__DEFAULT_RD_MODE, none);
                        _vfs_vfioctl(stdout, IOCTL_VFS__DEFAULT_WR_MODE, none);
                }

                if (stderr and (stderr->header.type == RES_TYPE_FILE)) {
                        _vfs_vfioctl(stderr, IOCTL_VFS__DEFAULT_RD_MODE, none);
                        _vfs_vfioctl(stderr, IOCTL_VFS__DEFAULT_WR_MODE, none);
                }

                ATOMIC (process_mtx) {
                        for (tid_t tid = 0; tid < threads; tid++) {
                                if (self_tid == tid) {
                                        continue;
                                }

                                if (proc->taskdata[tid].task) {
                                        _task_destroy(proc->taskdata[tid].task);
                                        proc->taskdata[tid].task = NULL;
                                }
                        }

                        /*
                         * NOTE: if process was killed in the meantime then it
                         *       is not in the active list and is released by
                         *       clean up function (kworker).
                         */
                        release = process_move_list(proc, &active_process_list, &release_process_list);

                        if (release) {
                                /*
                                 * NOTE: all other threads are destroyed, so the
                                 *       kill flag is cleared. Otherwise the lock of
                                 *       busy objects would be canceled when process
                                 *       resources are released (e.g. file close).
                                 */
                                proc->flag &= ~PROC_FLAG_KILL;
                        }

                        proc->taskdata[self_tid].task = NULL;
                }

                /*
                 * NOTE: process resources are released in context of exiting
                 *       thread without process mutex, because closing of
                 *       process files can block for a long time. Exit flag is
                 *       set when process is moved to the zombie list. Detached
                 *       process object is released and must not be accessed
                 *       after this call.
                 */
                if (release) {
                        process_release(proc);
                } else {
                        // process was killed in the meantime, kworker finishes it
                        _kworker_wake();
                }

                _task_exit();
        } else {
                _assert(is_proc_valid(proc));
        }
}

//==============================================================================
/**
 * @brief  The function works almost the same as _process_exit(), but set exit
 *         code to -1 and print on process's terminal suitable message.
 *
 * @param  proc         process container
 */
//==============================================================================
void _process_abort(_process_t *proc)
{
        if (is_proc_valid(proc)) {
                kfile_t *stderr = cast(kfile_t *, proc->desctab.resource[2]);

                static const char *aborted = "*** Aborted ***\n";
                size_t wrcnt;
                _vfs_fwrite(aborted, strlen(aborted), &wrcnt, stderr);

                pid_t pid = 0;
                _process_get_pid(proc, &pid);

                tid_t tid = _process_get_active_thread(proc, false);

                printk("*** APP ABORT: '%s' [%u:%d:%d,%s] ***", _process_get_name(proc), _cpuctl_get_cpu_id(), pid, tid,
                        "USR");

                _process_exit(proc, -1);
        } else {
                _assert(is_proc_valid(proc));
        }
}

//==============================================================================
/**
 * @brief  Function get CWD path of selected process.
 *
 * @param  proc         process container
 *
 * @return CWD path (always valid).
 */
//==============================================================================
const char *_process_get_CWD(_process_t *proc)
{
        if (is_proc_valid(proc)) {
                return proc->cwd ? proc->cwd : "/";
        } else {
                return "/";
        }
}

//==============================================================================
/**
 * @brief  Function set CWD path of selected process. If NULL passed then
 *         CWD is set to empty string ("").
 *
 * @param  proc         process container
 * @param  CWD          current working directory path (can be NULL)
 *
 * @return One of errno value.
 */
//==============================================================================
int _process_set_CWD(_process_t *proc, const char *CWD)
{
        int err = EINVAL;

        if (is_proc_valid(proc) && CWD && CWD[0] == '/') {
                char *cwd;
                err = _kmalloc(_MM_KRN, strsize(CWD), NULL, 0, 0, cast(void *, &cwd));
                if (!err) {
                        strcpy(cwd, CWD);
                        _vfs_realpath(cwd, SUB_SLASH);

                        if (proc->cwd) {
                                _kfree(_MM_KRN, cast(void *, &proc->cwd));
                        }

                        proc->cwd = cwd;
                }
        }

        return err;
}

//==============================================================================
/**
 * @brief  Function return collected statistics of process.
 *
 * @param  seek     process seek (start from 0)
 * @param  stat     process statistics
 *
 * @return One of errno value (ESUCC, EINVAL, ENOENT).
 */
//==============================================================================
int _process_get_stat_seek(size_t seek, _process_stat_t *stat)
{
        int err = EINVAL;

        if (stat) {
                err = ENOENT;

                ATOMIC (process_mtx) {
                        _process_t *proc = NULL;

                        foreach_process (p, active_process_list) {
                                if (seek == 0) {
                                        proc = p;
                                        goto finish;
                                }

                                seek--;
                        }

                        foreach_process (p, terminate_process_list) {
                                if (seek == 0) {
                                        proc = p;
                                        goto finish;
                                }

                                seek--;
                        }

                        foreach_process (p, destroy_process_list) {
                                if (seek == 0) {
                                        proc = p;
                                        goto finish;
                                }

                                seek--;
                        }

                        foreach_process (p, zombie_process_list) {
                                if (seek == 0) {
                                        proc = p;
                                        goto finish;
                                }

                                seek--;
                        }

                finish:
                        if (proc) {
                                process_get_stat(proc, stat);
                                err = ESUCC;
                        }
                }
        }

        return err;
}

//==============================================================================
/**
 * @brief  Function return collected process statistics.
 *
 * @param  pid      PID
 * @param  stat     process statistics
 *
 * @return One of errno value (ESUCC, EINVAL, ENOENT).
 */
//==============================================================================
int _process_get_stat_pid(pid_t pid, _process_stat_t *stat)
{
        int err = EINVAL;

        if (pid and stat) {
                err = ENOENT;

                ATOMIC (process_mtx) {
                        _process_t *proc = NULL;

                        foreach_process (p, active_process_list) {
                                if (p->pid == pid) {
                                        proc = p;
                                        goto finish;
                                }
                        }

                        foreach_process (p, terminate_process_list) {
                                if (p->pid == pid) {
                                        proc = p;
                                        goto finish;
                                }
                        }

                        foreach_process (p, destroy_process_list) {
                                if (p->pid == pid) {
                                        proc = p;
                                        goto finish;
                                }
                        }

                        foreach_process (p, zombie_process_list) {
                                if (p->pid == pid) {
                                        proc = p;
                                        goto finish;
                                }
                        }

                finish:
                        if (proc) {
                                process_get_stat(proc, stat);
                                err = ESUCC;
                        }
                }
        }

        return err;
}

//==============================================================================
/**
 * @brief  Function return name of selected process.
 *
 * @param  proc         process container
 *
 * @return On success process name is returned, otherwise "<NULL>".
 */
//==============================================================================
const char *_process_get_name(_process_t *proc)
{
        if (is_proc_valid(proc)) {
                return proc->pdata->name;
        } else {
                return "<NULL>";
        }
}

//==============================================================================
/**
 * @brief  Function return number of all processes (even zombies).
 *
 * @return Number of processes.
 */
//==============================================================================
size_t _process_get_count(void)
{
        size_t count = 0;

        ATOMIC (process_mtx) {
                foreach_process (proc, active_process_list) {
                        count++;
                }

                foreach_process (p, terminate_process_list) {
                        count++;
                }

                foreach_process (p, destroy_process_list) {
                        count++;
                }

                foreach_process (p, zombie_process_list) {
                        count++;
                }
        }

        return count;
}

//==============================================================================
/**
 * @brief  Function return active process.
 *
 * @return Object of active process or NULL if unknown process (e.g. idle task).
 */
//==============================================================================
_process_t *_process_get_active(void)
{
        return active_process[_cpuctl_get_cpu_id()];
}

//==============================================================================
/**
 * @brief Function return PID of selected process.
 *
 * @param proc         process container
 * @param pid          process PID
 *
 * @return One of errno value.
 */
//==============================================================================
int _process_get_pid(_process_t *proc, pid_t *pid)
{
        int err = EINVAL;

        if (is_proc_valid(proc) && pid) {
                *pid = proc->pid;
                err = ESUCC;
        }

        return err;
}

//==============================================================================
/**
 * @brief  Function return event flags object.
 *
 * @param  proc         process container
 * @param  flag         pointer to flag set
 *
 * @return One of errno value.
 */
//==============================================================================
int _process_get_event_flags(_process_t *proc, kflag_t **flag)
{
        int err = EINVAL;

        if (is_proc_valid(proc) && flag) {
                ATOMIC (process_mtx) {
                        *flag = proc->event;
                        err = ESUCC;
                }
        }

        return err;
}

//==============================================================================
/**
 * @brief  Function return priority of selected process.
 *
 * @param  pid      process ID
 * @param  prio     priority
 *
 * @return One of errno value.
 */
//==============================================================================
int _process_get_priority(pid_t pid, int *prio)
{
        int err = EINVAL;

        if (pid && prio) {
                _kernel_scheduler_lock();

                _process_t *active_proc = active_process[_cpuctl_get_cpu_id()];

                if (active_proc->pid == pid) {
                        *prio = _task_get_priority(active_proc->taskdata[0].task);
                        err = ESUCC;

                } else {
                        foreach_process (proc, active_process_list) {
                                if (proc->pid == pid) {
                                        *prio = _task_get_priority(proc->taskdata[0].task);
                                        err = ESUCC;
                                        break;
                                }
                        }
                }

                _kernel_scheduler_unlock();
        }

        return err;
}

//==============================================================================
/**
 * Function return container of selected process by PID.
 *
 * @param  pid          process ID
 * @param  process      process container
 *
 * @return One of errno value.
 */
//==============================================================================
int _process_get_container(pid_t pid, _process_t **process)
{
        int err = EINVAL;

        if (pid && process) {
                _kernel_scheduler_lock();

                _process_t *active_proc = active_process[_cpuctl_get_cpu_id()];

                if (active_proc->pid == pid) {
                        *process = active_proc;
                        err = ESUCC;
                }

                if (err) {
                        foreach_process (proc, active_process_list) {
                                if (proc->pid == pid) {
                                        *process = proc;
                                        err = ESUCC;
                                        break;
                                }
                        }
                }

                if (err) {
                        foreach_process (proc, terminate_process_list) {
                                if (proc->pid == pid) {
                                        *process = proc;
                                        err = ESUCC;
                                        break;
                                }
                        }
                }

                if (err) {
                        foreach_process (proc, destroy_process_list) {
                                if (proc->pid == pid) {
                                        *process = proc;
                                        err = ESUCC;
                                        break;
                                }
                        }
                }

                if (err) {
                        /*
                         * NOTE: process in release list has released resources
                         *       in progress. It can be used only to wait for exit.
                         */
                        foreach_process (proc, release_process_list) {
                                if (proc->pid == pid) {
                                        *process = proc;
                                        err = ESUCC;
                                        break;
                                }
                        }
                }

                if (err) {
                        foreach_process (proc, zombie_process_list) {
                                if (proc->pid == pid) {
                                        *process = proc;
                                        err = ESUCC;
                                        break;
                                }
                        }
                }

                _kernel_scheduler_unlock();
        }

        return err;
}

//==============================================================================
/**
 * @brief  Function return thread ID of active thread
 *
 * @param  process              process (can be NULL, then active process is used)
 * @param  isr                  called from ISR
 *
 * @return Thread ID.
 */
//==============================================================================
tid_t _process_get_active_thread(_process_t *process, bool isr)
{
        tid_t tid = UINT8_MAX;

        if (not isr) {
                _kernel_scheduler_lock();
        }

        if (process == NULL) {
                process = active_process[_cpuctl_get_cpu_id()];
        }

        if (process) {
                tid = process->curr_task;
        }

        _assert(tid < __OS_PROCESS_MAX_THREADS__);

        if (not isr) {
                _kernel_scheduler_unlock();
        }

        return tid;
}

//==============================================================================
/**
 * @brief  Function return PID of active process.
 *
 * @return Thread ID.
 */
//==============================================================================
pid_t _process_get_active_process_pid(void)
{
        pid_t pid = 0;

        _kernel_scheduler_lock();

        _process_t *active_proc = active_process[_cpuctl_get_cpu_id()];

        if (active_proc) {
                pid = active_proc->pid;
        }

        _kernel_scheduler_unlock();

        return pid;
}

//==============================================================================
/**
 * @brief  Function return max number of threads that can be handled by process.
 *
 * @param  proc         process
 *
 * @return On success number of threads, otherwise 0.
 */
//==============================================================================
u8_t _process_get_max_threads(_process_t *proc)
{
        u8_t threads = 0;

        if (is_proc_valid(proc)) {
                threads = __OS_PROCESS_MAX_THREADS__;
        }

        return threads;
}

//==============================================================================
/**
 * @brief  Function register selected resource in selected process.
 *
 * @param  proc         process container
 * @param  resource     resource to register
 *
 * @return One of errno value.
 */
//==============================================================================
int _process_register_resource(_process_t *proc, res_header_t *resource)
{
        if (is_proc_valid(proc)) {
                kmtx_t *mtx = (proc == _kworker_proc) ? kworker_mtx : process_mtx;

                resource->proc = proc;
                resource->next = NULL;
                resource->prev = NULL;

                ATOMIC (mtx) {
                        if (proc->res_list == NULL) {
                                proc->res_list = resource;
                        } else {
                                resource->next = proc->res_list;
                                proc->res_list->prev = resource;
                                proc->res_list = resource;
                        }

                        proc->res_list_size++;
                }

                return ESUCC;
        } else {
                return ESRCH;
        }
}

//==============================================================================
/**
 * @brief  Function release selected resource of selected type (type is confirmation).
 *
 * @param  proc         process container
 * @param  resource     resource address to release
 * @param  type         resource type
 *
 * @return One of errno value.
 */
//==============================================================================
int _process_release_resource(_process_t *proc, res_header_t *resource, res_type_t type)
{
        if (not is_proc_valid(proc)) {
                return EINVAL;
        }

        if (not _mm_is_object_in_heap(resource) or (resource->self != resource) or (resource->type != type)) {
                return EINVAL;
        }

        if (not is_proc_valid(resource->proc) or (resource->proc != proc)) {
                return ENOENT;
        }

        kmtx_t *mtx = (proc == _kworker_proc) ? kworker_mtx : process_mtx;

        ATOMIC (mtx) {
                if (resource->prev) {
                        resource->prev->next = resource->next;
                }

                if (resource->next) {
                        resource->next->prev = resource->prev;
                }

                if (proc->res_list == resource) {
                        proc->res_list = resource->next;
                }

                resource->next = NULL;
                resource->prev = NULL;

                proc->res_list_size--;
        }

        int err = resource_destroy(resource);
        if (err) {
                _kernel_panic_report(_KERNEL_PANIC_DESC_CAUSE_INTERNAL_1);
        }

        return err;
}

//==============================================================================
/**
 * @brief  Function create a new thread for selected process.
 *
 * @param[in ]  proc         process container
 * @param[in ]  func         thread function
 * @param[in ]  attr         thread attributes
 * @param[in ]  arg          thread argument
 * @param[out]  tid          thread ID
 *
 * @return One of errno value.
 */
//==============================================================================
int _process_thread_create(_process_t *proc, thread_func_t func, const _thread_attr_t *attr, void *arg, tid_t *tid)
{
        int err = EINVAL;

        if (!is_proc_valid(proc) || !func) {
                return err;
        }

        ATOMIC (process_mtx) {
                tid_t id = 1;
                for (; id < __OS_PROCESS_MAX_THREADS__; id++) {
                        if (proc->taskdata[id].task == NULL) {
                                memset(&proc->taskdata[id], 0, sizeof(proc->taskdata[id]));
                                break;
                        }
                }

                if (id < __OS_PROCESS_MAX_THREADS__) {
                        task_args_t *args = NULL;
                        err = _kmalloc(_MM_KRN, sizeof(*args), _cpuctl_fast_mem, 0, 0, cast(void *, &args));
                        if (!err) {

                                args->func = func;
                                args->arg = arg;
                                args->proc = proc;
                                args->task_data = &proc->taskdata[id];

                                size_t stack_depth = attr ? attr->stack_depth : _STACK_DEPTH_LOW;
                                size_t stack_size = stack_depth + (__OS_IRQ_STACK_DEPTH__) + (__OS_IO_STACK_DEPTH__);
                                stack_size = max(stack_size, _STACK_DEPTH_MINIMAL);

                                proc->taskdata[id].stack_size = stack_size;
                                proc->taskdata[id].id = id;
                                proc->taskdata[id].curr_syscall = NO_SYSCALL;
                                proc->taskdata[id].kernelspace = (proc->flag & PROC_FLAG_KWORKER);

                                if (proc->event) {
                                        _flag_clear(proc->event, _PROCESS_EXIT_FLAG(id));
                                }

                                err = _task_create(thread_code, "", proc->taskdata[id].stack_size, args,
                                        &proc->taskdata[id].task);
                                if (!err) {

                                        proc->taskdata[id].running = true;

                                        if (attr) {
                                                i16_t priority = min(attr->priority, _PRIORITY_HIGHEST);

                                                _task_set_priority(proc->taskdata[id].task, priority);
                                        }

                                        if (tid) {
                                                *tid = id;
                                        }

                                } else {
                                        _kfree(_MM_KRN, cast(void *, &args));
                                }
                        }
                }
        }

        return err;
}

//==============================================================================
/**
 * @brief  Function kill selected thread.
 *
 * @param  proc         process
 * @param  tid          thread ID
 *
 * @return One of errno value.
 */
//==============================================================================
int _process_thread_kill(_process_t *proc, tid_t tid)
{
        int err = EINVAL;

        if (is_proc_valid(proc) && (tid > 0) && is_tid_in_range(proc, tid)) {
                task_t *task = NULL;

                ATOMIC (process_mtx) {
                        task = proc->taskdata[tid].task;
                        proc->taskdata[tid].task = NULL;

                        if (proc->event) {
                                _flag_set(proc->event, _PROCESS_EXIT_FLAG(tid));
                        }

                        err = ESUCC;
                }

                /*
                 * NOTE: task is destroyed outside the process mutex, because
                 *       the thread can kill itself and mutex would never be
                 *       released.
                 */
                if (task) {
                        _task_destroy(task);
                }
        }

        return err;
}

//==============================================================================
/**
 * Function return task of selected thread (low level function).
 *
 * @param proc          process
 * @param tid           thread ID
 *
 * @return Task handle.
 */
//==============================================================================
task_t *_process_thread_get_task(_process_t *proc, tid_t tid)
{
        task_t *task = NULL;

        if (is_proc_valid(proc) && is_tid_in_range(proc, tid)) {
                ATOMIC (process_mtx) {
                        task = proc->taskdata[tid].task;
                }
        }

        return task;
}

//==============================================================================
/**
 * Function return thread statistics.
 *
 * @param  pid          process ID
 * @param  tid          thread ID
 * @param  stat         statistics
 *
 * @return One of errno value.
 */
//==============================================================================
int _process_thread_get_stat(pid_t pid, tid_t tid, _thread_stat_t *stat)
{
        int err = EINVAL;

        if (not pid or not stat) {
                return err;
        }

        err = ENOENT;

        ATOMIC (process_mtx) {
                _process_t *proc = NULL;

                foreach_process (p, active_process_list) {
                        if (p->pid == pid) {
                                proc = p;
                                goto finish;
                        }
                }

                foreach_process (p, terminate_process_list) {
                        if (p->pid == pid) {
                                proc = p;
                                goto finish;
                        }
                }

                foreach_process (p, destroy_process_list) {
                        if (p->pid == pid) {
                                proc = p;
                                goto finish;
                        }
                }

                foreach_process (p, zombie_process_list) {
                        if (p->pid == pid) {
                                proc = p;
                                goto finish;
                        }
                }

        finish:
                if (proc && (is_tid_in_range(proc, tid)) && proc->taskdata[tid].task) {

                        memset(stat, 0, sizeof(_thread_stat_t));

                        stat->tid = tid;

                        stat->CPU_load = 0;
                        for (size_t cpuid = 0; cpuid < _CPUCTL_CORE_NUM; cpuid++) {
                                stat->CPU_load += proc->taskdata[tid].CPU_load[cpuid];
                        }

                        stat->priority = _task_get_priority(proc->taskdata[tid].task);

                        stat->stack_size = proc->taskdata[tid].stack_size;

                        stat->stack_max_usage = proc->taskdata[tid].stack_size -
                                _task_get_free_stack(proc->taskdata[tid].task);

                        stat->syscalls_per_sec = proc->taskdata[tid].syscalls_per_sec;

                        err = ESUCC;
                }
        }

        return err;
}

//==============================================================================
/**
 * Function pin process thread to selected core.
 *
 * @param  pid          process ID
 * @param  tid          thread ID
 * @param  core_mask    core mask
 *
 * @return One of errno value.
 */
//==============================================================================
int _process_thread_pin_to_core(pid_t pid, tid_t tid, u32_t core_mask)
{
#if ((configNUMBER_OF_CORES > 1) && (configUSE_CORE_AFFINITY == 1))
        int err = EINVAL;

        if (not pid or (tid >= __OS_PROCESS_MAX_THREADS__) or not core_mask) {
                return err;
        }

        err = ENOENT;

        ATOMIC (process_mtx) {
                foreach_process (p, active_process_list) {
                        if (p->pid == pid) {
                                if (p->taskdata[tid].task) {
                                        _task_pin_core(p->taskdata[tid].task, core_mask);
                                        err = 0;
                                        break;
                                }
                        }
                }
        }

        return err;
#else
        UNUSED_ARG3(pid, tid, core_mask);
        return ENOTSUP;
#endif
}

//==============================================================================
/**
 * Function get current core.
 *
 * @param  core         current core
 *
 * @return One of errno value.
 */
//==============================================================================
int _process_get_current_core(u8_t *core)
{
        if (not core) {
                return EINVAL;
        }

#if ((configNUMBER_OF_CORES > 1) && (configUSE_CORE_AFFINITY == 1))
        *core = _cpuctl_get_cpu_id();
#else
        *core = 0;
#endif
        return 0;
}

//==============================================================================
/**
 * Thread kill helper.
 *
 * @param  arg          thread's argument
 */
//==============================================================================
static void _kill_thread_helper(void *arg)
{
        _process_t *proc;
        tid_t tid;
        _task_get_process_container(arg, &proc, &tid);
        _process_thread_kill(proc, tid);
        _task_exit();
}

//==============================================================================
/**
 * Function exit from calling thread.
 *
 * @param  exit         exit code
 *
 * @return One of errno value.
 */
//==============================================================================
int _process_thread_exit(int exit)
{
        _process_t *proc;
        tid_t tid;
        _task_get_process_container(NULL, &proc, &tid);

        int err = EINVAL;

        if (is_proc_valid(proc) and is_tid_in_range(proc, tid)) {

                if (tid == 0) {
                        _process_exit(proc, exit);

                } else {
                        proc->taskdata[tid].status = exit;

                        while (true) {
                                int err = _task_create(_kill_thread_helper, "", _STACK_DEPTH_MINIMAL,
                                        _task_get_handle(), NULL);
                                while (!err) {
                                        _sleep_ms(10);
                                }

                                _sleep_ms(100);
                        }
                }
        }

        return err;
}

//==============================================================================
/**
 * @brief  Function return thread exit status.
 *
 * @param  proc         process
 * @param  tid          thread id
 * @param  status       destiantion status pointer
 *
 * @return One of errno value.
 */
//==============================================================================
int _process_thread_get_status(_process_t *proc, tid_t tid, int *status)
{
        if (is_proc_valid(proc) and is_tid_in_range(proc, tid) and status) {
                *status = proc->taskdata[tid].status;
                return ESUCC;
        } else {
                return EINVAL;
        }
}

//==============================================================================
/**
 * @brief  Function return process container and thread ID associated with task.
 *
 * @param taskhdl       task (NULL for this task)
 * @param proc          process object (can be NULL)
 * @param tid           thread ID (can be NULL)
 */
//==============================================================================
void _task_get_process_container(task_t *taskhdl, _process_t **proc, tid_t *tid)
{
        ATOMIC (process_mtx) {
                taskhdl = taskhdl ? taskhdl : _task_get_handle();
                _assert(taskhdl);

                if (proc) {
                        _process_t *p = _task_get_storage_pointer(taskhdl, STORAGE_IDX_PROC);
                        _assert(p);
                        *proc = p;
                }

                if (tid) {
                        task_data_t *task_data = _task_get_storage_pointer(taskhdl, STORAGE_IDX_TASK_DATA);
                        _assert(task_data);
                        *tid = task_data->id;
                }
        }
}

//==============================================================================
/**
 * @brief  Function return process container associated with task.
 *
 * @param taskhdl       task (NULL for this task)
 * @param proc          process object
 *
 * @return On success process container pointer is returned, otherwise NULL.
 */
//==============================================================================
void _task_get_process_container_unprotected(task_t *taskhdl, _process_t **proc)
{
        taskhdl = taskhdl ? taskhdl : _task_get_handle();
        _assert(taskhdl);

        _process_t *p = _task_get_storage_pointer(taskhdl, STORAGE_IDX_PROC);

        _assert(p);
        *proc = p;
}

//==============================================================================
/**
 * @brief Function calculate general CPU load.
 *        Calculate are average values for 1/60, 1, 5, and 15 minutes.
 */
//==============================================================================
void _calculate_CPU_load_from_IRQ(void)
{
        for (size_t cpuid = 0; cpuid < _CPUCTL_CORE_NUM; cpuid++) {

                CPU_load_t *cpu_load = &CPU_load[cpuid];

                cpu_load->avg_CPU_load_calc.avg1sec = 0;

                foreach_process (proc, active_process_list) {

                        for (int tid = 0; tid < __OS_PROCESS_MAX_THREADS__; tid++) {

                                if (proc->taskdata[tid].task) {

                                        u32_t total_time_permille = cpu_load->CPU_total_time / 1000;

                                        u16_t cpu_load_per_core = total_time_permille
                                                ? (proc->taskdata[tid].timecnt[cpuid] / total_time_permille)
                                                : 0;

                                        proc->taskdata[tid].CPU_load[cpuid] = cpu_load_per_core;

                                        proc->taskdata[tid].timecnt[cpuid] = 0;

                                        cpu_load->avg_CPU_load_calc.avg1sec += proc->taskdata[tid].CPU_load[cpuid];

                                        proc->taskdata[tid].syscalls_per_sec = proc->taskdata[tid].syscalls_ctr;

                                        proc->taskdata[tid].syscalls_ctr = 0;
                                }
                        }
                }

                cpu_load->CPU_total_time = 0;

                // calculates average CPU load
                cpu_load->avg_CPU_load_result.avg1sec = cpu_load->avg_CPU_load_calc.avg1sec;

                cpu_load->avg_CPU_load_calc.avg1min += cpu_load->avg_CPU_load_result.avg1sec;
                cpu_load->avg_CPU_load_calc.avg5min += cpu_load->avg_CPU_load_result.avg1sec;
                cpu_load->avg_CPU_load_calc.avg15min += cpu_load->avg_CPU_load_result.avg1sec;

                if (_uptime_counter_sec % 60 == 0) {
                        cpu_load->avg_CPU_load_result.avg1min = cpu_load->avg_CPU_load_calc.avg1min / 60;
                        cpu_load->avg_CPU_load_calc.avg1min = 0;
                }

                if (_uptime_counter_sec % 300 == 0) {
                        cpu_load->avg_CPU_load_result.avg5min = cpu_load->avg_CPU_load_calc.avg5min / 300;
                        cpu_load->avg_CPU_load_calc.avg5min = 0;
                }

                if (_uptime_counter_sec % 900 == 0) {
                        cpu_load->avg_CPU_load_result.avg15min = cpu_load->avg_CPU_load_calc.avg15min / 900;
                        cpu_load->avg_CPU_load_calc.avg15min = 0;
                }
        }
}

//==============================================================================
/**
 * @brief Function return calculated average CPU load.
 *
 * @param avg   average measurements
 *
 * @return One of errno value.
 */
//==============================================================================
int _get_average_CPU_load(_avg_CPU_load_t *avg)
{
        if (avg) {
                avg->avg1sec = 0;
                avg->avg1min = 0;
                avg->avg5min = 0;
                avg->avg15min = 0;

                for (size_t i = 0; i < _CPUCTL_CORE_NUM; i++) {
                        avg->avg1sec += CPU_load[i].avg_CPU_load_result.avg1sec;
                        avg->avg1min += CPU_load[i].avg_CPU_load_result.avg1min;
                        avg->avg5min += CPU_load[i].avg_CPU_load_result.avg5min;
                        avg->avg15min += CPU_load[i].avg_CPU_load_result.avg15min;
                }

                return ESUCC;
        } else {
                return EINVAL;
        }
}

//==============================================================================
/**
 * @brief  Function check if all processes are consistent.
 *
 * @param  isr          called from ISR
 *
 * @return Return true if all is consistent, otherwise false.
 */
//==============================================================================
bool _process_is_consistent(bool isr)
{
        bool sanity_ok = true;
        u8_t cpuid = _cpuctl_get_cpu_id();

        if (not isr) {
                _kernel_scheduler_lock();
        }

        sanity_ok = (PID_cnt >= PID_MIN) && (PID_cnt <= PID_MAX);
        if (!sanity_ok) {
                if (not isr) {
                        _printk("PID counter not in range!");
                }
                goto end;
        }

        res_header_t *f = (void *)process_mtx;
        sanity_ok = (f->self == f) && (f->type == RES_TYPE_MUTEX);
        if (!sanity_ok) {
                if (not isr) {
                        _printk("Invalid process mutex!");
                }
                goto end;
        }

        f = (void *)kworker_mtx;
        sanity_ok = (f->self == f) && (f->type == RES_TYPE_MUTEX);
        if (!sanity_ok) {
                if (not isr) {
                        _printk("Invalid kworker mutex!");
                }
                goto end;
        }

        sanity_ok = active_process[cpuid] ? _mm_is_object_in_heap(active_process[cpuid]) : true;
        if (!sanity_ok) {
                goto end;
        }

        size_t process_counter = 0;

        foreach_process (p, active_process_list) {

                sanity_ok = is_proc_valid(p);
                if (!sanity_ok) {
                        if (not isr) {
                                _printk("invalid process @ %p!", p);
                        }
                        goto end;
                }

                sanity_ok = (p->pid >= PID_MIN) && (p->pid <= PID_MAX);
                if (!sanity_ok) {
                        if (not isr) {
                                _printk("PID %u: ID out of range!", p->pid);
                        }
                        goto end;
                }

                sanity_ok = (p->pdata != NULL);
                if (!sanity_ok) {
                        if (not isr) {
                                _printk("PID %u: NULL pdata!", p->pid);
                        }
                        goto end;
                }

                bool found = (p->pdata->main == _kworker_process);

                for (appset_table_t *appset = &appset_base; appset; appset = appset->next) {

                        for (size_t i = 0; (i < appset->table->number_of_programs) && !found; i++) {
                                found = (p->pdata == &appset->table->program_entry[i]);
                        }
                }
                if (not found) {
                        if (not isr) {
                                _printk("PID %u: pdata not found!", p->pid);
                        }
                        goto end;
                }

                sanity_ok = (strnlen(p->pdata->name, 256) < 256);
                if (!sanity_ok) {
                        if (not isr) {
                                _printk("PID %u: too long name!", p->pid);
                        }
                        goto end;
                }

                sanity_ok = (p->pdata->stack_depth != NULL);
                if (!sanity_ok) {
                        if (not isr) {
                                _printk("PID %u: NULL stack depth pointer!", p->pid);
                        }
                        goto end;
                }

                sanity_ok = (*p->pdata->stack_depth != 0);
                if (!sanity_ok) {
                        if (not isr) {
                                _printk("PID %u: 0 stack size!", p->pid);
                        }
                        goto end;
                }

                sanity_ok = not _mm_is_object_in_heap(p->pdata->main);
                if (!sanity_ok) {
                        if (not isr) {
                                _printk("PID %u: main object not in heap!", p->pid);
                        }
                        goto end;
                }

                res_header_t *flag = (void *)p->event;
                sanity_ok = (p->pdata->main != _kworker_process)
                        ? (_mm_is_object_in_heap(flag) && (flag->self == flag) && (flag->type == RES_TYPE_FLAG))
                        : true;
                if (!sanity_ok) {
                        if (not isr) {
                                _printk("PID %u: invalid event flag!", p->pid);
                        }
                        goto end;
                }

                dchain_t *chain = &p->desctab;
                while (chain and sanity_ok) {
                        for (size_t i = 0; sanity_ok and (i < ARRAY_SIZE(chain->resource)); i++) {
                                if (chain->resource[i]) {
                                        if (not _mm_is_object_in_heap(chain->resource[i])) {

                                                sanity_ok = false;
                                                if (not isr) {
                                                        _printk("PID %u: descriptor %u "
                                                                "out of heap!",
                                                                p->pid, i);
                                                }
                                                goto end;

                                        } else if (chain->resource[i]->proc != NULL) {
                                                sanity_ok = false;
                                                if (not isr) {
                                                        _printk("PID %u: descriptor %u "
                                                                "invalid proc-ref!",
                                                                p->pid, i);
                                                }
                                                goto end;

                                        } else if (chain->resource[i]->next != NULL) {

                                                sanity_ok = false;
                                                if (not isr) {
                                                        _printk("PID %u: descriptor %u "
                                                                "next not NULL!",
                                                                p->pid, i);
                                                }
                                                goto end;

                                        } else if (chain->resource[i]->prev != NULL) {

                                                sanity_ok = false;
                                                if (not isr) {
                                                        _printk("PID %u: descriptor %u "
                                                                "prev not NULL!",
                                                                p->pid, i);
                                                }
                                                goto end;
                                        }
                                }
                        }

                        chain = chain->next;
                }

                sanity_ok = (p->pdata->globals_size && (*p->pdata->globals_size > 0))
                        ? _mm_is_object_in_heap(p->globals)
                        : true;
                if (!sanity_ok) {
                        if (not isr) {
                                _printk("PID %u: globals not in heap!", p->pid);
                        }
                        goto end;
                }

                size_t n = 0;
                size_t m = p->res_list_size;
                res_header_t *prev = NULL;
                res_header_t *res = p->res_list;
                while (res && m) {

                        sanity_ok = _mm_is_object_in_heap(res) && (res->self == res) &&
                                ((res->type == RES_TYPE_DIR) || (res->type == RES_TYPE_MEMORY) ||
                                        (res->type == RES_TYPE_SEMAPHORE) || (res->type == RES_TYPE_FILE) ||
                                        (res->type == RES_TYPE_FLAG) || (res->type == RES_TYPE_MUTEX) ||
                                        (res->type == RES_TYPE_QUEUE) || (res->type == RES_TYPE_SOCKET));
                        if (!sanity_ok) {
                                if (not isr) {
                                        _printk("PID %u: invalid resource!", p->pid);
                                }
                                goto end;
                        }

                        sanity_ok = res->next != res;
                        if (!sanity_ok) {
                                if (not isr) {
                                        _printk("PID %u: resoruce loop!", p->pid);
                                }
                                goto end;
                        }

                        sanity_ok = res->prev == prev;
                        if (!sanity_ok) {
                                if (not isr) {
                                        _printk("PID %u: invalid prev resource!", p->pid);
                                }
                                goto end;
                        }

                        sanity_ok = is_proc_valid(res->proc);
                        if (!sanity_ok) {
                                if (not isr) {
                                        _printk("PID %u: invalid proc resource!", p->pid);
                                }
                                goto end;
                        }

                        prev = res;
                        res = res->next;
                        n++;
                        m--;
                }

                sanity_ok = (n == p->res_list_size) && (m == 0);
                if (!sanity_ok) {
                        if (not isr) {
                                _printk("PID %u: resource number mismatch!", p->pid);
                        }
                        goto end;
                }

                sanity_ok = (p->cwd != NULL);
                if (!sanity_ok) {
                        if (not isr) {
                                _printk("PID %u: NULL CWD!", p->pid);
                        }
                        goto end;
                }

                sanity_ok = _mm_is_object_in_heap(p->argv) && (p->argc >= 1);
                if (!sanity_ok) {
                        if (not isr) {
                                _printk("PID %u: argv not in heap!", p->pid);
                        }
                        goto end;
                }

                sanity_ok = (p->pdata->main == _kworker_process) ? ((p->flag & PROC_FLAG_KWORKER) == PROC_FLAG_KWORKER)
                                                                 : true;
                if (!sanity_ok) {
                        if (not isr) {
                                _printk("PID %u: kworker without flag!", p->pid);
                        }
                        goto end;
                }

                sanity_ok = (p->pdata->main != _kworker_process) ? ((p->flag & PROC_FLAG_KWORKER) == 0) : true;
                if (!sanity_ok) {
                        if (not isr) {
                                _printk("PID %u: flagged as kworker!", p->pid);
                        }
                        goto end;
                }

                for (int i = 0; i < __OS_PROCESS_MAX_THREADS__; i++) {
                        sanity_ok = (p->taskdata[i].task) ? _mm_is_object_in_heap(p->taskdata[i].task) : true;
                        if (!sanity_ok) {
                                if (not isr) {
                                        _printk("PID %u: thread %u task not in heap!", p->pid, i);
                                }
                                goto end;
                        }

                        sanity_ok = (p->taskdata[i].task) ? (p->taskdata[i].stack_size > 0) : true;
                        if (!sanity_ok) {
                                if (not isr) {
                                        _printk("PID %u: 0 stack size of thread %u!", p->pid, i);
                                }
                                goto end;
                        }
                }

                if (++process_counter >= 256) {
                        sanity_ok = false;
                        if (not isr) {
                                _printk("Looped process list!");
                        }
                        goto end;
                }
        }

end:
        if (not isr) {
                _kernel_scheduler_unlock();
        }

        return sanity_ok;
}

//==============================================================================
/**
 * @brief  Function mark thread that enter kernel space.
 *
 * @param  proc         process
 */
//==============================================================================
void _process_enter_kernelspace(_process_t *proc, u8_t syscall)
{
        extern struct _process *_kworker_proc;

        if (is_proc_valid(proc) and is_proc_valid(_kworker_proc)) {

                bool kill_request = false;

                ATOMIC (process_mtx) {
                        tid_t i = _process_get_active_thread(proc, false);
                        proc->taskdata[i].kernelspace = true;
                        proc->taskdata[i].curr_syscall = syscall;
                        proc->taskdata[i].syscalls_ctr++;
                        _kworker_proc->taskdata[0].syscalls_ctr++;

                        kill_request = proc->flag & PROC_FLAG_KILL;
                        proc->taskdata[i].running = !kill_request;

                        if (kill_request) {
                                /*
                                 * NOTE: syscall will not be executed, so the thread
                                 *       must not be visible as being in kernel space,
                                 *       otherwise the process clean up waits forever.
                                 */
                                proc->taskdata[i].kernelspace = false;
                                proc->taskdata[i].curr_syscall = NO_SYSCALL;
                        }
                }

                if (kill_request) {
                        // thread is parked, kworker can finish the killed process
                        _kworker_wake();
                        _task_suspend(NULL);
                        while (true) {
                                ;
                        }
                }
        }
}

//==============================================================================
/**
 * @brief  Function mark thread that exit kernel space.
 *
 * @param  proc         process
 */
//==============================================================================
void _process_exit_kernelspace(_process_t *proc)
{
        if (is_proc_valid(proc)) {
                bool kill_request = false;

                ATOMIC (process_mtx) {
                        tid_t i = _process_get_active_thread(proc, false);
                        proc->taskdata[i].kernelspace = false;
                        proc->taskdata[i].curr_syscall = NO_SYSCALL;

                        kill_request = proc->flag & PROC_FLAG_KILL;
                        proc->taskdata[i].running = !kill_request;
                }

                if (kill_request) {
                        // thread is parked, kworker can finish the killed process
                        _kworker_wake();
                        _task_suspend(NULL);
                        while (true) {
                                ;
                        }
                }
        }
}

//==============================================================================
/**
 * @brief  Function get kernelspace indicator.
 *
 * @param  proc         process
 * @param  tid          thread ID
 * @param  isr          called from ISR
 *
 * @return Kernelspace indicator.
 */
//==============================================================================
bool _process_is_kernelspace(_process_t *proc, tid_t tid, bool isr)
{
        bool kernelspace = false;

        if (is_proc_valid(proc) && is_tid_in_range(proc, tid)) {

                if (isr) {
                        kernelspace = proc->taskdata[tid].kernelspace;
                } else {
                        ATOMIC (process_mtx) {
                                kernelspace = proc->taskdata[tid].kernelspace;
                        }
                }
        }

        return kernelspace;
}

//==============================================================================
/**
 * @brief  Function get current syscall.
 *
 * @param  proc         process
 * @param  isr          called from ISR
 *
 * @return Current syscall.
 */
//==============================================================================
u8_t _process_get_curr_syscall(_process_t *proc, tid_t tid, bool isr)
{
        u8_t syscall = NO_SYSCALL;

        if (is_proc_valid(proc) && is_tid_in_range(proc, tid)) {
                if (isr) {
                        syscall = proc->taskdata[tid].curr_syscall;

                } else {
                        ATOMIC (process_mtx) {
                                syscall = proc->taskdata[tid].curr_syscall;
                        }
                }
        }

        return syscall;
}

//==============================================================================
/**
 * @brief  Function allocate descriptor.
 *
 * @param  process      process
 * @param  descriptor   descriptor destination
 * @param  res          resource pointer
 * @param  shared       shared descriptor
 *
 * @return One of errno value.
 */
//==============================================================================
int _process_descriptor_allocate(_process_t *process, int *desc, res_header_t *res, bool shared)
{
        int err = EMFILE;

        ATOMIC (process_mtx) {

                dchain_t *chain = NULL;

                res->proc = NULL;
                res->next = NULL;
                res->prev = NULL;

                if (shared) {
                        chain = &shared_desctab;
                } else {
                        chain = &process->desctab;
                }

                int node = 0;
                bool found = false;

                while (chain and (not found)) {
                        for (size_t i = 0; not found and (i < ARRAY_SIZE(chain->resource)); i++) {
                                if (chain->resource[i] == NULL) {
                                        chain->resource[i] = res;
                                        *desc = (node << 4) | i | (shared ? SHARED_DESCRIPTOR : 0);
                                        found = true;
                                        err = ESUCC;
                                        break;
                                }
                        }

                        if (not found) {
                                if (chain->next == NULL) {
                                        err = _kzalloc(_MM_KRN, sizeof(dchain_t), _cpuctl_fast_mem, 0, 0,
                                                cast(void **, &chain->next));
                                        if (err) {
                                                chain->next = NULL;
                                        }
                                }

                                chain = chain->next;
                                node++;
                        }
                }
        }

        return err;
}

//==============================================================================
/**
 * @brief  Function free descriptor (and resource).
 *
 * @param  process      process
 * @param  descriptor   descriptor
 * @param  rtype        object type (validation)
 *
 * @return One of errno value.
 */
//==============================================================================
int _process_descriptor_free(_process_t *process, int desc, res_type_t rtype)
{
        int err = ENOENT;

        if (desc < 0) {
                return err;
        }

        ATOMIC (process_mtx) {

                dchain_t *chain = NULL;

                if (desc & SHARED_DESCRIPTOR) {
                        chain = &shared_desctab;
                        desc &= ~SHARED_DESCRIPTOR;
                } else {
                        chain = &process->desctab;
                }

                int node = 0;

                while (chain) {
                        if ((desc >> 4) == node) {
                                res_header_t *res = chain->resource[desc & 0xF];

                                if (res == NULL) {
                                        err = ENOENT;

                                } else if ((rtype != RES_TYPE_UNKNOWN) and (res->type != rtype)) {
                                        printk("Descriptor %d points to "
                                               "incorrect object type!",
                                                desc);
                                        err = EINVAL;

                                } else {
                                        err = resource_destroy(res);
                                        if (!err) {
                                                chain->resource[desc & 0xF] = NULL;
                                        }
                                }

                                break;
                        }

                        chain = chain->next;
                        node++;
                }
        }

        return err;
}

//==============================================================================
/**
 * @brief  Function free descriptor.
 *
 * @param  process      process
 * @param  desc         descriptor
 * @param  res          resource pointer
 *
 * @return One of errno value.
 */
//==============================================================================
int _process_descriptor_get_resource(_process_t *process, int desc, res_header_t **res)
{
        int err = ENOENT;

        if (desc < 0) {
                return err;
        }

        ATOMIC (process_mtx) {

                dchain_t *chain = NULL;

                if (desc & SHARED_DESCRIPTOR) {
                        chain = &shared_desctab;
                        desc &= ~SHARED_DESCRIPTOR;
                } else {
                        chain = &process->desctab;
                }

                int node = 0;

                while (chain) {
                        if ((desc >> 4) == node) {
                                if (chain->resource[desc & 0xF]) {
                                        *res = chain->resource[desc & 0xF];
                                        err = ESUCC;
                                }
                                break;
                        }

                        chain = chain->next;
                        node++;
                }
        }

        return err;
}

//==============================================================================
/**
 * @brief  This function start user process code [USERLAND].
 *
 * @param  mainfn         process argument - process main function
 */
//==============================================================================
static void process_code(void *arg)
{
        _assert(arg);

        task_args_t args = *cast(task_args_t *, arg);
        _kfree(_MM_KRN, &arg);

        _task_set_storage_pointer(NULL, STORAGE_IDX_PROC, args.proc);
        _task_set_storage_pointer(NULL, STORAGE_IDX_TASK_DATA, args.task_data);
        _task_yield();

        if (args.at_init) {
                args.at_init(args.at_init_ctx);
        }

        process_func_t func = args.func;
        args.task_data->status = func(args.proc->argc, args.proc->argv);

        _process_exit(args.proc, args.task_data->status);
        _task_suspend(_THIS_TASK);
        for (;;) {
                ;
        }
}

//==============================================================================
/**
 * @brief  This function start user thread code [USERLAND].
 *
 * @param  arg          thread arguments - main function and argument value.
 */
//==============================================================================
static void thread_code(void *arg)
{
        _assert(arg);

        task_args_t args = *cast(task_args_t *, arg);
        _kfree(_MM_KRN, cast(void *, &arg));

        _task_set_storage_pointer(NULL, STORAGE_IDX_PROC, args.proc);
        _task_set_storage_pointer(NULL, STORAGE_IDX_TASK_DATA, args.task_data);
        _task_yield();

        thread_func_t func = args.func;
        args.task_data->status = func(args.arg);

        ATOMIC (process_mtx) {
                if (args.proc->event) {
                        _flag_set(args.proc->event, _PROCESS_EXIT_FLAG(args.task_data->id));
                }

                args.task_data->task = NULL;
        }

        _task_exit();
}

//==============================================================================
/**
 * Function remove process from selected list.
 *
 * @param  proc         process to remove
 * @param  list         list
 */
//==============================================================================
static void process_remove_from_list(_process_t *proc, _process_t **list)
{
        ATOMIC (process_mtx) {
                _process_t *prev = NULL;
                foreach_process (p, *list) {
                        if (p == proc) {
                                if (prev) {
                                        prev->header.next = proc->header.next;
                                } else {
                                        *list = cast(_process_t *, proc->header.next);
                                }

                                proc->header.next = NULL;
                                break;
                        } else {
                                prev = p;
                        }
                }
        }
}

//==============================================================================
/**
 * Function move process from selected list to another.
 *
 * @param  proc         process to move
 * @param  list_from    source list
 * @param  list_to      destination list
 */
//==============================================================================
static bool process_move_list(_process_t *proc, _process_t **list_from, _process_t **list_to)
{
        bool moved = false;

        ATOMIC (process_mtx) {
                _process_t *prev = NULL;
                foreach_process (p, *list_from) {
                        if (p == proc) {

                                if (prev) {
                                        prev->header.next = proc->header.next;
                                } else {
                                        *list_from = cast(_process_t *, proc->header.next);
                                }

                                proc->header.next = cast(struct res_header *, *list_to);

                                *list_to = proc;

                                moved = true;
                                break;
                        } else {
                                prev = p;
                        }
                }
        }

        return moved;
}

//==============================================================================
/**
 * @brief  Function release resources of process that is in the release list.
 *         Resources are released without process mutex. When resources are
 *         released then process is moved to the zombie list (exit flag is set)
 *         or detached process object is freed.
 *
 * @param  proc         process in the release list
 */
//==============================================================================
static void process_release(_process_t *proc)
{
        process_destroy_all_resources(proc);

        /*
         * NOTE: mutex must be locked to not leave process in the release
         *       list. Lock can be canceled if calling process is killed.
         */
        while (_mutex_lock(process_mtx, _WAIT_FOREVER_MS) != ESUCC) {
                ;
        }

        if (not(proc->flag & PROC_FLAG_DETACHED)) {
                process_move_list(proc, &release_process_list, &zombie_process_list);

                // waiting process can remove zombie from now on
                if (proc->event) {
                        _flag_set(proc->event, _PROCESS_EXIT_FLAG(0));
                }
        } else {
                process_remove_from_list(proc, &release_process_list);
                _flag_destroy(proc->event);
                proc->event = NULL;
                proc->header.self = NULL;
                proc->header.type = RES_TYPE_UNKNOWN;
                _kfree(_MM_KRN, cast(void *, &proc));
        }

        _mutex_unlock(process_mtx);
}

//==============================================================================
/**
 * @brief  Function destroy (release) all process resources. Function does not
 *         destroy process object.
 *
 * @param  proc     selected process container
 */
//==============================================================================
static void process_destroy_all_resources(_process_t *proc)
{
        destroy_all_tasks(proc, true);

        if (proc->argv) {
                argtab_destroy(proc->argv);
                proc->argv = NULL;
                proc->argc = 0;
        }

        // close files, directories, sockets
        res_header_t *resource_curr = proc->res_list;
        res_header_t *resource_prev = NULL;
        res_header_t *resource_next = NULL;

        while (resource_curr) {
                resource_next = resource_curr->next;

                if (resource_curr->type != RES_TYPE_MEMORY) {

                        if (resource_curr == proc->res_list) {
                                proc->res_list = resource_next;
                        } else {
                                if (resource_prev) {
                                        resource_prev->next = resource_next;
                                }
                        }

                        int err = resource_destroy(resource_curr);
                        if (err != ESUCC) {
                                printk("PROCESS: PID %d: unknown object %p\n", proc->pid, resource_curr);
                        }
                } else {
                        resource_prev = resource_curr;
                }

                resource_curr = resource_next;
        }

        // free all objects accessed from descriptor (files, dirs, mutexes, semaphores,
        // queues, sockets)
        dchain_t *chain = &proc->desctab;
        while (chain) {
                for (size_t i = 0; i < ARRAY_SIZE(chain->resource); i++) {
                        if (chain->resource[i]) {
                                int err = resource_destroy(chain->resource[i]);
                                if (!err) {
                                        chain->resource[i] = NULL;
                                } else {
                                        printk("PROCESS: PID %d: unknown object %p\n", proc->pid, chain->resource[i]);
                                }
                        }
                }

                chain = chain->next;
        }

        // free all memory regions
        while (proc->res_list) {
                res_header_t *resource = proc->res_list;
                proc->res_list = resource->next;

                int err = resource_destroy(resource);
                if (err != ESUCC) {
                        printk("PROCESS: PID %d: unknown object %p\n", proc->pid, resource);
                }
        }

        if (proc->cwd) {
                _kfree(_MM_KRN, cast(void *, &proc->cwd));
        }

        // detach from all shared memory regions
#if __OS_ENABLE_SHARED_MEMORY__ > 0
        _shm_detach_anywhere(proc->pid);
#endif

        proc->res_list_size = 0;
        proc->res_list = NULL;
        proc->globals = NULL;
}

//==============================================================================
/**
 * @brief  Function gets process statistics.
 *
 * @param  proc         process
 * @param  stat         statistics container
 */
//==============================================================================
static void process_get_stat(_process_t *proc, _process_stat_t *stat)
{
        char *cmdline_buf = stat->cmdline_buf;
        size_t cmdline_len = stat->cmdline_len;

        memset(stat, 0, sizeof(_process_stat_t));
        stat->cmdline_buf = cmdline_buf;
        stat->cmdline_len = cmdline_len;

        if (stat->cmdline_buf and stat->cmdline_len) {
                size_t n = 0;

                for (size_t i = 0; i < proc->argc; i++) {
                        n += sys_scnprintf(&stat->cmdline_buf[n], stat->cmdline_len - n, "%s ", proc->argv[i]);
                }

                stat->cmdline_buf[n] = '\0';
        }

        stat->pid = proc->pid;
        stat->priority = (proc->taskdata[0].task) ? _task_get_priority(proc->taskdata[0].task) : 0;
        stat->CPU_load = 0;

        u8_t threads = __OS_PROCESS_MAX_THREADS__;
        for (tid_t tid = 0; tid < threads; tid++) {
                if (proc->taskdata[tid].task) {
                        stat->threads_count++;

                        for (size_t cpuid = 0; cpuid < _CPUCTL_CORE_NUM; cpuid++) {
                                stat->CPU_load += proc->taskdata[tid].CPU_load[cpuid];
                        }

                        stat->syscalls_per_sec += proc->taskdata[tid].syscalls_per_sec;

                        stat->stack_max_usage += (proc->taskdata[tid].stack_size -
                                _task_get_free_stack(proc->taskdata[tid].task));

                        stat->stack_size += proc->taskdata[tid].stack_size;
                }
        }

        foreach_resource (res, proc->res_list) {
                switch (res->type) {
                case RES_TYPE_FILE:
                        stat->files_count++;
                        break;

                case RES_TYPE_DIR:
                        stat->dir_count++;
                        break;

                case RES_TYPE_MUTEX:
                        stat->mutexes_count++;
                        break;

                case RES_TYPE_QUEUE:
                        stat->queue_count++;
                        break;

                case RES_TYPE_FLAG:
                case RES_TYPE_SEMAPHORE:
                        stat->semaphores_count++;
                        break;

                case RES_TYPE_MEMORY:
                        stat->memory_block_count++;
                        stat->memory_usage += _mm_get_block_size(res);
                        break;

                case RES_TYPE_SOCKET:
                        stat->socket_count++;
                        break;

                default:
                        break;
                }
        }

        dchain_t *chain = &proc->desctab;
        while (chain) {
                for (size_t i = 0; i < ARRAY_SIZE(chain->resource); i++) {
                        if (chain->resource[i]) {
                                switch (chain->resource[i]->type) {
                                case RES_TYPE_FILE:
                                        stat->files_count++;
                                        break;

                                case RES_TYPE_DIR:
                                        stat->dir_count++;
                                        break;

                                case RES_TYPE_MUTEX:
                                        stat->mutexes_count++;
                                        break;

                                case RES_TYPE_QUEUE:
                                        stat->queue_count++;
                                        break;

                                case RES_TYPE_FLAG:
                                case RES_TYPE_SEMAPHORE:
                                        stat->semaphores_count++;
                                        break;

                                case RES_TYPE_SOCKET:
                                        stat->socket_count++;
                                        break;

                                default:
                                        break;
                                }
                        }
                }

                chain = chain->next;
        }

        stat->memory_usage = _mm_align(stat->memory_usage);
}

//==============================================================================
/**
 * @brief  Function apply process attributes.
 *
 * @param  proc     process
 * @param  atrr     attributes
 *
 * @return One of errno value.
 */
//==============================================================================
static int process_apply_attributes(_process_t *proc, const _process_attr_t *attr)
{
        int err = ESUCC;

        if (attr) {
                u8_t cpuid = _cpuctl_get_cpu_id();

                /*
                 * Apply Current Working Directory path
                 */
                size_t cwdlen = attr->cwd ? strsize(attr->cwd) : strsize("/");

                err = _kmalloc(_MM_KRN, cwdlen, NULL, 0, 0, cast(void *, &proc->cwd));
                if (!err) {
                        if (attr->cwd && attr->cwd[0] == '/') {
                                strcpy(proc->cwd, attr->cwd);
                        } else {
                                strcpy(proc->cwd, "/");
                        }

                        _vfs_realpath(proc->cwd, SUB_SLASH);
                } else {
                        goto finish;
                }

                /*
                 * Create stdin/out/err descriptors
                 */
                struct vfs_path cpath;
                cpath.CWD = proc->cwd;

                const struct {
                        int fd;
                        const char *path;
                        int flags;
                } stdio[3] = {
                        {attr->fd_stdin, attr->p_stdin, O_RDONLY},
                        {attr->fd_stdout, attr->p_stdout, O_RDWR | O_APPEND},
                        {attr->fd_stderr, attr->p_stderr, O_RDWR | O_APPEND},
                };

                for (size_t i = 0; i < ARRAY_SIZE(stdio); i++) {
                        if (stdio[i].fd >= 0) {
                                res_header_t *res = NULL;
                                _process_descriptor_get_resource(active_process[cpuid], stdio[i].fd, &res);

                                if (res and (res->type == RES_TYPE_FILE)) {
                                        if (_vfs_fopen_ref(res->self) == ESUCC) {
                                                proc->desctab.resource[i] = res;
                                        }
                                }

                        } else if (stdio[i].path) {
                                cpath.PATH = stdio[i].path;

                                kfile_t *file;
                                err = _vfs_fopen(&cpath, stdio[i].flags, 0, &file);
                                if (err) {
                                        goto finish;
                                }

                                // descriptor number must match the stream (0: stdin, 1: stdout, 2: stderr)
                                file->header.proc = NULL;
                                file->header.next = NULL;
                                file->header.prev = NULL;
                                proc->desctab.resource[i] = &file->header;
                        }
                }

                /*
                 * Set detached flag
                 */
                proc->flag |= attr->detached ? PROC_FLAG_DETACHED : 0;
        }

finish:
        return err;
}

//==============================================================================
/**
 * @brief  Function destroy all process tasks.
 *
 * @param  proc         process container
 * @param  with_zero    destroy all tasks with task number zero (main process task)
 */
//==============================================================================
static void destroy_all_tasks(_process_t *proc, bool with_zero)
{
        u8_t threads = __OS_PROCESS_MAX_THREADS__;

        for (tid_t tid = with_zero ? 0 : 1; tid < threads; tid++) {
                if (proc->taskdata[tid].task) {
                        _task_destroy(proc->taskdata[tid].task);
                        proc->taskdata[tid].task = NULL;
                }
        }
}

//==============================================================================
/**
 * @brief Function create PID number.
 *
 * @return On success PID number is returned, otherwise 0.
 */
//==============================================================================
static int get_pid(pid_t *pid)
{
        int err = ESRCH;

        ATOMIC (process_mtx) {
                int n = PID_MAX + 1;
                while (pid && (n-- > 0)) {
                        if (++PID_cnt >= (PID_MAX + 1)) {
                                PID_cnt = PID_MIN;
                        }

                        _process_t *const lists[] = {
                                active_process_list,
                                terminate_process_list,
                                destroy_process_list,
                                release_process_list,
                                zombie_process_list,
                        };

                        bool found = false;

                        for (size_t l = 0; (l < ARRAY_SIZE(lists)) and not found; l++) {
                                foreach_process (proc, lists[l]) {
                                        found = proc->pid == PID_cnt;
                                        if (found) {
                                                break;
                                        }
                                }
                        }

                        if (not found) {
                                *pid = PID_cnt;
                                err = ESUCC;
                                break;
                        }
                }
        }

        return err;
}

//==============================================================================
/**
 * @brief  Function destroy (release) selected resource.
 *
 * @param  resource     resource to release
 *
 * @return One of errno value.
 */
//==============================================================================
static int resource_destroy(res_header_t *resource)
{
        if (!_mm_is_object_in_heap(resource)) {
                printk("Resource object %p out of heap", resource);
                return EINVAL;
        }

        res_header_t *res2free = resource;

        switch (resource->type) {
        case RES_TYPE_FILE:
                _vfs_fclose(cast(kfile_t *, res2free));
                break;

        case RES_TYPE_DIR:
                _vfs_closedir(cast(kdir_t *, res2free));
                break;

        case RES_TYPE_MEMORY:
                _kfree(_MM_PROG, cast(void *, &res2free));
                break;

        case RES_TYPE_MUTEX:
                _mutex_destroy(cast(kmtx_t *, res2free));
                break;

        case RES_TYPE_QUEUE:
                _queue_destroy(cast(kqueue_t *, res2free));
                break;

        case RES_TYPE_SEMAPHORE:
                _semaphore_destroy(cast(ksem_t *, res2free));
                break;

        case RES_TYPE_FLAG:
                _flag_destroy(cast(kflag_t *, res2free));
                break;

        case RES_TYPE_SOCKET:
#if _ENABLE_NETWORK_ == _YES_
                _net_socket_destroy(cast(ksocket_t *, res2free));
                break;
#else
                return ENOTSUP;
#endif

        default:
                printk("Unknown resource object type %p", resource);
                return EINVAL;
        }

        return ESUCC;
}

//==============================================================================
/**
 * @brief  Function check if first command argument is a path.
 *
 * @param  cmd          command to check
 *
 * @return True if command is in path form, otherwise false.
 */
//==============================================================================
#if __OS_SYSTEM_SHEBANG_ENABLE__ > 0
static bool is_cmd_path(const char *cmd)
{
        cmd += strspn(cmd, " ");
        return ((cmd[0] == '.' && cmd[1] == '/') || (cmd[0] == '.' && cmd[1] == '.' && cmd[2] == '/') ||
                (cmd[0] == '/'));
}
#endif

//==============================================================================
/**
 * @brief  Function check if argument is a file with shebang (#!).
 *
 * @param  proc         process
 * @param  cmd          RAW command
 * @param  cmdarg       commands after shebang analyze
 *
 * @return One of errno value.
 */
//==============================================================================
#if __OS_SYSTEM_SHEBANG_ENABLE__ > 0
static int analyze_shebang(_process_t *proc, const char *cmd, char **cmdarg)
{
        // allocations
        char *filename = NULL;
        char *line = NULL;
        kfile_t *file = NULL;

        // skip leading spaces
        cmd += strspn(cmd, " ");

        // argument start
        char *args = strchr(cmd, ' ');
        args += args ? 1 : 0;
        size_t argslen = args ? strsize(args) : 0;

        // file name length
        size_t fnlen = args ? ((u32_t)args - (u32_t)cmd) : strsize(cmd);

        // allocate file name
        int err = _kmalloc(_MM_KRN, fnlen, NULL, 0, 0, cast(void **, &filename));
        if (err) {
                goto finish;
        }
        sys_strlcpy(filename, cmd, fnlen);

        // allocate line
        err = _kzalloc(_MM_KRN, SHEBANGLEN, NULL, 0, 0, cast(void **, &line));
        if (err) {
                goto finish;
        }

        // open file
        struct vfs_path path;
        path.CWD = proc->cwd;
        path.PATH = filename;
        err = _vfs_fopen(&path, O_RDONLY, 0, &file);
        if (err) {
                goto finish;
        }

        // check that file is set to execute
        struct stat st;
        st.st_mode = 0;
        err = _vfs_fstat(file, &st);
        if (err) {
                goto finish;
        }
        if ((st.st_mode & S_IXUSR) == 0) {
                err = EACCES;
                goto finish;
        }

        // read line
        size_t rd = 0;
        err = _vfs_fread(line, SHEBANGLEN - 1, &rd, file);
        if (err) {
                goto finish;
        }

        // check shebang
        if (line[0] == '#' && line[1] == '!') {
                char *p = line + 2;
                p += strspn(p, " ");

                char *end = strchr(p, '\r');
                end = end ? end : strchr(p, '\n');

                if (end) {
                        *end = '\0';
                }

                size_t arglen = strsize(p) + strsize(filename) + argslen;

                err = _kmalloc(_MM_KRN, arglen, NULL, 0, 0, cast(void **, cmdarg));
                if (!err) {
                        sys_strlcpy(*cmdarg, p, arglen);
                        sys_strlcat(*cmdarg, " ", arglen);
                        sys_strlcat(*cmdarg, filename, arglen);

                        if (args) {
                                sys_strlcat(*cmdarg, " ", arglen);
                                sys_strlcat(*cmdarg, args, arglen);
                        }
                }
        }

finish:
        if (filename) {
                _kfree(_MM_KRN, cast(void **, &filename));
        }

        if (line) {
                _kfree(_MM_KRN, cast(void **, &line));
        }

        if (file) {
                _vfs_fclose(file);
        }

        return err;
}
#endif

//==============================================================================
/**
 * @brief Function create new table with argument pointers.
 *
 * @param[in]  str              argument string
 * @param[out] argc             number of argument
 * @param[out] argv             pointer to pointer of argument array
 *
 * @return One of errno value.
 */
//==============================================================================
static int argtab_create(const char *str, u8_t *argc, char **argv[])
{
        int err = EINVAL;

        if (!isstrempty(str) && argc && argv) {

                llist_t *largs;
                err = _llist_create_krn(_MM_KRN, NULL, NULL, &largs);

                if (!err) {
                        // parse arguments
                        while (*str != '\0') {
                                // skip spaces
                                str += strspn(str, " ");

                                // select character to find as end of argument
                                bool quo = false;
                                char find = ' ';
                                if (*str == '\'' || *str == '"') {
                                        quo = true;
                                        find = *str;
                                        str++;
                                }

                                // find selected character
                                const char *start = str;
                                const char *end = strchr(str, find);

                                // check if string end is reached
                                if (!end) {
                                        end = strchr(str, '\0');
                                } else {
                                        end++;
                                }

                                // calculate argument length (without nul character)
                                int str_len = end - start;
                                if (str_len == 0) {
                                        break;
                                }

                                if (quo || *(end - 1) == ' ') {
                                        str_len--;
                                }

                                // add argument to list
                                char *arg;
                                err = _kmalloc(_MM_KRN, str_len + 1, NULL, 0, 0, cast(void **, &arg));
                                if (!err) {
                                        _strlcpy(arg, start, str_len + 1);

                                        if (sys_llist_push_back(largs, arg) == NULL) {
                                                _kfree(_MM_KRN, cast(void *, &arg));
                                                goto finish;
                                        }
                                } else {
                                        goto finish;
                                }

                                // next token
                                str = end;
                        }

                        // create table with arguments
                        int no_of_args = sys_llist_size(largs);
                        *argc = no_of_args;

                        char **arg = NULL;
                        err = _kmalloc(_MM_KRN, (no_of_args + 1) * sizeof(char *), NULL, 0, 0, cast(void *, &arg));
                        if (!err) {
                                for (int i = 0; i < no_of_args; i++) {
                                        arg[i] = sys_llist_take_front(largs);
                                }

                                arg[no_of_args] = NULL;
                                *argv = arg;
                        }

                finish:
                        sys_llist_destroy(largs);
                }
        }

        return err;
}

//==============================================================================
/**
 * @brief  Function remove argument table.
 *
 * @param  argv          pointer to argument table (must be ended with NULL)
 */
//==============================================================================
static void argtab_destroy(char **argv)
{
        if (argv) {
                int n = 0;
                while (argv[n]) {
                        _kfree(_MM_KRN, cast(void *, &argv[n]));
                        n++;
                }

                _kfree(_MM_KRN, cast(void *, &argv));
        }
}

//==============================================================================
/**
 * @brief  Return program table pointer.
 *
 * @return Program table address.
 */
//==============================================================================
const appset_table_t *_process_get_appset_table(void)
{
        return &appset_base;
}

//==============================================================================
/**
 * @brief  Register new appset(s).
 *
 * @param  appset_address_array         appset address array
 * @param  appset_count                 number of addresses
 *
 * @return One of errno value.
 */
//==============================================================================
int _process_register_appset(const void **appset_address_array, size_t appset_count)
{
        int err = EINVAL;

        if (not appset_address_array or not appset_count) {
                return err;
        }

        appset_table_t *appset = &appset_base;

        for (; appset; appset = appset->next) {
                if (not appset->next) {
                        break;
                }
        }

        for (size_t i = 0; i < appset_count; i++) {

                appset_table_t *at = NULL;
                err = _kmalloc(_MM_KRN, sizeof(*at), _cpuctl_fast_mem, 0, 0, cast(void **, &at));
                if (err) {
                        break;
                }

                at->table = appset_address_array[i];
                at->next = NULL;

                if (at->table->data_init_start and at->table->data_start and at->table->data_size) {
                        memcpy(at->table->data_start, at->table->data_init_start, at->table->data_size);
                }

                if (at->table->bss_start and at->table->bss_size) {
                        memset(at->table->bss_start, 0, at->table->bss_size);
                }

                appset->next = at;
                appset = appset->next;
        }

        return err;
}

//==============================================================================
/**
 * @brief Function find program by name and return program descriptor container.
 *
 * @param name         program name
 * @param prog         program container
 *
 * @return One of errno value.
 */
//==============================================================================
static int find_program(const char *name, const _program_entry_t **prog)
{
        static const size_t kworker_stack_depth = __OS_TASK_MIN_STACK_DEPTH__;
        static const size_t kworker_globals_size = 0;

        static const _program_entry_t kworker = {
                .globals_size = &kworker_globals_size,
                .main = _kworker_process,
                .name = "kworker",
                .stack_depth = &kworker_stack_depth,
        };

        if (strncmp(name, "kworker", 128) == 0) {
                *prog = &kworker;
                return ESUCC;
        }

        for (appset_table_t *appset = &appset_base; appset; appset = appset->next) {

                if ((appset->table->magic == 0x32565341) and (appset->table->number_of_programs > 0) and
                        (appset->table->number_of_programs != UINT32_MAX)) {

                        for (size_t i = 0; i < appset->table->number_of_programs; i++) {
                                if (strncmp(appset->table->program_entry[i].name, name, 128) == 0) {
                                        *prog = &appset->table->program_entry[i];
                                        return ESUCC;
                                }
                        }
                }
        }

        return ENOENT;
}

//==============================================================================
/**
 * @brief  Function allocate global variables for selected process.
 *
 * @param  proc                 process
 * @param  app_ctx_size         size of application context
 * @param  usrprog              program descriptor
 *
 * @return One of errno value.
 */
//==============================================================================
static int allocate_process_globals(_process_t *proc, size_t app_ctx_size, const _program_entry_t *usrprog)
{
        int err = ESUCC;

        const char **pref_ram = _cpuctl_preferred_APP_RAM;
        if (proc->pdata->pref_ram and *proc->pdata->pref_ram) {
                pref_ram = *proc->pdata->pref_ram;
        }

        if (*usrprog->globals_size > 0) {
                res_header_t *mem;
                err = _kzalloc(_MM_PROG, *usrprog->globals_size, pref_ram, _MM_FLAG__DMA, _MM_FLAG__DMA,
                        cast(void *, &mem));
                if (!err) {
                        proc->globals = &mem[1];
                        _process_register_resource(proc, mem);
                }
        }

        if (!err and (app_ctx_size > 0)) {
                res_header_t *ctx;
                err = _kzalloc(_MM_PROG, app_ctx_size, pref_ram, _MM_FLAG__DMA, _MM_FLAG__DMA, cast(void *, &ctx));
                if (!err) {
                        proc->app_ctx = &ctx[1];
                        _process_register_resource(proc, ctx);
                }
        }

        return err;
}

//==============================================================================
/**
 * @brief  Function copy task context to standard variables (stdin, stdout, stderr,
 *         global, errno). Function is called when this task is already switched.
 *         See FreeRTOSConfig.h file.
 *
 * @param  task         current task
 * @param  proc         process container
 * @param  task_data    task data
 */
//==============================================================================
void _task_switched_in(void *task, void *proc, void *task_data)
{
        UNUSED_ARG1(task);

        u8_t cpuid = _cpuctl_get_cpu_id();

        CPU_load[cpuid].CPU_total_time_last = CPU_load[cpuid].CPU_total_time;
        active_process[cpuid] = proc;

        if (active_process[cpuid] && (active_process[cpuid]->header.type == RES_TYPE_PROCESS)) {
                active_process[cpuid]->curr_task = cast(task_data_t *, task_data)->id;
                _errno[cpuid] = cast(task_data_t *, task_data)->errnov;
                _global[cpuid] = active_process[cpuid]->globals;
                _app_ctx[cpuid] = active_process[cpuid]->app_ctx;

        } else {
                _global[cpuid] = NULL;
                _app_ctx[cpuid] = NULL;
                _errno[cpuid] = 0;
        }
}

//==============================================================================
/**
 * @brief  Function copy standard variables (stdin, stdout, stderr, global, errno)
 *         to task context. Function is called before this task context to be
 *         switched.
 *         See FreeRTOSConfig.h file.
 *
 * @param  task         current task
 * @param  proc         process container
 * @param  task_data    task data
 */
//==============================================================================
void _task_switched_out(void *task, void *proc, void *task_data)
{
        UNUSED_ARG2(task, proc);

        u8_t cpuid = _cpuctl_get_cpu_id();

        CPU_load[cpuid].CPU_total_time += _cpuctl_get_CPU_load_counter_delta();

        if (active_process[cpuid] && (active_process[cpuid]->header.type == RES_TYPE_PROCESS)) {
                cast(task_data_t *, task_data)->errnov = _errno[cpuid];
                active_process[cpuid]->globals = _global[cpuid];
                active_process[cpuid]->app_ctx = _app_ctx[cpuid];

#if (__OS_MONITOR_CPU_LOAD__ > 0)
                u32_t dt = 0;
                if (CPU_load[cpuid].CPU_total_time >= CPU_load[cpuid].CPU_total_time_last) {
                        dt = CPU_load[cpuid].CPU_total_time - CPU_load[cpuid].CPU_total_time_last;
                } else {
                        dt = CPU_load[cpuid].CPU_total_time;
                }

                active_process[cpuid]->taskdata[active_process[cpuid]->curr_task].timecnt[cpuid] += dt;
#endif
        }
}

/*==============================================================================
  End of file
==============================================================================*/
