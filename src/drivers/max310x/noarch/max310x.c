/*==============================================================================
File    max310x.c

Author  Dawid Wałach

Brief   MAX310x SPI/I2C UART (MAX3107, MAX3108, MAX3109)

        Copyright (C) 2021 Dawid Wałach <dawid.walach@3dgence.com>

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

/*==============================================================================
  Include files
==============================================================================*/
#include "drivers/driver.h"
#include "../max310x_ioctl.h"
#include "max310x_regs.h"
#include "spi/spi_ioctl.h"
#include "gpio/gpio_ddi.h"
#if __ENABLE_IRQ__ > 0
#include "irq/irq_ddi.h"
#endif

/*==============================================================================
  Local macros
==============================================================================*/
#define MUTEX_TIMEOUT      _WAIT_FOREVER_MS
#define PORTS_MAX          2   /* MAX3109 */
#define RESET_TIMEOUT_MS   20  /* UART ready after reset (DIVLSB reset value) */
#define CRYSTAL_TIMEOUT_MS 100 /* crystal oscillator ready (STSInt ClockReady) */
#define POLL_MIN_MS        1   /* minimal period of thread without IRQ */
#define POLL_MAX_MS        50  /* maximal period of thread without IRQ */
#define IRQ_GUARD_MS       100 /* period of thread with IRQ (lost interrupt) */
#define IRQ_LOOPS          8   /* maximal number of services while IRQ pin is active */
#define RX_TRIGGER_LEVEL   64  /* RX FIFO interrupt level [words] */
#define RX_TIMEOUT_CHARS   2   /* RX timeout interrupt (idle line) [characters] */
#define DEFAULT_RX_BUFFER  256
#define DEFAULT_TX_BUFFER  128

/*==============================================================================
  Local object types
==============================================================================*/
typedef struct chip chip_t;

/* UART of chip (device of driver) */
typedef struct {
        chip_t *chip; /* chip (allocated by UART 0) */
        kfile_t *dev; /* interface device file of UART (SPI: file of UART 0) */
        kqueue_t *rxq;
        kqueue_t *txq;
        MAX310X_config_t conf;
        u8_t irqen; /* last value of IRQEn register */
        u8_t open;  /* number of open files */
        bool ready; /* UART configured, served by thread */
        u8_t major;
        u8_t minor;
} port_t;

/* chip: shared by UARTs */
struct chip {
        kmtx_t *mtx; /* access to chip and ports */
        ksem_t *sem; /* thread wake up (IRQ, write) */
        tid_t thread;
        bool thread_run;
        bool irq;
        port_t *port[PORTS_MAX];
        u8_t poll_ms;
        u8_t buf[MAX310X_FIFO_SIZE];
};

/*==============================================================================
  Local function prototypes
==============================================================================*/
static int configure(port_t *hdl, const MAX310X_config_t *cfg);
static int chip_init(port_t *hdl);
static void chip_stop(chip_t *chip);
static int chip_detect(port_t *hdl);
static int port_setup(port_t *hdl);
static int port_buffers(port_t *hdl, const MAX310X_config_t *cfg);
static int set_baud(port_t *hdl);
static int thread(void *arg);
#if __ENABLE_IRQ__ > 0
static bool irq_handler(u8_t line, void *arg);
#endif
static void port_service(port_t *hdl);
static int wait_for_reg(port_t *hdl, u8_t reg, u8_t mask, u8_t value, u32_t timeout_ms);
static int read_reg(port_t *hdl, u8_t reg, u8_t *buf, size_t count);
static int write_reg(port_t *hdl, u8_t reg, const u8_t *buf, size_t count);
static int write_reg_byte(port_t *hdl, u8_t reg, u8_t value);

/*==============================================================================
  Local object
==============================================================================*/
MODULE_NAME(MAX310X);

static const _thread_attr_t THREAD_ATTR = {
        .stack_depth = _STACK_DEPTH_LOW,
        .priority = _PRIORITY_NORMAL,
        .detached = true,
};

/*==============================================================================
  Exported object
==============================================================================*/

/*==============================================================================
  Function definitions
==============================================================================*/

//==============================================================================
/**
 * @brief Initialize device.
 *
 * @param[out]          **device_handle        device allocated memory
 * @param[in ]            major                major device number
 * @param[in ]            minor                minor device number
 * @param[in ]            config               optional module configuration
 *
 * @return One of errno value (errno.h).
 */
//==============================================================================
API_MOD_INIT(MAX310X, void **device_handle, u8_t major, u8_t minor, const void *config)
{
        if (minor >= PORTS_MAX) {
                return ENODEV;
        }

        chip_t *chip = NULL;

        if (minor > 0) {
                /* UART 1 uses chip of UART 0 */
                port_t *port0 = NULL;
                int err = drv_get_instance(major, 0, cast(void **, &port0));
                if (err || !port0) {
                        dev_dbg2(major, minor, "UART 0 is not initialized");
                        return ENODEV;
                }

                chip = port0->chip;
        }

        int err = sys_zalloc(sizeof(port_t), device_handle);
        if (err) {
                return err;
        }

        port_t *hdl = *device_handle;
        hdl->major = major;
        hdl->minor = minor;

        if (minor == 0) {
                err = sys_zalloc(sizeof(chip_t), cast(void **, &chip));

                if (!err) {
                        err = sys_mutex_create(KMTX_TYPE_RECURSIVE, &chip->mtx);
                }

                if (!err) {
                        err = sys_semaphore_create(1, 0, &chip->sem);
                }
        }

        if (!err) {
                hdl->chip = chip;

                err = sys_mutex_lock(chip->mtx, MUTEX_TIMEOUT);
                if (!err) {
                        chip->port[minor] = hdl;
                        sys_mutex_unlock(chip->mtx);
                }
        }

        if (!err && config) {
                err = configure(hdl, config);
        }

        if (err) {
                if (minor == 0) {
                        if (chip) {
                                if (chip->sem) {
                                        sys_semaphore_destroy(chip->sem);
                                }

                                if (chip->mtx) {
                                        sys_mutex_destroy(chip->mtx);
                                }

                                sys_free(cast(void **, &chip));
                        }

                } else if (chip && (sys_mutex_lock(chip->mtx, MUTEX_TIMEOUT) == ESUCC)) {
                        chip->port[minor] = NULL;
                        sys_mutex_unlock(chip->mtx);
                }

                sys_free(device_handle);
        }

        return err;
}

