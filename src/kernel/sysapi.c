/*==============================================================================
@file    sysapi.c

@author  Daniel Zorychta

@brief   System functions.

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
#include "kernel/sysfunc.h"
#include "kernel/khooks.h"

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
 * @brief Function open selected file
 *
 * @param[in]  *name             file path
 * @param[in]   flags            flags
 * @param[in]   mode             mode (O_CREAT)
 * @param[out] **file            pointer to file pointer
 *
 * @return One of errno values
 */
//==============================================================================
int sys_fopen(const char *name, int flags, mode_t mode, kfile_t **file)
{
        struct vfs_path cpath;
        cpath.CWD = NULL;
        cpath.PATH = name;

        int r = _vfs_fopen(&cpath, flags, mode, file);
        if (r == ESUCC) {
                _process_register_resource(_kworker_proc, cast(res_header_t *, *file));
        }
        return r;
}

//==============================================================================
/**
 * @brief Function close opened file
 *
 * @param[in] file              pinter to file
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
int sys_fclose(kfile_t *file)
{
        return _process_release_resource(_kworker_proc, cast(res_header_t *, file), RES_TYPE_FILE);
}

//==============================================================================
/**
 * @brief Function writes data to stream.
 *
 * The function writes <i>size</i> bytes long buffer, to the stream pointed to
 * by <i>file</i>, obtaining them from the location given by <i>ptr</i>.
 * Number of wrote bytes is passed by <i>wrcnt</i> argument.
 *
 * @note Function can be used only by file system or driver code.
 *
 * @param ptr           data buffer
 * @param size          buffer size
 * @param wrcnt         number of wrote bytes
 * @param file          stream
 *
 * @return One of @ref errno value.
 */
//==============================================================================
int sys_fwrite(const void *ptr, size_t size, size_t *wrcnt, kfile_t *file)
{
        return _vfs_fwrite(ptr, size, wrcnt, file);
}

//==============================================================================
/**
 * @brief Function reads data to stream.
 *
 * The function reads <i>size</i> bytes long buffer, from the stream pointed to
 * by <i>file</i>, storing them at the location given by <i>ptr</i>.
 * Number of read bytes is passed by <i>rdcnt</i> argument.
 *
 * @note Function can be used only by file system or driver code.
 *
 * @param ptr           data buffer
 * @param size          buffer size
 * @param rdcnt         number of read bytes
 * @param file          stream
 *
 * @return One of @ref errno value.
 */
//==============================================================================
int sys_fread(void *ptr, size_t size, size_t *rdcnt, kfile_t *file)
{
        return _vfs_fread(ptr, size, rdcnt, file);
}

//==============================================================================
/**
 * @brief Function create binary semaphore
 *
 * @param[in]  cnt_max          max count value (1 for binary)
 * @param[in]  cnt_init         initial value (0 or 1 for binary)
 * @param[out] sem              created semaphore handle
 *
 * @return One of errno values.
 */
//==============================================================================
int sys_semaphore_create(const size_t cnt_max, const size_t cnt_init, ksem_t **sem)
{
        int r = _semaphore_create(cnt_max, cnt_init, sem);
        if (r == ESUCC) {
                _process_register_resource(_kworker_proc, cast(res_header_t *, *sem));
        }
        return r;
}

//==============================================================================
/**
 * @brief Function delete semaphore
 *
 * @param[in] *sem      semaphore object
 */
//==============================================================================
int sys_semaphore_destroy(ksem_t *sem)
{
        return _process_release_resource(_kworker_proc, cast(res_header_t *, sem), RES_TYPE_SEMAPHORE);
}

//==============================================================================
/**
 * @brief Function creates flags. Flag is similar to semaphore except single
 *        object contains more than one synchronization point.
 *
 * @note Function can be used only by file system or driver code.
 *
 * @param flag            created flag handle
 *
 * @return One of @ref errno value.
 */
//==============================================================================
int sys_flag_create(kflag_t **flag)
{
        int r = _flag_create(flag);
        if (r == ESUCC) {
                _process_register_resource(_kworker_proc, cast(res_header_t *, *flag));
        }
        return r;
}

//==============================================================================
/**
 * @brief Function deletes flag.
 *
 * @note Function can be used only by file system or driver code.
 *
 * @param flag      flag object
 *
 * @return One of @ref errno value.
 */
//==============================================================================
int sys_flag_destroy(kflag_t *flag)
{
        return _process_release_resource(_kworker_proc, cast(res_header_t *, flag), RES_TYPE_FLAG);
}

