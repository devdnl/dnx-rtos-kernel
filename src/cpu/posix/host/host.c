/*==============================================================================
@file    host.c

@author  Daniel Zorychta

@brief   Host operating system support (POSIX simulator).

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
 * NOTE: This file is compiled with host headers. Do not include dnx RTOS
 *       headers here (except FreeRTOS).
 */

/*==============================================================================
  Include files
==============================================================================*/
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/ucontext.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include "FreeRTOS.h"
#include "task.h"
#include "host.h"

/*==============================================================================
  Local macros
==============================================================================*/
#define DEFAULT_RAM_SIZE_MIB __CPU_RAM_SIZE_MIB__
#define MAX_RAM_SIZE_MIB     1024

/* kernel panic causes (kernel/kpanic.h) */
#define KERNEL_PANIC_CAUSE_SEGFAULT   0
#define KERNEL_PANIC_CAUSE_CPUFAULT_1 2
#define KERNEL_PANIC_CAUSE_CPUFAULT_2 3
#define KERNEL_PANIC_CAUSE_CPUFAULT_3 4

/*==============================================================================
  Local object types
==============================================================================*/
typedef struct {
        const char *path;
        int fd;
        bool read_only;
} disk_t;

/*==============================================================================
  Local function prototypes
==============================================================================*/
static void lock(void);
static void unlock(void);
static void terminal_restore(void);

/* kernel function (kernel/kpanic.h) */
extern void _kernel_panic_report_from_exception(int suggested_cause, uint32_t exc_num);

/*==============================================================================
  Local objects
==============================================================================*/
static struct {
        pthread_t main_thread;
        char **argv;
        size_t ram_size;
        void *ram;
        bool terminal_configured;
        struct termios terminal_attr;
        disk_t disk[_HOST_DISK_COUNT];
        char fault_info[128];
} host;

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
 * @brief  Enter to section where host library is used. Scheduler tick cannot
 *         switch tasks in this section because the host library can use
 *         internal locks.
 */
//==============================================================================
static void lock(void)
{
        if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED) {
                vPortEnterCritical();
        }
}

//==============================================================================
/**
 * @brief  Exit from section where host library is used.
 */
//==============================================================================
static void unlock(void)
{
        if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED) {
                vPortExitCritical();
        }
}

//==============================================================================
/**
 * @brief  Write all bytes to selected file descriptor.
 *
 * @param  fd           file descriptor
 * @param  buf          buffer
 * @param  len          buffer length
 *
 * @return Number of written bytes or -1 on error.
 */
//==============================================================================
static int write_all(int fd, const void *buf, size_t len)
{
        const char *ptr = buf;
        size_t left = len;

        while (left > 0) {
                ssize_t n = write(fd, ptr, left);
                if (n < 0) {
                        if (errno == EINTR) {
                                continue;
                        } else if (errno == EAGAIN) {
                                struct pollfd pfd = {.fd = fd, .events = POLLOUT};
                                poll(&pfd, 1, 10);
                                continue;
                        } else {
                                return -1;
                        }
                }

                ptr += n;
                left -= n;
        }

        return (int)len;
}

//==============================================================================
/**
 * @brief  Restore terminal configuration.
 */
//==============================================================================
static void terminal_restore(void)
{
        if (host.terminal_configured) {
                tcsetattr(STDIN_FILENO, TCSANOW, &host.terminal_attr);
                host.terminal_configured = false;
        }
}

//==============================================================================
/**
 * @brief  Configure terminal to raw mode. Ctrl+\ (SIGQUIT) terminates simulator.
 */
//==============================================================================
static void terminal_configure(void)
{
        if (isatty(STDIN_FILENO) && (tcgetattr(STDIN_FILENO, &host.terminal_attr) == 0)) {

                struct termios attr = host.terminal_attr;

                attr.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON);
                attr.c_oflag &= ~OPOST;
                attr.c_lflag &= ~(ECHO | ECHONL | ICANON | IEXTEN);
                attr.c_lflag |= ISIG;
                attr.c_cflag &= ~(CSIZE | PARENB);
                attr.c_cflag |= CS8;
                attr.c_cc[VINTR] = _POSIX_VDISABLE;
                attr.c_cc[VSUSP] = _POSIX_VDISABLE;
                attr.c_cc[VQUIT] = 0x1C; // Ctrl+'\'
                attr.c_cc[VMIN] = 1;
                attr.c_cc[VTIME] = 0;

                if (tcsetattr(STDIN_FILENO, TCSANOW, &attr) == 0) {
                        host.terminal_configured = true;
                }
        }
}