//==============================================================================
/**
 * @brief Release device.
 *
 * @param[in ]          *device_handle          device allocated memory
 *
 * @return One of errno value (errno.h).
 */
//==============================================================================
API_MOD_RELEASE(MAX310X, void *device_handle)
{
        port_t *hdl = device_handle;
        chip_t *chip = hdl->chip;

        int err = sys_mutex_lock(chip->mtx, 0);
        if (err) {
                return err;
        }

        for (int i = hdl->minor + 1; i < PORTS_MAX; i++) {
                if (chip->port[i]) {
                        sys_mutex_unlock(chip->mtx);
                        return EBUSY; /* release other UARTs first */
                }
        }

        if (hdl->open) {
                sys_mutex_unlock(chip->mtx);
                return EBUSY;
        }

        hdl->ready = false;
        chip->port[hdl->minor] = NULL;

        if (hdl->minor == 0) {
                chip_stop(chip);
        }

        if (hdl->dev && ((hdl->minor == 0) || (hdl->conf.interface == MAX310X_INTERFACE__I2C))) {
                sys_fclose(hdl->dev);
        }

        if (hdl->rxq) {
                sys_queue_destroy(hdl->rxq);
        }

        if (hdl->txq) {
                sys_queue_destroy(hdl->txq);
        }

        sys_mutex_unlock(chip->mtx);

        if (hdl->minor == 0) {
                sys_semaphore_destroy(chip->sem);
                sys_mutex_destroy(chip->mtx);
                sys_free(cast(void **, &chip));
        }

        sys_free(&device_handle);

        return ESUCC;
}

//==============================================================================
/**
 * @brief Open device.
 *
 * @param[in ]          *device_handle          device allocated memory
 * @param[in ]           flags                  file operation flags (O_RDONLY, O_WRONLY, O_RDWR)
 *
 * @return One of errno value (errno.h).
 */
//==============================================================================
API_MOD_OPEN(MAX310X, void *device_handle, u32_t flags)
{
        UNUSED_ARG1(flags);

        port_t *hdl = device_handle;

        int err = sys_mutex_lock(hdl->chip->mtx, MUTEX_TIMEOUT);
        if (!err) {
                hdl->open++;
                sys_mutex_unlock(hdl->chip->mtx);
        }

        return err;
}

//==============================================================================
/**
 * @brief Close device.
 *
 * @param[in ]          *device_handle          device allocated memory
 * @param[in ]           force                  device force close (true)
 *
 * @return One of errno value (errno.h).
 */
//==============================================================================
API_MOD_CLOSE(MAX310X, void *device_handle, bool force)
{
        UNUSED_ARG1(force);

        port_t *hdl = device_handle;

        int err = sys_mutex_lock(hdl->chip->mtx, MUTEX_TIMEOUT);
        if (!err) {
                if (hdl->open > 0) {
                        hdl->open--;
                }

                sys_mutex_unlock(hdl->chip->mtx);
        }

        return err;
}

//==============================================================================
/**
 * @brief Write data to device.
 *
 * @param[in ]          *device_handle          device allocated memory
 * @param[in ]          *src                    data source
 * @param[in ]           count                  number of bytes to write
 * @param[in ][out]     *fpos                   file position
 * @param[out]          *wrcnt                  number of written bytes
 * @param[in ]           fattr                  file attributes
 *
 * @return One of errno value (errno.h).
 */
//==============================================================================
API_MOD_WRITE(MAX310X, void *device_handle, const u8_t *src, size_t count, fpos_t *fpos, size_t *wrcnt,
        struct vfs_fattr fattr)
{
        UNUSED_ARG1(fpos);

        port_t *hdl = device_handle;

        *wrcnt = 0;

        if (!hdl->ready || !hdl->conf.tx_enable) {
                return EPERM;
        }

        while (count > 0) {
                /* buffer is full: thread transmits data, then the write waits for space */
                if (sys_queue_send(hdl->txq, src, 0) != ESUCC) {
                        sys_semaphore_signal(hdl->chip->sem);

                        if (fattr.non_blocking_wr || (sys_queue_send(hdl->txq, src, _WAIT_FOREVER_MS) != ESUCC)) {
                                break;
                        }
                }

                src++;
                count--;
                (*wrcnt)++;
        }

        sys_semaphore_signal(hdl->chip->sem);

        return ESUCC;
}

//==============================================================================
/**
 * @brief Read data from device.
 *
 * @param[in ]          *device_handle          device allocated memory
 * @param[out]          *dst                    data destination
 * @param[in ]           count                  number of bytes to read
 * @param[in ][out]     *fpos                   file position
 * @param[out]          *rdcnt                  number of read bytes
 * @param[in ]           fattr                  file attributes
 *
 * @return One of errno value (errno.h).
 */