//==============================================================================
/**
 * @brief Function create new mutex
 *
 * @param[in]  type     mutex type
 * @param[out] mtx      created mutex handle
 *
 * @return One of errno values.
 */
//==============================================================================
int sys_mutex_create(enum kmtx_type type, kmtx_t **mtx)
{
        int r = _mutex_create(type, mtx);
        if (r == ESUCC) {
                _process_register_resource(_kworker_proc, cast(res_header_t *, *mtx));
        }
        return r;
}

//==============================================================================
/**
 * @brief Function destroy mutex
 *
 * @param[in] *mutex    mutex object
 *
 * @return One of errno values.
 */
//==============================================================================
int sys_mutex_destroy(kmtx_t *mutex)
{
        return _process_release_resource(_kworker_proc, cast(res_header_t *, mutex), RES_TYPE_MUTEX);
}

//==============================================================================
/**
 * @brief Function create new queue
 *
 * @param[in]  length           queue length
 * @param[in]  item_size        queue item size
 * @param[out] queue            created queue
 *
 * @return One of errno values.
 */
//==============================================================================
int sys_queue_create(size_t length, size_t item_size, kqueue_t **queue)
{
        int r = _queue_create(length, item_size, queue);
        if (r == ESUCC) {
                _process_register_resource(_kworker_proc, cast(res_header_t *, *queue));
        }
        return r;
}

//==============================================================================
/**
 * @brief Function delete queue
 *
 * @param[in] *queue            queue object
 *
 * @return One of errno values.
 */
//==============================================================================
int sys_queue_destroy(kqueue_t *queue)
{
        return _process_release_resource(_kworker_proc, cast(res_header_t *, queue), RES_TYPE_QUEUE);
}

//==============================================================================
/**
 * @brief Function create new task and if enabled add to monitor list
 *
 * Function by default allocate memory for task data (localized in task tag)
 * which is used to cpu load calculation and standard IO and etc.
 *
 * @param[in ] func             thread code
 * @param[in ] attr             thread attribute (NULL for default)
 * @param[in ] arg              argument pointer (can be NULL)
 * @param[in ] thread           thread parameters (task and ID) (can be NULL)
 * @param[out] task             task handle
 *
 * @return On of errno value.
 */
//==============================================================================
int sys_thread_create(thread_func_t func, const _thread_attr_t *attr, void *arg, tid_t *tid)
{
        return _process_thread_create(_kworker_proc, func, attr, arg, tid);
}

//==============================================================================
/**
 * @brief Function delete task
 * Function remove task from monitoring list, and next delete the task from OS
 * list. Function resume the parent task before delete.
 *
 * @param *taskHdl       task handle
 */
//==============================================================================
int sys_thread_destroy(tid_t tid)
{
        return _process_thread_kill(_kworker_proc, tid);
}

//==============================================================================
/**
 * @brief Function sets file position indicator.
 *
 * The function sets the file position indicator for the stream
 * pointed to by <i>file</i>. The new position, measured in bytes, is obtained
 * by adding offset bytes to the position specified by whence. If whence is set
 * to @ref SEEK_SET, @ref SEEK_CUR, or @ref SEEK_END, the offset is
 * relative to the start of the file, the current position indicator, or
 * end-of-file, respectively. A successful call to the sys_fseek() function
 * clears the end-of-file indicator for the stream.
 *
 * @param file          stream
 * @param offset        offset
 * @param mode          seek mode
 *
 * @return One of @ref errno value.
 */
//==============================================================================
int sys_fseek(kfile_t *file, i64_t offset, int mode)
{
        return _vfs_fseek(file, offset, mode);
}

//==============================================================================
/**
 * @brief Function sends request to selected file to do non-standard operation.
 *
 * The function manipulates the file parameters. In particular, many
 * operating characteristics of character special files (e.g., drivers) may
 * be controlled with ioctl() requests.
 *
 * The second argument is a device-dependent request code. The third
 * argument is an untyped pointer to memory.
 *
 * @param file          stream to control
 * @param rq            request number (each driver has own requests)
 * @param ...           untyped pointer to memory (optional in some requests)
 *
 * @return One of @ref errno value.
 *

 *
 * @note
 * The names of all requests are constructed in the same way: @b
 IOCTL_<MODULE_NAME>__<REQUEST_NAME>.
 */
//==============================================================================
int sys_ioctl(kfile_t *file, int rq, ...)
{
        va_list arg;
        va_start(arg, rq);
        int err = _vfs_vfioctl(file, rq, arg);
        va_end(arg);
        return err;
}