//==============================================================================
/**
 * @brief  Termination signal handler.
 *
 * @param  sig          signal number
 */
//==============================================================================
static void signal_terminate(int sig)
{
        terminal_restore();

        static const char msg[] = "\r\n*** dnx RTOS simulator terminated ***\r\n";
        if (write(STDERR_FILENO, msg, sizeof(msg) - 1)) {
        }

        _exit(128 + sig);
}

//==============================================================================
/**
 * @brief  Function used as return point of crashed task. Task is killed by
 *         kernel crash handler.
 */
//==============================================================================
static void crashed_task_trap(void)
{
        for (;;) {
                _host_sleep_us(10000);
        }
}

//==============================================================================
/**
 * @brief  Fatal signal handler (segmentation fault, etc.). Fault in the task
 *         is reported to the kernel (as CPU exception on the microcontroller)
 *         and the task is redirected to the trap function. Kernel kills the
 *         crashed process or restarts the system.
 *
 * @param  sig          signal number
 * @param  info         signal information
 * @param  ctx          context
 */
//==============================================================================
static void signal_fatal(int sig, siginfo_t *info, void *ctx)
{
        char msg[160];
        int len;

        bool task_fault = (sig != SIGABRT) && (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED) &&
                !pthread_equal(pthread_self(), host.main_thread);

        if (task_fault) {
                len = snprintf(msg, sizeof(msg), "\r\n*** dnx RTOS simulator: %s at address %p ***\r\n", strsignal(sig),
                        info->si_addr);

                if (write(STDERR_FILENO, msg, len)) {
                }

                int cause;
                switch (sig) {
                case SIGSEGV:
                        cause = KERNEL_PANIC_CAUSE_SEGFAULT;
                        break;
                case SIGBUS:
                        cause = KERNEL_PANIC_CAUSE_CPUFAULT_1;
                        break;
                case SIGILL:
                        cause = KERNEL_PANIC_CAUSE_CPUFAULT_2;
                        break;
                default:
                        cause = KERNEL_PANIC_CAUSE_CPUFAULT_3;
                        break;
                }

                /* return to trap function instead of broken code */
                ucontext_t *uc = ctx;
#if defined(__i386__)
                uintptr_t pc = (uintptr_t)uc->uc_mcontext.gregs[REG_EIP];
                uc->uc_mcontext.gregs[REG_EIP] = (greg_t)(uintptr_t)crashed_task_trap;
#elif defined(__x86_64__)
                uintptr_t pc = (uintptr_t)uc->uc_mcontext.gregs[REG_RIP];
                uc->uc_mcontext.gregs[REG_RIP] = (greg_t)(uintptr_t)crashed_task_trap;
#else
#error Architecture not supported
#endif
                snprintf(host.fault_info, sizeof(host.fault_info), "signal %d (%s), address %p, PC %p", sig,
                        strsignal(sig), info->si_addr, (void *)pc);

                _kernel_panic_report_from_exception(cause, 0);

        } else {
                terminal_restore();

                len = snprintf(msg, sizeof(msg), "\r\n*** dnx RTOS simulator: signal %d (%s) at address %p ***\r\n",
                        sig, strsignal(sig), info->si_addr);

                if (write(STDERR_FILENO, msg, len)) {
                }

                signal(sig, SIG_DFL);
                raise(sig);
        }
}

//==============================================================================
/**
 * @brief  Configure host signals.
 */
//==============================================================================
static void signals_configure(void)
{
        signal(SIGPIPE, SIG_IGN);

        struct sigaction sa;
        memset(&sa, 0, sizeof(sa));
        sigfillset(&sa.sa_mask);
        sa.sa_handler = signal_terminate;
        sigaction(SIGQUIT, &sa, NULL);
        sigaction(SIGTERM, &sa, NULL);
        sigaction(SIGHUP, &sa, NULL);
        sigaction(SIGINT, &sa, NULL);

        memset(&sa, 0, sizeof(sa));
        sigfillset(&sa.sa_mask);
        sa.sa_sigaction = signal_fatal;
        sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
        sigaction(SIGSEGV, &sa, NULL);
        sigaction(SIGBUS, &sa, NULL);
        sigaction(SIGILL, &sa, NULL);
        sigaction(SIGFPE, &sa, NULL);
        sigaction(SIGABRT, &sa, NULL);
}