//==============================================================================
API_MOD_READ(MAX310X, void *device_handle, u8_t *dst, size_t count, fpos_t *fpos, size_t *rdcnt, struct vfs_fattr fattr)
{
        UNUSED_ARG1(fpos);

        port_t *hdl = device_handle;

        *rdcnt = 0;

        if (!hdl->ready || !hdl->conf.rx_enable) {
                return EPERM;
        }

        u32_t timeout = fattr.non_blocking_rd ? 0 : _WAIT_FOREVER_MS;

        while (count > 0) {
                if (sys_queue_receive(hdl->rxq, dst, timeout) != ESUCC) {
                        break;
                }

                dst++;
                count--;
                (*rdcnt)++;

                if (fattr.non_strict_rd) {
                        timeout = 0;
                }
        }

        return ESUCC;
}

//==============================================================================
/**
 * @brief IO control.
 *
 * @param[in ]          *device_handle          device allocated memory
 * @param[in ]           request                request
 * @param[in ][out]     *arg                    request's argument
 *
 * @return One of errno value (errno.h).
 */
//==============================================================================
API_MOD_IOCTL(MAX310X, void *device_handle, int request, void *arg)
{
        port_t *hdl = device_handle;

        if (!arg) {
                return EINVAL;
        }

        int err;

        switch (request) {
        case IOCTL_MAX310X__CONFIGURE:
                err = configure(hdl, arg);
                break;

        case IOCTL_MAX310X__GET_CONFIGURATION:
                *cast(MAX310X_config_t *, arg) = hdl->conf;
                err = ESUCC;
                break;

        case IOCTL_MAX310X__CONFIGURE_STR: {
                static const char *const CHIP[] = {"max3107", "max3108", "max3109"};
                static const char *const INTERFACE[] = {"spi", "i2c"};
                static const char *const PARITY[] = {"off", "odd", "even"};
                static const char *const STOP_BITS[] = {"1", "2"};

                char path[64];
                MAX310X_config_t cfg = hdl->conf;
                cfg.path = path;

                int chip = sys_opt_get_enum(arg, "chip", CHIP, ARRAY_SIZE(CHIP), cfg.chip);
                int interface = sys_opt_get_enum(arg, "interface", INTERFACE, ARRAY_SIZE(INTERFACE), cfg.interface);
                int parity = sys_opt_get_enum(arg, "parity", PARITY, ARRAY_SIZE(PARITY), MAX310X_PARITY__OFF);
                int stop = sys_opt_get_enum(arg, "stop_bits", STOP_BITS, ARRAY_SIZE(STOP_BITS), MAX310X_STOP_BITS__1);
                int reset_port = sys_opt_get_int(arg, "reset_port", -1);
                int reset_pin = sys_opt_get_int(arg, "reset_pin", -1);
                int irq_port = sys_opt_get_int(arg, "irq_port", -1);
                int irq_pin = sys_opt_get_int(arg, "irq_pin", -1);

                cfg.clock = sys_opt_get_int(arg, "clock", cfg.clock);
                cfg.crystal = sys_opt_get_bool(arg, "crystal", cfg.crystal);
                cfg.baud = sys_opt_get_int(arg, "baud", 115200);
                cfg.word_length = sys_opt_get_int(arg, "word_length", 8);
                cfg.tx_enable = sys_opt_get_bool(arg, "tx_enable", true);
                cfg.rx_enable = sys_opt_get_bool(arg, "rx_enable", true);
                cfg.tx_invert = sys_opt_get_bool(arg, "tx_invert", false);
                cfg.rx_invert = sys_opt_get_bool(arg, "rx_invert", false);
                cfg.hw_flow_ctrl = sys_opt_get_bool(arg, "hw_flow_ctrl", false);
                cfg.flow_halt = sys_opt_get_int(arg, "flow_halt", 96);
                cfg.flow_resume = sys_opt_get_int(arg, "flow_resume", 32);
                cfg.rs485 = sys_opt_get_bool(arg, "rs485", false);
                cfg.echo_suppress = sys_opt_get_bool(arg, "echo_suppress", false);
                cfg.loopback = sys_opt_get_bool(arg, "loopback", false);
                cfg.rx_buffer = sys_opt_get_int(arg, "rx_buffer", DEFAULT_RX_BUFFER);
                cfg.tx_buffer = sys_opt_get_int(arg, "tx_buffer", DEFAULT_TX_BUFFER);

                if ((reset_port >= 0) && (reset_pin >= 0)) {
                        cfg.reset_pin_used = true;
                        cfg.reset_port_idx = reset_port;
                        cfg.reset_pin_idx = reset_pin;
                }

                if ((irq_port >= 0) && (irq_pin >= 0)) {
                        cfg.irq_pin_used = true;
                        cfg.irq_port_idx = irq_port;
                        cfg.irq_pin_idx = irq_pin;
                }

                if (sys_opt_get_str_copy(arg, "path", path, sizeof(path)) == 0) {
                        cfg.path = NULL; /* UART 1 on SPI: file of UART 0 */
                }

                if ((chip < 0) || (interface < 0) || (parity < 0) || (stop < 0)) {
                        err = EINVAL;

                } else {
                        cfg.chip = chip;
                        cfg.interface = interface;
                        cfg.parity = parity;
                        cfg.stop_bits = stop;
                        err = configure(hdl, &cfg);
                }
                break;
        }

        default:
                err = EBADRQC;
                break;
        }

        return err;
}

//==============================================================================
/**
 * @brief Flush device: wait until data are transmitted.
 *
 * @param[in ]          *device_handle          device allocated memory
 *
 * @return One of errno value (errno.h).
 */