//==============================================================================
/**
 * @brief Function gets file information.
 *
 * The function return information about a file pointed by <i>file</i>.
 *
 * @param file          file object
 * @param buf           file's information
 *
 * @return One of @ref errno value.
 */
//==============================================================================
int sys_fstat(kfile_t *file, struct stat *buf)
{
        return _vfs_fstat(file, buf);
}

//==============================================================================
/**
 * @brief Function forces write buffers to stream.
 *
 * For output streams, sys_fflush() forces a write of all buffered data for
 * the given output or update stream via the stream's underlying write function.
 * For input streams, sys_fflush() discards any buffered data that has been
 * fetched from the underlying file. The open status of the stream is unaffected.
 *
 * @param file          stream
 *
 * @return One of @ref errno value.
 */
//==============================================================================
int sys_fflush(kfile_t *file)
{
        return _vfs_fflush(file);
}

//==============================================================================
/**
 * @brief Function prints message according to format to buffer.
 *
 * The function produce output according to a <i>format</i> as described below.
 * The function write output pointed to by <i>bfr</i> of size <i>size</i>.<p>
 *
 * The format string follows POSIX (C99) rules. A conversion specification is:
 * <b>%[flags][width][.precision][length]conversion</b>@n
 *   flags:      <b>-</b> left-justify, <b>+</b> always print sign, <b>space</b>
 *               prefix positive value with space, <b>#</b> alternative form,
 *               <b>0</b> pad with zeros@n
 *   width:      decimal number or <b>*</b> (taken from argument, negative value
 *               means <b>-</b> flag)@n
 *   precision:  <b>.</b> followed by decimal number or <b>*</b>@n
 *   length:     hh, h, l, ll, j, z, t, L@n
 *   conversion: d, i, o, u, x, X, f, F, e, E, g, G, c, s, p, %@n
 *
 *   @code printf("[%-*s|%+5d|%08.3f|%#x|%.2e]", 6, "ab", 42, 3.14159, 255, 1234.5);
 *   //=> [ab    |  +42|0003.142|0xff|1.23e+03] @endcode
 *
 * Not supported (to keep kernel small): %a, %A, %ls, %n and positional
 * arguments (%n$). Such conversion specification is printed as is and its
 * argument is skipped. %lc is printed as %c. Floating point values from about
 * 1e-6 to 1e38 are correctly rounded up to 17 significant digits, other values
 * up to about 15 digits.
 *
 * @param bfr           buffer where output was produced
 * @param size          buffer size
 * @param format        formatting string
 * @param ...           argument sequence
 *
 * @return Number of characters written to the buffer, excluding the terminating
 * null byte. The output is truncated if it does not fit to the buffer, so the
 * value is always in range 0..size-1 (0 if size is 0). This is different than
 * POSIX snprintf(), the function works as scnprintf() in Linux kernel.
 */
//==============================================================================
int sys_strfmt(char *bfr, size_t size, const char *format, ...)
{
        va_list arg;
        va_start(arg, format);
        int r = _vscnprintf(bfr, size, format, arg);
        va_end(arg);
        return r;
}

//==============================================================================
/**
 * @brief Function scans input according to format.
 *
 * The function scans input according to format as described below. This format
 * may contain conversion specifications; the results from such conversions,
 * if any, are stored in the locations pointed to by the pointer arguments that
 * follow format. Each pointer argument must be of a type that is appropriate
 * for the value returned by the corresponding conversion specification.<p>
 *
 * <b>%</b> - The flag starts interpreting of formatting. After character can be type a
 * numbers which determine e.g. buffer length, number of digits, etc.<p>
 *
 * <b>c</b> - Scans single character.<p>
 *
 * <b>s</b> - Scans string.<p>
 *
 * <b>d, i</b> - Scans signed <i>int</i> type number.<p>
 *
 * <b>u</b> - Scans unsigned type number.<p>
 *
 * <b>x, X</b> - Scans value in HEX formatting.<p>
 *
 * <b>o</b> - Scans value in Octal formatting.<p>
 *
 * <b>f, F, g, G</b> - Scans float value.
 *
 * @param str           input string (must be <i>null</i> terminated)
 * @param format        formatting string
 * @param ...           argument sequence list
 *
 * @return The function return the number of input items successfully matched
 * and assigned, which can be fewer than provided for, or even zero in the event
 * of an early matching failure.<p>
 *
 * The value @ref EOF is returned if the end of input is reached before either
 * the first successful conversion or a matching failure occurs. @ref EOF is
 * also returned if a read error occurs, in which case the error indicator for
 * the stream is set.
 */