//==============================================================================
/**
 * @brief  Show program usage.
 *
 * @param  name         program name
 */
//==============================================================================
static void show_usage(const char *name)
{
        printf("Usage: %s [options]\n", name);
        printf("dnx RTOS simulator (POSIX).\n\n");
        printf("Options:\n");
        printf("  -r, --ram <MiB>          size of RAM (default: %u MiB)\n", DEFAULT_RAM_SIZE_MIB);
        printf("      --rom <file>         ROMFS image (the same as --disk 0=<file>:ro)\n");
        printf("  -d, --disk <n>=<file>[:ro]\n");
        printf("                           connect file to the host disk <n> (0-%d),\n", _HOST_DISK_COUNT - 1);
        printf("                           disk is available by the HOSTDISK driver\n");
        printf("  -h, --help               show this help\n\n");
        printf("Press Ctrl+\\ to terminate simulator.\n");
}

//==============================================================================
/**
 * @brief  Parse disk argument: <n>=<file>[:ro]
 *
 * @param  arg          argument
 * @param  force_ro     force read only access
 * @param  disk_no      disk number (if negative then number is read from arg)
 *
 * @return On success true is returned.
 */
//==============================================================================
static bool parse_disk(char *arg, bool force_ro, int disk_no)
{
        char *path = arg;

        if (disk_no < 0) {
                char *eq = strchr(arg, '=');
                if (!eq) {
                        return false;
                }

                *eq = '\0';
                char *end = NULL;
                disk_no = (int)strtol(arg, &end, 10);
                if ((end == arg) || (*end != '\0')) {
                        return false;
                }

                path = eq + 1;
        }

        if ((disk_no < 0) || (disk_no >= _HOST_DISK_COUNT) || (*path == '\0')) {
                return false;
        }

        bool ro = force_ro;
        size_t len = strlen(path);
        if ((len > 3) && (strcmp(&path[len - 3], ":ro") == 0)) {
                path[len - 3] = '\0';
                ro = true;
        }

        host.disk[disk_no].path = path;
        host.disk[disk_no].read_only = ro;

        return true;
}

//==============================================================================
/**
 * @brief  Parse program arguments.
 *
 * @param  argc         argument count
 * @param  argv         arguments
 *
 * @return On success true is returned.
 */
//==============================================================================
static bool parse_args(int argc, char *argv[])
{
        for (int i = 1; i < argc; i++) {
                const char *opt = argv[i];
                char *val = (i + 1 < argc) ? argv[i + 1] : NULL;

                if ((strcmp(opt, "-h") == 0) || (strcmp(opt, "--help") == 0)) {
                        show_usage(argv[0]);
                        exit(EXIT_SUCCESS);

                } else if ((strcmp(opt, "-r") == 0) || (strcmp(opt, "--ram") == 0)) {
                        long mib = val ? strtol(val, NULL, 10) : 0;
                        if ((mib <= 0) || (mib > MAX_RAM_SIZE_MIB)) {
                                fprintf(stderr, "Invalid RAM size: %s\n", val ? val : "");
                                return false;
                        }
                        host.ram_size = (size_t)mib * 1024 * 1024;
                        i++;

                } else if (strcmp(opt, "--rom") == 0) {
                        if (!val || !parse_disk(val, true, 0)) {
                                fprintf(stderr, "Invalid ROM file\n");
                                return false;
                        }
                        i++;

                } else if ((strcmp(opt, "-d") == 0) || (strcmp(opt, "--disk") == 0)) {
                        if (!val || !parse_disk(val, false, -1)) {
                                fprintf(stderr, "Invalid disk argument: %s\n", val ? val : "");
                                return false;
                        }
                        i++;

                } else {
                        fprintf(stderr, "Unknown option: %s\n", opt);
                        return false;
                }
        }

        return true;
}

//==============================================================================
/**
 * @brief  Program entry.
 *
 * @param  argc         argument count
 * @param  argv         arguments
 *
 * @return Program status.
 */
//==============================================================================
int main(int argc, char *argv[])
{
        host.main_thread = pthread_self();
        host.argv = argv;
        host.ram_size = (size_t)DEFAULT_RAM_SIZE_MIB * 1024 * 1024;

        for (int i = 0; i < _HOST_DISK_COUNT; i++) {
                host.disk[i].fd = -1;
        }

        if (!parse_args(argc, argv)) {
                show_usage(argv[0]);
                return EXIT_FAILURE;
        }

        host.ram = aligned_alloc(64, host.ram_size);
        if (!host.ram) {
                perror("RAM");
                return EXIT_FAILURE;
        }

        memset(host.ram, 0, host.ram_size);

        signals_configure();
        terminal_configure();
        atexit(terminal_restore);

        return _dnx_main();
}