//==============================================================================
API_MOD_FLUSH(MAX310X, void *device_handle)
{
        port_t *hdl = device_handle;

        if (!hdl->ready || !hdl->conf.tx_enable) {
                return ESUCC;
        }

        int err = ESUCC;

        for (;;) {
                size_t items = 0;
                u8_t lvl = 0;

                err = sys_queue_get_number_of_items(hdl->txq, &items);

                if (!err && (items == 0)) {
                        err = sys_mutex_lock(hdl->chip->mtx, MUTEX_TIMEOUT);
                        if (!err) {
                                err = hdl->ready ? read_reg(hdl, MAX310X_REG_TXFIFOLVL, &lvl, 1) : EIO;
                                sys_mutex_unlock(hdl->chip->mtx);
                        }
                }

                /* wait also for the last character in the transmitter */
                sys_sleep_ms(1 + (12000 / hdl->conf.baud));

                if (err || ((items == 0) && (lvl == 0))) {
                        break;
                }
        }

        return err;
}

//==============================================================================
/**
 * @brief Device information.
 *
 * @param[in ]          *device_handle          device allocated memory
 * @param[out]          *device_stat            device status
 *
 * @return One of errno value (errno.h).
 */
//==============================================================================
API_MOD_STAT(MAX310X, void *device_handle, struct vfs_dev_stat *device_stat)
{
        UNUSED_ARG1(device_handle);

        device_stat->st_size = 0;

        return ESUCC;
}

//==============================================================================
/**
 * @brief  Function configures UART (UART 0: also the chip).
 *
 * @param  hdl          UART handle
 * @param  cfg          configuration
 *
 * @return One of errno value (errno.h).
 */
//==============================================================================
static int configure(port_t *hdl, const MAX310X_config_t *cfg)
{
        chip_t *chip = hdl->chip;

        bool shared_dev = (hdl->minor > 0) && (cfg->interface == MAX310X_INTERFACE__SPI);

        if ((!cfg->path && !shared_dev) || (cfg->interface > MAX310X_INTERFACE__I2C) ||
                (cfg->chip > MAX310X_CHIP__MAX3109) || (cfg->baud == 0) || (cfg->parity > MAX310X_PARITY__EVEN) ||
                (cfg->stop_bits > MAX310X_STOP_BITS__2) || (cfg->word_length < 5) || (cfg->word_length > 8) ||
                (cfg->rx_buffer == 0) || (cfg->tx_buffer == 0) ||
                (cfg->hw_flow_ctrl && ((cfg->flow_halt / 8) <= (cfg->flow_resume / 8)))) {
                return EINVAL;
        }

        int err = sys_mutex_lock(chip->mtx, MUTEX_TIMEOUT);
        if (err) {
                return err;
        }

        port_t *port0 = chip->port[0];

        if (hdl->minor == 0) {
                if ((cfg->clock == 0) || ((cfg->chip != MAX310X_CHIP__MAX3109) && (chip->port[1]))) {
                        err = EINVAL;
                }

        } else if (!port0 || !port0->ready) {
                dev_dbg(hdl, "UART 0 is not configured");
                err = ENODEV;

        } else if ((port0->conf.chip != MAX310X_CHIP__MAX3109) || (cfg->interface != port0->conf.interface)) {
                err = EINVAL;
        }

        if (!err) {
                err = port_buffers(hdl, cfg);
        }

        if (!err) {
                hdl->ready = false;

                if (hdl->minor == 0) {
                        /* other UARTs stop using the SPI file of UART 0 */
                        for (int i = 1; i < PORTS_MAX; i++) {
                                port_t *p = chip->port[i];
                                if (p) {
                                        p->ready = false;
                                        if (p->conf.interface == MAX310X_INTERFACE__SPI) {
                                                p->dev = NULL;
                                        }
                                }
                        }
                }

                if (hdl->dev && !((hdl->minor > 0) && (hdl->dev == port0->dev))) {
                        sys_fclose(hdl->dev);
                }

                hdl->dev = NULL;
                hdl->conf = *cfg;
                hdl->conf.path = NULL; /* string of caller */
                hdl->conf.flow_halt = min(cfg->flow_halt, 120);
                hdl->conf.flow_resume = min(cfg->flow_resume, 120);

                if (hdl->minor > 0) {
                        /* chip parameters are set by UART 0 */
                        hdl->conf.chip = port0->conf.chip;
                        hdl->conf.clock = port0->conf.clock;
                        hdl->conf.crystal = port0->conf.crystal;
                        hdl->conf.reset_pin_used = false;
                        hdl->conf.irq_pin_used = false;
                }

                if (shared_dev) {
                        /* SPI device file can be open once: UARTs use file of UART 0 */
                        hdl->dev = port0->dev;
                } else {
                        err = sys_fopen(cfg->path, O_RDWR, 0, &hdl->dev);
                }
        }

        if (!err) {
                err = (hdl->minor == 0) ? chip_init(hdl) : port_setup(hdl);
        }

        if (err) {
                dev_dbg(hdl, "configuration error %d", err);

                if (hdl->dev && !shared_dev) {
                        sys_fclose(hdl->dev);
                }

                hdl->dev = NULL;
                hdl->conf.baud = 0; /* UART is not configured */
        }

        sys_mutex_unlock(chip->mtx);

        return err;
}

//==============================================================================
/**
 * @brief  Function initializes the chip (called with chip mutex): reset,
 *         detection, clock, IRQ, configuration of UARTs and the thread.
 *
 * @param  hdl          handle of UART 0
 *
 * @return One of errno value (errno.h).
 */