//==============================================================================
int sys_sscanf(const char *str, const char *format, ...)
{
        va_list arg;
        va_start(arg, format);
        int n = _vsscanf(str, format, arg);
        va_end(arg);
        return n;
}

//==============================================================================
/**
 * @brief Function wait for semaphore.
 *
 * The function waits for semaphore signal pointed by
 * <i>sem</i> by <i>timeout</i> milliseconds. If semaphore was signaled then
 * ESUCC is returned, otherwise (timeout) ETIME. When <i>timeout</i>
 * value is set to 0 then semaphore is polling without timeout.
 *
 * @param sem           semaphore object pointer
 * @param timeout       timeout value in milliseconds
 *
 * @return One of @ref errno value.
 */
//==============================================================================
int sys_semaphore_wait(ksem_t *sem, const u32_t timeout)
{
        return _semaphore_wait(sem, timeout);
}

//==============================================================================
/**
 * @brief Function signal semaphore.
 *
 * The function signals semaphore pointed by <i>sem</i>.
 *
 * @param sem           semaphore object pointer
 *
 * @return One of @ref errno value.
 */
//==============================================================================
int sys_semaphore_signal(ksem_t *sem)
{
        return _semaphore_signal(sem);
}

//==============================================================================
/**
 * @brief Function wait for semaphore from ISR.
 *
 * @param sem              semaphore object
 * @param task_woken       true if higher priority task woken, otherwise false (can be
 @ref NULL)
 *
 * @return One of @ref errno value.
 */
//==============================================================================
int sys_semaphore_wait_from_ISR(ksem_t *sem, bool *task_woken)
{
        return _semaphore_wait_from_ISR(sem, task_woken);
}

//==============================================================================
/**
 * @brief Function signal semaphore from ISR.
 *
 * @param sem              semaphore object
 * @param task_woken       true if higher priority task woken, otherwise false (can be
 @ref NULL)
 *
 * @return One of @ref errno value.
 */
//==============================================================================
int sys_semaphore_signal_from_ISR(ksem_t *sem, bool *task_woken)
{
        return _semaphore_signal_from_ISR(sem, task_woken);
}

//==============================================================================
/**
 * @brief Function wait for selected flags.
 *
 * @param flag          flag object
 * @param bits          bits to wait for (logical AND)
 * @param blocktime_ms  timeout in milliseconds
 *
 * @return One of @ref errno value.
 */
//==============================================================================
int sys_flag_wait(kflag_t *flag, u32_t bits, const u32_t blocktime_ms)
{
        return _flag_wait(flag, bits, blocktime_ms);
}

//==============================================================================
/**
 * @brief Function set selected flags.
 *
 * @param flag          flag object
 * @param bits          bits to set
 *
 * @return One of @ref errno value.
 */
//==============================================================================
int sys_flag_set(kflag_t *flag, u32_t bits)
{
        return _flag_set(flag, bits);
}

//==============================================================================
/**
 * @brief Function clear selected flags.
 *
 * @param flag          flag object
 * @param bits          bits to clear
 *
 * @return One of @ref errno value.
 */
//==============================================================================
int sys_flag_clear(kflag_t *flag, u32_t bits)
{
        return _flag_clear(flag, bits);
}

//==============================================================================
/**
 * @brief Function lock mutex.
 *
 * @param mutex             mutex object
 * @param timeout           polling time
 *
 * @return One of @ref errno value.
 */
//==============================================================================
int sys_mutex_lock(kmtx_t *mutex, const u32_t timeout)
{
        return _mutex_lock(mutex, timeout);
}

//==============================================================================
/**
 * @brief Function try lock mutex.
 *
 * The function try to lock mutex. If mutex is free then is immediately locked,
 * if not then error is returned. Function is equivalent to:
 * sys_mutex_lock(mtx, 0) call.
 *
 * @param mutex             mutex object
 *
 * @return One of @ref errno value.
 */
//==============================================================================
int sys_mutex_trylock(kmtx_t *mutex)
{
        return _mutex_lock(mutex, 0);
}

//==============================================================================
/**
 * @brief Function unlock mutex.
 *
 * @param *mutex            mutex object
 *
 * @return One of @ref errno value.
 */
//==============================================================================
int sys_mutex_unlock(kmtx_t *mutex)
{
        return _mutex_unlock(mutex);
}

//==============================================================================
/**
 * @brief Function reset queue.
 *
 * @param *queue            queue object
 *
 * @return One of @ref errno value.
 */