//==============================================================================
/**
 * @brief  Terminate simulator.
 *
 * @param  status       exit status
 */
//==============================================================================
void _host_exit(int status)
{
        lock();
        terminal_restore();
        _exit(status);
}

//==============================================================================
/**
 * @brief  Restart simulator (program is started again with the same arguments).
 */
//==============================================================================
void _host_restart(void)
{
        lock();
        terminal_restore();

        for (int i = 0; i < _HOST_DISK_COUNT; i++) {
                if (host.disk[i].fd >= 0) {
                        fsync(host.disk[i].fd);
                }
        }

        /* signals must be unblocked in the new program */
        sigset_t set;
        sigemptyset(&set);
        pthread_sigmask(SIG_SETMASK, &set, NULL);

        execv("/proc/self/exe", host.argv);

        perror("restart");
        _exit(EXIT_FAILURE);
}

//==============================================================================
/**
 * @brief  Print message on the host standard error.
 *
 * @param  str          string
 */
//==============================================================================
void _host_print(const char *str)
{
        lock();
        write_all(STDERR_FILENO, str, strlen(str));
        unlock();
}

//==============================================================================
/**
 * @brief  Return information about the last task fault.
 *
 * @return Fault description.
 */
//==============================================================================
const char *_host_get_fault_info(void)
{
        return host.fault_info[0] ? host.fault_info : "no fault information";
}

//==============================================================================
/**
 * @brief  Return RAM block used by kernel as heap.
 *
 * @param  size         RAM size
 *
 * @return RAM address.
 */
//==============================================================================
void *_host_get_ram(size_t *size)
{
        *size = host.ram_size;
        return host.ram;
}

//==============================================================================
/**
 * @brief  Return monotonic time in microseconds.
 *
 * @return Time in microseconds.
 */
//==============================================================================
uint64_t _host_get_monotonic_us(void)
{
        struct timespec t;
        clock_gettime(CLOCK_MONOTONIC, &t);
        return ((uint64_t)t.tv_sec * 1000000) + ((uint64_t)t.tv_nsec / 1000);
}

//==============================================================================
/**
 * @brief  Return real time (UTC) in microseconds since epoch.
 *
 * @return Time in microseconds.
 */
//==============================================================================
int64_t _host_get_realtime_us(void)
{
        struct timespec t;
        clock_gettime(CLOCK_REALTIME, &t);
        return ((int64_t)t.tv_sec * 1000000) + ((int64_t)t.tv_nsec / 1000);
}

//==============================================================================
/**
 * @brief  Sleep host thread. Function can be interrupted by scheduler tick.
 *
 * @param  us           microseconds
 */
//==============================================================================
void _host_sleep_us(uint32_t us)
{
        struct timespec t = {.tv_sec = us / 1000000, .tv_nsec = (long)(us % 1000000) * 1000};
        nanosleep(&t, NULL);
}

//==============================================================================
/**
 * @brief  Check if selected UART is available.
 *
 * @param  port         UART number
 *
 * @return If UART is available true is returned.
 */
//==============================================================================
bool _host_uart_is_available(int port)
{
        return port == 0;
}

//==============================================================================
/**
 * @brief  Read data from UART (non-blocking).
 *
 * @param  port         UART number
 * @param  buf          buffer
 * @param  len          buffer length
 *
 * @return Number of read bytes, 0 if no data, -1 on error.
 */
//==============================================================================
int _host_uart_read(int port, void *buf, size_t len)
{
        int n = -1;

        if (port == 0) {
                lock();

                struct pollfd pfd = {.fd = STDIN_FILENO, .events = POLLIN};
                int r = poll(&pfd, 1, 0);
                if (r > 0) {
                        if (pfd.revents & POLLIN) {
                                n = (int)read(STDIN_FILENO, buf, len);
                                if ((n < 0) && ((errno == EINTR) || (errno == EAGAIN))) {
                                        n = 0;
                                }
                        } else {
                                /* hang up or error: stdin closed */
                                n = -1;
                        }
                } else if (r == 0) {
                        n = 0;
                } else {
                        n = (errno == EINTR) ? 0 : -1;
                }

                unlock();
        }

        return n;
}