//==============================================================================
static int chip_init(port_t *hdl)
{
        chip_t *chip = hdl->chip;

        chip_stop(chip);

        for (int i = 1; i < PORTS_MAX; i++) {
                port_t *p = chip->port[i];

                /* configured UART on SPI uses the new file of UART 0 */
                if (p && (p->conf.baud > 0) && (p->conf.interface == MAX310X_INTERFACE__SPI)) {
                        p->dev = hdl->dev;
                }
        }

        int err = ESUCC;

        if (hdl->conf.reset_pin_used) {
                _GPIO_DDI_clear_pin(hdl->conf.reset_port_idx, hdl->conf.reset_pin_idx);
                sys_sleep_ms(1);
                _GPIO_DDI_set_pin(hdl->conf.reset_port_idx, hdl->conf.reset_pin_idx);

                err = wait_for_reg(hdl, MAX310X_REG_DIVLSB, 0xFF, MAX310X_DIVLSB_RESET, RESET_TIMEOUT_MS);
        }

        /* soft reset of UART 0: registers have reset values (MAX3108 detection) */
        if (!err) {
                err = write_reg_byte(hdl, MAX310X_REG_MODE2, MAX310X_MODE2_RST_BIT);
        }

        if (!err) {
                err = write_reg_byte(hdl, MAX310X_REG_MODE2, 0);
        }

        if (!err) {
                err = wait_for_reg(hdl, MAX310X_REG_DIVLSB, 0xFF, MAX310X_DIVLSB_RESET, RESET_TIMEOUT_MS);
        }

        if (!err) {
                err = chip_detect(hdl);
        }

        /* reference clock (external clock or crystal, PLL is not used) */
        if (!err) {
                u8_t clksrc = MAX310X_CLKSRC_EXTCLK_BIT | MAX310X_CLKSRC_PLLBYP_BIT;

                if (hdl->conf.crystal) {
                        clksrc |= MAX310X_CLKSRC_CRYST_BIT;
                }

                err = write_reg_byte(hdl, MAX310X_REG_CLKSOURCE, clksrc);

                if (!err && hdl->conf.crystal) {
                        err = wait_for_reg(hdl, MAX310X_REG_STSINT, MAX310X_STS_CLKREADY_BIT, MAX310X_STS_CLKREADY_BIT,
                                CRYSTAL_TIMEOUT_MS);
                }
        }

        if (!err && hdl->conf.irq_pin_used) {
#if __ENABLE_IRQ__ > 0
                err = _IRQ_DDI_attach(hdl->conf.irq_pin_idx, hdl->conf.irq_port_idx,
                        IRQ_CONFIG__TRIGGER_ON_FALLING_EDGE, irq_handler, chip);
                chip->irq = !err;
#else
                dev_dbg(hdl, "IRQ driver is not in the kernel");
                err = ENOTSUP;
#endif
        }

        for (int i = 0; !err && (i < PORTS_MAX); i++) {
                port_t *p = chip->port[i];

                if (p && p->dev) {
                        err = port_setup(p);
                }
        }

        if (!err) {
                chip->thread_run = true;
                err = sys_thread_create(thread, &THREAD_ATTR, chip, &chip->thread);
                chip->thread_run = !err;
        }

        if (err) {
                chip_stop(chip);
        }

        return err;
}

//==============================================================================
/**
 * @brief  Function stops thread and IRQ of the chip (called with chip mutex).
 *
 * @param  chip         chip
 */
//==============================================================================
static void chip_stop(chip_t *chip)
{
        if (chip->thread_run) {
                /* thread waits for semaphore or chip mutex */
                sys_thread_destroy(chip->thread);
                chip->thread_run = false;
        }

#if __ENABLE_IRQ__ > 0
        if (chip->irq) {
                port_t *port0 = chip->port[0];
                _IRQ_DDI_detach(port0->conf.irq_pin_idx);
                chip->irq = false;
        }
#endif
}

//==============================================================================
/**
 * @brief  Function checks type of the chip.
 *
 * @param  hdl          handle of UART 0 (registers have reset values)
 *
 * @return One of errno value (errno.h).
 */
//==============================================================================
static int chip_detect(port_t *hdl)
{
        u8_t val = 0;
        u8_t expected = 0;
        int err;

        switch (hdl->conf.chip) {
        case MAX310X_CHIP__MAX3108:
                /* no revision ID: reset value of clock source */
                err = read_reg(hdl, MAX310X_REG_CLKSOURCE, &val, 1);
                expected = MAX310X_CLKSOURCE_RESET;
                break;

        case MAX310X_CHIP__MAX3109:
                err = write_reg_byte(hdl, MAX310X_REG_GLOBALCMD, MAX310X_GLOBALCMD_EXTREG_EN);
                if (!err) {
                        err = read_reg(hdl,
                                (hdl->conf.interface == MAX310X_INTERFACE__I2C) ? MAX310X_EXTREG_REVID_I2C
                                                                                : MAX310X_EXTREG_REVID_SPI,
                                &val, 1);

                        int dis = write_reg_byte(hdl, MAX310X_REG_GLOBALCMD, MAX310X_GLOBALCMD_EXTREG_DIS);
                        err = err ? err : dis;
                }

                val &= MAX310X_REVID_MASK;
                expected = MAX310X_REVID_MAX3109;
                break;

        default:
                err = read_reg(hdl, MAX310X_REG_REVID, &val, 1);
                val &= MAX310X_REVID_MASK;
                expected = MAX310X_REVID_MAX3107;
                break;
        }

        if (!err && (val != expected)) {
                dev_dbg(hdl, "unknown chip (0x%02X)", val);
                err = ENODEV;
        }

        return err;
}

//==============================================================================
/**
 * @brief  Function creates buffers of UART (called with chip mutex). Buffers
 *         are recreated when their size is changed (when the device is open
 *         only by configuring file), otherwise cleared.
 *
 * @param  hdl          UART handle
 * @param  cfg          new configuration
 *
 * @return One of errno value (errno.h).
 */