//==============================================================================
int sys_queue_reset(kqueue_t *queue)
{
        return _queue_reset(queue);
}

//==============================================================================
/**
 * @brief Function send queue.
 *
 * @param queue            queue object
 * @param item             item
 * @param waittime_ms      wait time
 *
 * @return One of @ref errno value.
 */
//==============================================================================
int sys_queue_send(kqueue_t *queue, const void *item, const u32_t waittime_ms)
{
        return _queue_send(queue, item, waittime_ms);
}

//==============================================================================
/**
 * @brief Function send queue.
 *
 * @param *queue            queue object
 * @param *item             item
 * @param *task_woken       true if higher priority task woken, otherwise false (can be
 * @ref NULL)
 *
 * @return One of @ref errno value.
 */
//==============================================================================
int sys_queue_send_from_ISR(kqueue_t *queue, const void *item, bool *task_woken)
{
        return _queue_send_from_ISR(queue, item, task_woken);
}

//==============================================================================
/**
 * @brief Function send queue.
 *
 * @param queue            queue object
 * @param item             item
 * @param waittime_ms      wait time
 *
 * @return One of @ref errno value.
 */
//==============================================================================
int sys_queue_receive(kqueue_t *queue, void *item, const u32_t waittime_ms)
{
        return _queue_receive(queue, item, waittime_ms);
}

//==============================================================================
/**
 * @brief Function receive queue from ISR.
 *
 * @param queue            queue object
 * @param item             item
 * @param task_woken       true if higher priority task woke, otherwise false (can be @ref
 * NULL)
 *
 * @return One of @ref errno value.
 */
//==============================================================================
int sys_queue_receive_from_ISR(kqueue_t *queue, void *item, bool *task_woken)
{
        return _queue_receive_from_ISR(queue, item, task_woken);
}

//==============================================================================
/**
 * @brief Function gets number of items in queue.
 *
 * @param queue            queue object
 * @param items            number of items in queue
 *
 * @return One of @ref errno value.
 */
//==============================================================================
int sys_queue_get_number_of_items(kqueue_t *queue, size_t *items)
{
        return _queue_get_number_of_items(queue, items);
}

//==============================================================================
/**
 * @brief Function gets number of items in queue from ISR.
 *
 * @param queue            queue object
 * @param items            number of items in queue
 *
 * @return One of @ref errno value.
 */
//==============================================================================
int sys_queue_get_number_of_items_from_ISR(kqueue_t *queue, size_t *items)
{
        return _queue_get_number_of_items_from_ISR(queue, items);
}

//==============================================================================
/**
 * @brief Function return OS time in milliseconds.
 *
 * @return OS time in milliseconds.
 */
//==============================================================================
clock_t sys_get_uptime_ms(void)
{
        return _kernel_get_uptime_ms();
}

//==============================================================================
/**
 * @brief Function yield thread.
 */
//==============================================================================
void sys_thread_yield(void)
{
        _task_yield();
}

//==============================================================================
/**
 * @brief Function yield thread from ISR.
 *
 * @param yield         thread yield
 */
//==============================================================================
void sys_thread_yield_from_ISR(bool yield)
{
        _task_yield_from_ISR(yield);
}

//==============================================================================
/**
 * @brief Function enter to critical section.
 */
//==============================================================================
void sys_critical_section_begin(void)
{
        _critical_section_begin();
}

//==============================================================================
/**
 * @brief Function exit from critical section.
 */
//==============================================================================
void sys_critical_section_end(void)
{
        _critical_section_end();
}

//==============================================================================
/**
 * @brief Function put to sleep thread for microseconds.
 *
 * @note Function can sleep longer that declared because of context switch
 *       settings.
 *
 * @note Function work in critical section.
 *
 * @param microseconds          number of microseconds of sleep
 */
//==============================================================================
void sys_sleep_us(const u32_t microseconds)
{
        _cpuctl_delay_us(microseconds);
}

//==============================================================================
/**
 * @brief Function put to sleep thread for milliseconds.
 *
 * @note Function can sleep longer that declared because of context switch
 *       settings. Context switch can be longer than 1ms.
 *
 * @param milliseconds          number of milliseconds of sleep
 */
//==============================================================================
void sys_sleep_ms(const u32_t milliseconds)
{
        _sleep_ms(milliseconds);
}