//==============================================================================
/**
 * @brief  Write data to UART.
 *
 * @param  port         UART number
 * @param  buf          buffer
 * @param  len          buffer length
 *
 * @return Number of written bytes, -1 on error.
 */
//==============================================================================
int _host_uart_write(int port, const void *buf, size_t len)
{
        int n = -1;

        if (port == 0) {
                lock();
                n = write_all(STDOUT_FILENO, buf, len);
                unlock();
        }

        return n;
}

//==============================================================================
/**
 * @brief  Open host disk.
 *
 * @param  disk         disk number
 * @param  size         disk size
 * @param  read_only    read only flag
 *
 * @return 0 on success, otherwise host errno value.
 */
//==============================================================================
int _host_disk_open(int disk, uint64_t *size, bool *read_only)
{
        if ((disk < 0) || (disk >= _HOST_DISK_COUNT)) {
                return ENODEV;
        }

        disk_t *d = &host.disk[disk];

        if (!d->path) {
                return ENODEV;
        }

        int err = 0;

        lock();

        if (d->fd < 0) {
                d->fd = open(d->path, d->read_only ? O_RDONLY : O_RDWR);
        }

        if (d->fd >= 0) {
                struct stat st;
                if (fstat(d->fd, &st) == 0) {
                        *size = (uint64_t)st.st_size;
                        *read_only = d->read_only;
                } else {
                        err = errno;
                }
        } else {
                err = errno;
        }

        unlock();

        return err;
}

//==============================================================================
/**
 * @brief  Close host disk.
 *
 * @param  disk         disk number
 */
//==============================================================================
void _host_disk_close(int disk)
{
        if ((disk >= 0) && (disk < _HOST_DISK_COUNT) && (host.disk[disk].fd >= 0)) {
                lock();
                fsync(host.disk[disk].fd);
                close(host.disk[disk].fd);
                host.disk[disk].fd = -1;
                unlock();
        }
}

//==============================================================================
/**
 * @brief  Read data from host disk.
 *
 * @param  disk         disk number
 * @param  offset       offset
 * @param  buf          buffer
 * @param  len          length
 *
 * @return Number of read bytes, -1 on error.
 */
//==============================================================================
int _host_disk_read(int disk, uint64_t offset, void *buf, size_t len)
{
        if ((disk < 0) || (disk >= _HOST_DISK_COUNT) || (host.disk[disk].fd < 0)) {
                return -1;
        }

        size_t done = 0;

        lock();

        while (done < len) {
                ssize_t n = pread(host.disk[disk].fd, (char *)buf + done, len - done, (off_t)(offset + done));
                if (n < 0) {
                        if (errno == EINTR) {
                                continue;
                        }
                        done = (done == 0) ? (size_t)-1 : done;
                        break;
                } else if (n == 0) {
                        break;
                }
                done += (size_t)n;
        }

        unlock();

        return (int)done;
}

//==============================================================================
/**
 * @brief  Write data to host disk.
 *
 * @param  disk         disk number
 * @param  offset       offset
 * @param  buf          buffer
 * @param  len          length
 *
 * @return Number of written bytes, -1 on error.
 */
//==============================================================================
int _host_disk_write(int disk, uint64_t offset, const void *buf, size_t len)
{
        if ((disk < 0) || (disk >= _HOST_DISK_COUNT) || (host.disk[disk].fd < 0) || host.disk[disk].read_only) {
                return -1;
        }

        size_t done = 0;

        lock();

        while (done < len) {
                ssize_t n = pwrite(host.disk[disk].fd, (const char *)buf + done, len - done, (off_t)(offset + done));
                if (n < 0) {
                        if (errno == EINTR) {
                                continue;
                        }
                        done = (done == 0) ? (size_t)-1 : done;
                        break;
                }
                done += (size_t)n;
        }

        unlock();

        return (int)done;
}

//==============================================================================
/**
 * @brief  Synchronize host disk.
 *
 * @param  disk         disk number
 *
 * @return 0 on success, -1 on error.
 */
//==============================================================================
int _host_disk_sync(int disk)
{
        if ((disk < 0) || (disk >= _HOST_DISK_COUNT) || (host.disk[disk].fd < 0)) {
                return -1;
        }

        lock();
        int r = fsync(host.disk[disk].fd);
        unlock();

        return r;
}

/*==============================================================================
  End of file
==============================================================================*/