//==============================================================================
static int port_buffers(port_t *hdl, const MAX310X_config_t *cfg)
{
        bool resize = !hdl->rxq || !hdl->txq || (cfg->rx_buffer != hdl->conf.rx_buffer) ||
                (cfg->tx_buffer != hdl->conf.tx_buffer);

        if (!resize) {
                sys_queue_reset(hdl->rxq);
                sys_queue_reset(hdl->txq);
                return ESUCC;
        }

        if (hdl->open > 1) {
                return EBUSY; /* other open files can use buffers (one file can configure) */
        }

        hdl->ready = false;

        if (hdl->rxq) {
                sys_queue_destroy(hdl->rxq);
                hdl->rxq = NULL;
        }

        if (hdl->txq) {
                sys_queue_destroy(hdl->txq);
                hdl->txq = NULL;
        }

        int err = sys_queue_create(cfg->rx_buffer, 1, &hdl->rxq);
        if (!err) {
                err = sys_queue_create(cfg->tx_buffer, 1, &hdl->txq);
        }

        if (!err) {
                hdl->conf.rx_buffer = cfg->rx_buffer;
                hdl->conf.tx_buffer = cfg->tx_buffer;
        }

        return err;
}

//==============================================================================
/**
 * @brief  Function configures UART registers (called with chip mutex). UART is
 *         reset, so all registers are written.
 *
 * @param  hdl          UART handle
 *
 * @return One of errno value (errno.h).
 */
//==============================================================================
static int port_setup(port_t *hdl)
{
        chip_t *chip = hdl->chip;
        const MAX310X_config_t *cfg = &hdl->conf;

        hdl->ready = false;
        hdl->irqen = 0;

        int err = ESUCC;

        if (hdl->minor > 0) {
                err = write_reg_byte(hdl, MAX310X_REG_MODE2, MAX310X_MODE2_RST_BIT);

                if (!err) {
                        err = write_reg_byte(hdl, MAX310X_REG_MODE2, 0);
                }

                if (!err) {
                        err = wait_for_reg(hdl, MAX310X_REG_DIVLSB, 0xFF, MAX310X_DIVLSB_RESET, RESET_TIMEOUT_MS);
                }
        }

        if (!err) {
                err = set_baud(hdl);
        }

        if (!err) {
                u8_t lcr = cfg->word_length - 5; /* LENGTH[1:0] */

                if (cfg->stop_bits == MAX310X_STOP_BITS__2) {
                        lcr |= MAX310X_LCR_STOPLEN_BIT;
                }

                if (cfg->parity != MAX310X_PARITY__OFF) {
                        lcr |= MAX310X_LCR_PARITY_BIT;
                }

                if (cfg->parity == MAX310X_PARITY__EVEN) {
                        lcr |= MAX310X_LCR_EVENPARITY_BIT;
                }

                err = write_reg_byte(hdl, MAX310X_REG_LCR, lcr);
        }

        if (!err) {
                u8_t irda = (cfg->tx_invert ? MAX310X_IRDA_TXINV_BIT : 0) |
                        (cfg->rx_invert ? MAX310X_IRDA_RXINV_BIT : 0);
                err = write_reg_byte(hdl, MAX310X_REG_IRDA, irda);
        }

        if (!err && cfg->rs485) {
                /* RTS setup time before transmission and hold time after it */
                err = write_reg_byte(hdl, MAX310X_REG_HDPLXDELAY, (1 << 4) | 1);
        }

        if (!err && cfg->hw_flow_ctrl) {
                err = write_reg_byte(hdl, MAX310X_REG_FLOWLVL,
                        MAX310X_FLOWLVL_RES(cfg->flow_resume) | MAX310X_FLOWLVL_HALT(cfg->flow_halt));

                if (!err) {
                        err = write_reg_byte(hdl, MAX310X_REG_FLOWCTRL,
                                MAX310X_FLOWCTRL_AUTOCTS_BIT | MAX310X_FLOWCTRL_AUTORTS_BIT);
                }
        }

        if (!err && chip->irq) {
                /* interrupts: RX FIFO level, RX timeout (LSR), TX FIFO empty (by thread) */
                err = write_reg_byte(hdl, MAX310X_REG_FIFOTRGLVL, MAX310X_FIFOTRIGLVL_RX(RX_TRIGGER_LEVEL));

                if (!err) {
                        err = write_reg_byte(hdl, MAX310X_REG_RXTIMEOUT, RX_TIMEOUT_CHARS);
                }

                if (!err) {
                        err = write_reg_byte(hdl, MAX310X_REG_LSRINTEN, MAX310X_LSR_RXTO_BIT);
                }

                if (!err) {
                        hdl->irqen = MAX310X_IRQ_RXFIFO_BIT | MAX310X_IRQ_LSR_BIT;
                        err = write_reg_byte(hdl, MAX310X_REG_IRQEN, hdl->irqen);
                }
        }

        if (!err) {
                u8_t mode2 = (cfg->loopback ? MAX310X_MODE2_LOOPBACK_BIT : 0) |
                        (cfg->echo_suppress ? MAX310X_MODE2_ECHOSUPR_BIT : 0);

                err = write_reg_byte(hdl, MAX310X_REG_MODE2, mode2);
        }

        if (!err) {
                /* transmitter and receiver are enabled at the end, MAX3107: IRQ pin enable */
                u8_t mode1 = (cfg->tx_enable ? 0 : MAX310X_MODE1_TXDIS_BIT) |
                        (cfg->rx_enable ? 0 : MAX310X_MODE1_RXDIS_BIT) |
                        (cfg->rs485 ? MAX310X_MODE1_TRNSCVCTRL_BIT : 0) |
                        ((chip->irq && (cfg->chip == MAX310X_CHIP__MAX3107)) ? MAX310X_MODE1_IRQSEL_BIT : 0);

                err = write_reg_byte(hdl, MAX310X_REG_MODE1, mode1);
        }

        if (!err) {
                /* period of thread without IRQ: about 64 characters of the fastest UART */
                u8_t poll = max(POLL_MIN_MS, min(POLL_MAX_MS, 640000 / cfg->baud));
                bool first = true;

                for (int i = 0; i < PORTS_MAX; i++) {
                        port_t *p = chip->port[i];
                        if (p && p->ready) {
                                first = false;
                        }
                }

                chip->poll_ms = first ? poll : min(chip->poll_ms, poll);

                sys_queue_reset(hdl->rxq);
                sys_queue_reset(hdl->txq);
                hdl->ready = true;
        }

        return err;
}