//==============================================================================
/**
 * @brief  Convert tm structure to time_t.
 *
 * This function performs the reverse translation that localtime does.
 * The values of the members tm_wday and tm_yday of timeptr are ignored, and
 * the values of the other members are interpreted even if out of their valid
 * ranges (see struct tm). For example, tm_mday may contain values above 31,
 * which are interpreted accordingly as the days that follow the last day of
 * the selected month.
 * A call to this function automatically adjusts the values of the members of
 * timeptr if they are off-range or -in the case of tm_wday and tm_yday- if they
 * have values that do not match the date described by the other members.
 *
 * @param  timeptr      Pointer to a tm structure that contains a calendar time
 *                      broken down into its components (see struct tm)
 *
 * @return A time_t value corresponding to the calendar time passed as argument.
 *         If the calendar time cannot be represented, a value of -1 is returned.
 */
//==============================================================================
time_t sys_mktime(struct tm *timeptr)
{
#if __OS_ENABLE_TIMEMAN__ == _YES_
        return _mktime(timeptr);
#else
        UNUSED_ARG1(timeptr);
        return _kernel_get_uptime_ms() / 1000;
#endif
}

//==============================================================================
/**
 * @brief  Convert time_t to tm as UTC time.
 *
 * Uses the value pointed by timer to fill a tm structure with the values that
 * represent the corresponding time, expressed as a UTC time (i.e., the time
 * at the GMT timezone).
 *
 * @param timer    Pointer to an object of type time_t that contains a time value.
 *                      time_t is an alias of a fundamental arithmetic type
 *                      capable of representing times as returned by function time.
 *
 * @param tm       Pointer to an object of type struct tm that will contains
 *                      converted timer value to time structure.
 *
 * @return A pointer to a tm structure with its members filled with the values
 *         that correspond to the UTC time representation of timer.
 */
//==============================================================================
struct tm *sys_gmtime_r(const time_t *timer, struct tm *tm)
{
#if __OS_ENABLE_TIMEMAN__ == _YES_
        return _gmtime_r(timer, tm);
#else
        UNUSED_ARG1(timer);
        memset(tm, 0, sizeof(struct tm));
        return tm;
#endif
}

//==============================================================================
/**
 * @brief  Function return integer value from given configuration.
 *
 * Function found expression: VAR=VAL. If variable is not found then <i>defval</i>
 * is returned.
 *
 * @param opts          options to analyze
 * @param var           variable to search
 * @param defval        value used if variable does not found
 *
 * @return Integer (on error or when variable does not found the default value).
 */
//==============================================================================
int sys_opt_get_int(const char *opts, const char *var, int defval)
{
        return _stropt_get_int(opts, var, defval);
}

//==============================================================================
/**
 * @brief  Function return string from given configuration.
 *
 * Function found expression: VAR=STR.
 *
 * @param opts          options to analyze
 * @param var           variable to search
 * @param len           string length
 *
 * @return On success string pointer and length passed by len pointer, otherwise NULL.
 */
//==============================================================================
const char *sys_opt_get_str_ref(const char *opts, const char *var, size_t *len)
{
        return _stropt_get_string_ref(opts, var, len);
}

//==============================================================================
/**
 * @brief  Function copy string from option.
 *
 * Function found expression: VAR=STR.
 *
 * @param opts          options to analyze
 * @param var           variable to search
 * @param buf           destination buffer
 * @param buflen        destination buffer length
 *
 * @return On success string pointer and length passed by len pointer, otherwise NULL.
 */
//==============================================================================
size_t sys_opt_get_str_copy(const char *opts, const char *var, char *buf, size_t buflen)
{
        return _stropt_get_string_copy(opts, var, buf, buflen);
}

//==============================================================================
/**
 * @brief  Function return integer value from given configuration.
 *
 * Function found expression: VAR=BOOL(false, true, on, off, enable, disable, yes, no).
 * If none of given values is found, then function return <i>defval</i> defined
 * by user.
 *
 * @param opts          options to analyze
 * @param var           variable to search
 * @param defval        default value
 *
 * @return On success return 1 or 0
 */
//==============================================================================
int sys_opt_get_bool(const char *opts, const char *var, int defval)
{
        return _stropt_get_bool(opts, var, defval);
}

//==============================================================================
/**
 * @brief  Function check if flag exist.
 *
 * @param opts          options to analyze
 * @param flag          flag to find
 *
 * @return On success return true, otherwise false.
 */
//==============================================================================
bool sys_opt_has_flag(const char *opts, const char *flag)
{
        return _stropt_is_flag(opts, flag);
}

//==============================================================================
/**
 * @brief  Function return enumeration value from given configuration.
 *
 * Function found expression: VAR=NAME or VAR=NUMBER, e.g. "parity=even".
 *
 * @param opts          options to analyze
 * @param var           variable to search
 * @param names         names of enumeration values (index is value)
 * @param count         number of names
 * @param defval        value used if variable does not found
 *
 * @return Enumeration value, <i>defval</i> if variable does not exist, -1 if
 *         value is not correct.
 */