//==============================================================================
/**
 * @brief  Function sets baud rate generator: baud = clock / (16 * (DIV +
 *         FRACT / 16)) in 1x mode, 2x and 4x modes are used for high baud rates.
 *
 * @param  hdl          UART handle
 *
 * @return One of errno value (errno.h).
 */
//==============================================================================
static int set_baud(port_t *hdl)
{
        u32_t clock = hdl->conf.clock;
        u32_t baud = hdl->conf.baud;
        u32_t div = clock / baud;
        u32_t c = 16;
        u8_t mode = 0;

        if (div < 8) {
                c = 4;
                mode = MAX310X_BRGCFG_4XMODE_BIT;
        } else if (div < 16) {
                c = 8;
                mode = MAX310X_BRGCFG_2XMODE_BIT;
        }

        u32_t f = c * baud;
        u32_t fract = 0;

        div = clock / f;
        if (div > 0) {
                fract = (16 * (clock % f)) / f;
        } else {
                div = 1;
        }

        if (div > 0xFFFF) {
                return EINVAL;
        }

        /* real baud rate differs more than 2% */
        u32_t real = (u32_t)((16ULL * clock) / (c * (16 * div + fract)));
        if ((real > baud + baud / 50) || (real < baud - baud / 50)) {
                dev_dbg(hdl, "baud rate %lu not possible (%lu)", (unsigned long)baud, (unsigned long)real);
                return EINVAL;
        }

        int err = write_reg_byte(hdl, MAX310X_REG_DIVMSB, div >> 8);
        if (!err) {
                err = write_reg_byte(hdl, MAX310X_REG_DIVLSB, div);
        }

        if (!err) {
                err = write_reg_byte(hdl, MAX310X_REG_BRGCONFIG, fract | mode);
        }

        return err;
}

#if __ENABLE_IRQ__ > 0
//==============================================================================
/**
 * @brief  IRQ pin of the chip (interrupt): wakes thread.
 *
 * @param  line         EXTI line
 * @param  arg          chip
 *
 * @return True if higher priority task was woken.
 */
//==============================================================================
static bool irq_handler(u8_t line, void *arg)
{
        UNUSED_ARG1(line);

        chip_t *chip = arg;
        bool woken = false;
        sys_semaphore_signal_from_ISR(chip->sem, &woken);

        return woken;
}
#endif

//==============================================================================
/**
 * @brief  Thread of the chip: moves data between FIFOs and buffers.
 *
 * @param  arg          chip
 *
 * @return Never returns (thread is destroyed by chip_stop()).
 */
//==============================================================================
static int thread(void *arg)
{
        chip_t *chip = arg;

        for (;;) {
                sys_semaphore_wait(chip->sem, chip->irq ? IRQ_GUARD_MS : chip->poll_ms);

                for (int loop = 0; loop < IRQ_LOOPS; loop++) {
                        if (sys_mutex_lock(chip->mtx, MUTEX_TIMEOUT) != ESUCC) {
                                break;
                        }

                        for (int i = 0; i < PORTS_MAX; i++) {
                                port_t *p = chip->port[i];
                                if (p && p->ready) {
                                        port_service(p);
                                }
                        }

                        bool again = false;
#if __ENABLE_IRQ__ > 0
                        if (chip->irq) {
                                /* IRQ pin is still active (new event during service) */
                                port_t *port0 = chip->port[0];
                                again = (_GPIO_DDI_get_pin(port0->conf.irq_port_idx, port0->conf.irq_pin_idx) == 0);
                        }
#endif
                        sys_mutex_unlock(chip->mtx);

                        if (!again) {
                                break;
                        }
                }
        }

        return ESUCC;
}

//==============================================================================
/**
 * @brief  Service of UART (called with chip mutex): RX FIFO to RX buffer,
 *         TX buffer to TX FIFO, interrupts.
 *
 * @param  hdl          UART handle
 */
//==============================================================================
static void port_service(port_t *hdl)
{
        chip_t *chip = hdl->chip;
        u8_t val;

        if (chip->irq) {
                /* clear interrupts: ISR and LSR (RX timeout) are cleared by read */
                read_reg(hdl, MAX310X_REG_ISR, &val, 1);
                read_reg(hdl, MAX310X_REG_LSR, &val, 1);
        }

        /* RX: data that fit to the buffer, other data wait in the FIFO */
        size_t items = 0;
        sys_queue_get_number_of_items(hdl->rxq, &items);

        size_t space = hdl->conf.rx_buffer - min(items, hdl->conf.rx_buffer);

        if ((space > 0) && (read_reg(hdl, MAX310X_REG_RXFIFOLVL, &val, 1) == ESUCC)) {
                size_t len = min(min(val, space), sizeof(chip->buf));

                if ((len > 0) && (read_reg(hdl, MAX310X_REG_RHR_THR, chip->buf, len) == ESUCC)) {
                        for (size_t i = 0; i < len; i++) {
                                sys_queue_send(hdl->rxq, &chip->buf[i], 0);
                        }
                }
        }

        /* TX: data of buffer that fit to the FIFO */
        items = 0;
        sys_queue_get_number_of_items(hdl->txq, &items);

        if ((items > 0) && (read_reg(hdl, MAX310X_REG_TXFIFOLVL, &val, 1) == ESUCC)) {
                size_t len = min(min(items, MAX310X_FIFO_SIZE - min(val, MAX310X_FIFO_SIZE)), sizeof(chip->buf));

                for (size_t i = 0; i < len; i++) {
                        sys_queue_receive(hdl->txq, &chip->buf[i], 0);
                }

                if (len > 0) {
                        write_reg(hdl, MAX310X_REG_RHR_THR, chip->buf, len);
                }

                items -= len;
        }

        /* TX FIFO empty interrupt: only when buffer has data */
        if (chip->irq) {
                u8_t irqen = (hdl->irqen & ~MAX310X_IRQ_TXEMPTY_BIT) | ((items > 0) ? MAX310X_IRQ_TXEMPTY_BIT : 0);

                if ((irqen != hdl->irqen) && (write_reg_byte(hdl, MAX310X_REG_IRQEN, irqen) == ESUCC)) {
                        hdl->irqen = irqen;
                }
        }
}

//==============================================================================
/**
 * @brief  Function waits until register has selected value.
 *
 * @param  hdl          UART handle
 * @param  reg          register
 * @param  mask         mask of bits
 * @param  value        expected value of masked bits
 * @param  timeout_ms   timeout
 *
 * @return One of errno value (errno.h).
 */
//==============================================================================
static int wait_for_reg(port_t *hdl, u8_t reg, u8_t mask, u8_t value, u32_t timeout_ms)
{
        u32_t tref = sys_get_uptime_ms();

        for (;;) {
                u8_t val = 0;
                int err = read_reg(hdl, reg, &val, 1);

                if (!err && ((val & mask) == value)) {
                        return ESUCC;

                } else if (sys_is_time_expired(tref, timeout_ms)) {
                        dev_dbg(hdl, "register 0x%02X timeout", reg);
                        return err ? err : ETIME;
                }

                sys_sleep_ms(1);
        }
}

//==============================================================================
/**
 * @brief  Read registers of UART (FIFO: RHR register is not incremented).
 *
 * @param  hdl          UART handle
 * @param  reg          first register
 * @param  buf          destination buffer
 * @param  count        number of bytes to read
 *
 * @return One of errno value (errno.h).
 */
//==============================================================================
static int read_reg(port_t *hdl, u8_t reg, u8_t *buf, size_t count)
{
        int err;

        if (hdl->conf.interface == MAX310X_INTERFACE__I2C) {
                /* I2C device of UART (1 byte sub-address): file position is register */
                err = sys_fseek(hdl->dev, reg, VFS_SEEK_SET);
                if (!err) {
                        size_t rdcnt = 0;
                        err = sys_fread(buf, count, &rdcnt, hdl->dev);
                        if (!err && (rdcnt != count)) {
                                err = EIO;
                        }
                }

        } else {
                /* SPI: command (bit 7 = 0: read, UART, register) and data in one CS cycle */
                u8_t cmd = (hdl->minor << MAX310X_SPI_PORT_SHIFT) | (reg & 0x1F);

                SPI_transceive_t tdata = {
                        .tx_buffer = NULL,
                        .rx_buffer = buf,
                        .count = count,
                        .separated = false,
                        .next = NULL,
                };

                SPI_transceive_t tcmd = {
                        .tx_buffer = &cmd,
                        .rx_buffer = NULL,
                        .count = 1,
                        .separated = false,
                        .next = &tdata,
                };

                err = sys_ioctl(hdl->dev, IOCTL_SPI__TRANSCEIVE, &tcmd);
        }

        return err;
}

//==============================================================================
/**
 * @brief  Write registers of UART (FIFO: THR register is not incremented).
 *
 * @param  hdl          UART handle
 * @param  reg          first register
 * @param  buf          source buffer
 * @param  count        number of bytes to write
 *
 * @return One of errno value (errno.h).
 */
//==============================================================================
static int write_reg(port_t *hdl, u8_t reg, const u8_t *buf, size_t count)
{
        int err;

        if (hdl->conf.interface == MAX310X_INTERFACE__I2C) {
                err = sys_fseek(hdl->dev, reg, VFS_SEEK_SET);
                if (!err) {
                        size_t wrcnt = 0;
                        err = sys_fwrite(buf, count, &wrcnt, hdl->dev);
                        if (!err && (wrcnt != count)) {
                                err = EIO;
                        }
                }

        } else {
                /* SPI: command (bit 7 = 1: write, UART, register) and data in one CS cycle */
                u8_t cmd = MAX310X_SPI_WRITE_BIT | (hdl->minor << MAX310X_SPI_PORT_SHIFT) | (reg & 0x1F);

                SPI_transceive_t tdata = {
                        .tx_buffer = buf,
                        .rx_buffer = NULL,
                        .count = count,
                        .separated = false,
                        .next = NULL,
                };

                SPI_transceive_t tcmd = {
                        .tx_buffer = &cmd,
                        .rx_buffer = NULL,
                        .count = 1,
                        .separated = false,
                        .next = &tdata,
                };

                err = sys_ioctl(hdl->dev, IOCTL_SPI__TRANSCEIVE, &tcmd);
        }

        return err;
}

//==============================================================================
/**
 * @brief  Write one register of UART.
 *
 * @param  hdl          UART handle
 * @param  reg          register
 * @param  value        value
 *
 * @return One of errno value (errno.h).
 */
//==============================================================================
static int write_reg_byte(port_t *hdl, u8_t reg, u8_t value)
{
        return write_reg(hdl, reg, &value, 1);
}

/*==============================================================================
  End of file
==============================================================================*/