//==============================================================================
int sys_opt_get_enum(const char *opts, const char *var, const char *const names[], size_t count, int defval)
{
        return _stropt_get_enum(opts, var, names, count, defval);
}

//==============================================================================
/**
 * @brief Function returns a pseudo-random integer.
 *
 * The rand() function returns a pseudo-random integer in the range 0 to
 * @ref RAND_MAX inclusive (i.e., the mathematical range [0, RAND_MAX]).
 *
 * @param  seed
 *
 * @return The rand() function return a value between @b 0 and @ref RAND_MAX (inclusive).
 */
//==============================================================================
u32_t sys_rand_r(unsigned int *seed)
{
        extern int rand_r(unsigned int *seed);
        return rand_r(seed);
}

//==============================================================================
/**
 * @brief Function locks the scheduler (context switch).
 */
//==============================================================================
void sys_scheduler_lock(void)
{
        _kernel_scheduler_lock();
}

//==============================================================================
/**
 * @brief Function unlocks the scheduler (context switch).
 */
//==============================================================================
void sys_scheduler_unlock(void)
{
        _kernel_scheduler_unlock();
}

//==============================================================================
/**
 * @brief Function locks spinlock (scheduler is locked).
 *
 * @param spinlock      spinlock object
 */
//==============================================================================
void sys_spinlock_lock(kspinlock_t *spinlock)
{
#if _CPUCTL_CORE_NUM > 1
        /* critical section of SMP port uses hardware lock shared by cores */
        _critical_section_begin();
#else
        _kernel_scheduler_lock();
#endif
        sys_assert_msg(spinlock->locked == 0, "spinlock locked recursively");
        spinlock->locked = 1;
}

//==============================================================================
/**
 * @brief Function unlocks spinlock.
 *
 * @param spinlock      spinlock object
 */
//==============================================================================
void sys_spinlock_unlock(kspinlock_t *spinlock)
{
        spinlock->locked = 0;
#if _CPUCTL_CORE_NUM > 1
        _critical_section_end();
#else
        _kernel_scheduler_unlock();
#endif
}

//==============================================================================
/**
 * @brief Function locks spinlock and masks interrupts.
 *
 * @param spinlock      spinlock object
 */
//==============================================================================
void sys_spinlock_lock_irq(kspinlock_t *spinlock)
{
        _critical_section_begin();
        sys_assert_msg(spinlock->locked == 0, "spinlock locked recursively");
        spinlock->locked = 1;
}

//==============================================================================
/**
 * @brief Function unlocks spinlock and unmasks interrupts.
 *
 * @param spinlock      spinlock object
 */
//==============================================================================
void sys_spinlock_unlock_irq(kspinlock_t *spinlock)
{
        spinlock->locked = 0;
        _critical_section_end();
}

#if (_CPUCTL_CACHE_ALIGN > 1)
//==============================================================================
/**
 * @brief Function cleans data cache.
 *
 * @param addr      memory address (NULL: whole data cache)
 * @param size      memory size in bytes
 */
//==============================================================================
void sys_dcache_clean(void *addr, size_t size)
{
        UNUSED_ARG2(addr, size); // arguments are not used by CPU without cache

        if (addr) {
                _cpuctl_clean_dcache_by_addr(addr, size);
        } else {
                _cpuctl_clean_dcache();
        }
}

//==============================================================================
/**
 * @brief Function invalidates data cache.
 *
 * @param addr      memory address
 * @param size      memory size in bytes
 */
//==============================================================================
void sys_dcache_invalidate(void *addr, size_t size)
{
        UNUSED_ARG2(addr, size); // arguments are not used by CPU without cache

        if (addr) {
                _cpuctl_invalidate_dcache_by_addr(addr, size);
        }
}

//==============================================================================
/**
 * @brief Function cleans and invalidates data cache.
 *
 * @param addr      memory address (NULL: whole data cache)
 * @param size      memory size in bytes
 */
//==============================================================================
void sys_dcache_flush(void *addr, size_t size)
{
        UNUSED_ARG2(addr, size); // arguments are not used by CPU without cache

        if (addr) {
                _cpuctl_clean_invalidate_dcache_by_addr(addr, size);
        } else {
                _cpuctl_clean_invalidate_dcache();
        }
}
//==============================================================================
/**
 * @brief Function invalidates instruction cache (whole cache).
 *
 * @param addr      memory address (not used, whole cache is invalidated)
 * @param size      memory size in bytes (not used)
 */
//==============================================================================
void sys_icache_invalidate(void *addr, size_t size)
{
        UNUSED_ARG2(addr, size);

        _cpuctl_invalidate_icache();
}

//==============================================================================
/**
 * @brief Function synchronizes caches after code was written to memory.
 *
 * @param addr      code address (NULL: whole caches)
 * @param size      code size in bytes
 */
//==============================================================================
void sys_cache_sync_code(void *addr, size_t size)
{
        sys_dcache_clean(addr, size);
        sys_icache_invalidate(addr, size);
}
#endif

#if (__OS_SYSTEM_MSG_ENABLE__ > 0)
//==============================================================================
/**
 * @brief Function put message to kernel log.
 *
 * @param format        formatting string
 * @param ...           argument sequence
 */
//==============================================================================
void printk(const char *format, ...)
{
        va_list args;
        va_start(args, format);
        _vprintk(format, args);
        va_end(args);
}
#endif

#if (__OS_ENABLE_SYS_ASSERT__ > 0)
//==============================================================================
/**
 * @brief Assertion hook.
 *
 * @param assert        assertion result
 * @param msg           message (can be NULL)
 */
//==============================================================================
void sys_assert_hook(bool assert, const char *msg)
{
        _assert_hook(assert, msg);
}
#endif

//==============================================================================
/**
 * @brief Function returns file position indicator.
 *
 * The function obtains the current value of the file position
 * indicator pointed by <i>lseek</i> for the stream pointed to by <i>file</i>.
 *
 * @param file          stream
 * @param lseek         file position indicator
 *
 * @return One of @ref errno value.
 */
//==============================================================================
int sys_ftell(kfile_t *file, i64_t *lseek)
{
        return _vfs_ftell(file, lseek);
}

//==============================================================================
/**
 * @brief Function clears end-of-file and error indicators.
 *
 * The function clears the end-of-file and error indicators
 * for the stream pointed to by <i>file</i>.
 *
 * @param file          stream
 *
 * @return One of @ref errno value.
 */
//==============================================================================
int sys_clearerr(kfile_t *file)
{
        return _vfs_clearerr(file);
}

//==============================================================================
/**
 * @brief Function tests error indicator.
 *
 * The function tests the error indicator for the stream pointed
 * to by <i>file</i>, returning nonzero if it is set.  The error indicator can
 * be reset only by the sys_clearerr() function.
 *
 * @param file          stream
 * @param error         pointer to error indicator
 *
 * @return One of @ref errno value.
 */
//==============================================================================
int sys_ferror(kfile_t *file, int *error)
{
        return _vfs_ferror(file, error);
}

//==============================================================================
/**
 * @brief  Get current time.
 *
 * The function returns this value, and if the argument is not a @ref NULL pointer,
 * it also sets this value to the object pointed by timer.
 * The value returned generally represents the number of seconds since 00:00
 * hours, Jan 1, 1970 UTC (i.e., the current unix timestamp). Although libraries
 * may use a different representation of time: Portable programs should not use
 * the value returned by this function directly, but always rely on calls to
 * other elements of the standard library to translate them to portable types
 * (such as localtime, gmtime or difftime).
 *
 * @param  timer        Pointer to an object of type time_t, where the time
 *                      value is stored.
 *                      Alternatively, this parameter can be a @ref NULL pointer,
 *                      in which case the parameter is not used (the function
 *                      still returns a value of type time_t with the result).
 *
 * @return One of @ref errno value.
 */
//==============================================================================
int sys_gettime(time_t *timer)
{
#if __OS_ENABLE_TIMEMAN__ == _YES_
        struct timeval timeval;
        int err = _gettime(&timeval);
        if (!err) {
                if (timer) {
                        *timer = timeval.tv_sec;
                } else {
                        err = EINVAL;
                }
        }

        return err;
#else
        UNUSED_ARG1(timer);
        return ENOTSUP;
#endif
}

//==============================================================================
/**
 * @brief  Function return free memory in bytes.
 *
 * @return Free memory value.
 */
//==============================================================================
size_t sys_get_free_mem(void)
{
        return _mm_get_mem_free();
}

//==============================================================================
/**
 * @brief  Function return memory size (RAM) in bytes.
 *
 * @return Memory size.
 */
//==============================================================================
size_t sys_get_mem_size(void)
{
        return _mm_get_mem_size();
}

/*==============================================================================
  End of file
==============================================================================*/
