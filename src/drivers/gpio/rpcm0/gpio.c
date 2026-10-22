/*==============================================================================
@file    gpio.c

@author  Daniel Zorychta

@brief   GPIO driver.

@note    Copyright (C) 2026  Daniel Zorychta <daniel.zorychta@gmail.com>

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

// #pragma GCC optimize("Og")

/*==============================================================================
  Include files
==============================================================================*/
#include "drivers/driver.h"
#include "rpcm0/gpio_cfg.h"
#include "hardware/regs/io_bank0.h"
#include "hardware/regs/resets.h"
#include "hardware/regs/addressmap.h"
#include "hardware/structs/io_bank0.h"
#include "hardware/structs/pads_bank0.h"
#include "hardware/structs/io_qspi.h"
#include "hardware/structs/pads_qspi.h"
#include "hardware/structs/sio.h"
#include "hardware/structs/resets.h"
#include "../gpio_ioctl.h"

/*==============================================================================
  Local symbolic constants/macros
==============================================================================*/
#define GPIO_CONF(bank, pin_num)                                                                                   \
        .pin = {.port_idx = IOCTL_GPIO_##bank##_IDX, .pin_idx = pin_num},                                          \
        .speed = (__GPIO_##bank##_PIN_##pin_num##_STRENGTH__ & _GPIO_BIT_SLEW_RATE_FAST) ? GPIO_pin_speed__fast    \
                                                                                         : GPIO_pin_speed__slow,   \
        .mode = (__GPIO_##bank##_PIN_##pin_num##_MODE__ >> 0) & 0xFF,                                              \
        .pull = (__GPIO_##bank##_PIN_##pin_num##_MODE__ >> 8) & 0xFF, .mux = __GPIO_##bank##_PIN_##pin_num##_AF__, \
        .drive_strength = __GPIO_##bank##_PIN_##pin_num##_STRENGTH__ & _GPOI_BIT_STRENGTH_MASK,                    \
        .schmitt_input = __GPIO_##bank##_PIN_##pin_num##_SCHMITT__ & 1,                                            \
        .state = __GPIO_##bank##_PIN_##pin_num##_STATE__ & 1,

/*==============================================================================
  Local types, enums definitions
==============================================================================*/
typedef struct {
        io_rw_32 *gpio_oe;
        io_rw_32 *gpio_oe_set;
        io_rw_32 *gpio_oe_clr;
        io_rw_32 *gpio_out;
        io_rw_32 *gpio_out_set;
        io_rw_32 *gpio_out_clr;
        io_rw_32 *gpio_out_togl;
        const io_rw_32 *gpio_in;
        const GPIO_pin_in_port_conf_t *conf;
        u32_t resets_mask;
        u32_t resets_done_mask;
        u8_t pins_count;
} GPIO_HW_t;

typedef struct {
        u32_t OD;
        u8_t major;
        u8_t minor;
} GPIO_hdl_t;

/*==============================================================================
  Local function prototypes
==============================================================================*/
static int pin_set(GPIO_hdl_t *hdl, u8_t port, u8_t pin);
static int pin_clear(GPIO_hdl_t *hdl, u8_t port, u8_t pin);
static int pin_toggle(GPIO_hdl_t *hdl, u8_t port, u8_t pin);
static int pin_get(GPIO_hdl_t *hdl, u8_t port, u8_t pin, int *state);
static int pin_set_conf(GPIO_hdl_t *hdl, const GPIO_pin_in_port_conf_t *conf);
static int pin_get_conf(GPIO_hdl_t *hdl, GPIO_pin_in_port_conf_t *conf);

/*==============================================================================
  Local object definitions
==============================================================================*/
MODULE_NAME(GPIO);

static const GPIO_pin_in_port_conf_t BANK0_CONF[] = {
        {GPIO_CONF(BANK0, 0)},
        {GPIO_CONF(BANK0, 1)},
        {GPIO_CONF(BANK0, 2)},
        {GPIO_CONF(BANK0, 3)},
        {GPIO_CONF(BANK0, 4)},
        {GPIO_CONF(BANK0, 5)},
        {GPIO_CONF(BANK0, 6)},
        {GPIO_CONF(BANK0, 7)},
        {GPIO_CONF(BANK0, 8)},
        {GPIO_CONF(BANK0, 9)},
        {GPIO_CONF(BANK0, 10)},
        {GPIO_CONF(BANK0, 11)},
        {GPIO_CONF(BANK0, 12)},
        {GPIO_CONF(BANK0, 13)},
        {GPIO_CONF(BANK0, 14)},
        {GPIO_CONF(BANK0, 15)},
        {GPIO_CONF(BANK0, 16)},
        {GPIO_CONF(BANK0, 17)},
        {GPIO_CONF(BANK0, 18)},
        {GPIO_CONF(BANK0, 19)},
        {GPIO_CONF(BANK0, 20)},
        {GPIO_CONF(BANK0, 21)},
        {GPIO_CONF(BANK0, 22)},
        {GPIO_CONF(BANK0, 23)},
        {GPIO_CONF(BANK0, 24)},
        {GPIO_CONF(BANK0, 25)},
        {GPIO_CONF(BANK0, 26)},
        {GPIO_CONF(BANK0, 27)},
        {GPIO_CONF(BANK0, 28)},
        {GPIO_CONF(BANK0, 29)},
};

static const GPIO_pin_in_port_conf_t QSPI_CONF[] = {
        {GPIO_CONF(QSPI, 0)},
        {GPIO_CONF(QSPI, 1)},
        {GPIO_CONF(QSPI, 2)},
        {GPIO_CONF(QSPI, 3)},
        {GPIO_CONF(QSPI, 4)},
        {GPIO_CONF(QSPI, 5)},
};

static const GPIO_HW_t GPIO_HW[] = {
        {
                .gpio_oe = &sio_hw->gpio_oe,
                .gpio_oe_set = &sio_hw->gpio_oe_set,
                .gpio_oe_clr = &sio_hw->gpio_oe_clr,
                .gpio_out = &sio_hw->gpio_out,
                .gpio_out_set = &sio_hw->gpio_set,
                .gpio_out_clr = &sio_hw->gpio_clr,
                .gpio_out_togl = &sio_hw->gpio_togl,
                .gpio_in = &sio_hw->gpio_in,
                .resets_mask = RESETS_RESET_IO_BANK0_BITS | RESETS_RESET_PADS_BANK0_BITS,
                .resets_done_mask = RESETS_RESET_DONE_IO_BANK0_BITS | RESETS_RESET_DONE_PADS_BANK0_BITS,
                .conf = BANK0_CONF,
                .pins_count = ARRAY_SIZE(BANK0_CONF),
        },
        {
                .gpio_oe = &sio_hw->gpio_hi_oe,
                .gpio_oe_set = &sio_hw->gpio_hi_oe_set,
                .gpio_oe_clr = &sio_hw->gpio_hi_oe_clr,
                .gpio_out = &sio_hw->gpio_hi_out,
                .gpio_out_set = &sio_hw->gpio_hi_set,
                .gpio_out_clr = &sio_hw->gpio_hi_clr,
                .gpio_out_togl = &sio_hw->gpio_hi_togl,
                .gpio_in = &sio_hw->gpio_hi_in,
                .resets_mask = RESETS_RESET_IO_QSPI_BITS | RESETS_RESET_PADS_QSPI_BITS,
                .resets_done_mask = RESETS_RESET_DONE_IO_QSPI_BITS | RESETS_RESET_DONE_PADS_QSPI_BITS,
                .conf = QSPI_CONF,
                .pins_count = ARRAY_SIZE(QSPI_CONF),
        },
};

/*==============================================================================
  Exported object definitions
==============================================================================*/

/*==============================================================================
  Function definitions
==============================================================================*/

//==============================================================================
/**
 * @brief Initialize device
 *
 * @param[out]          **device_handle        device allocated memory
 * @param[in ]            major                major device number
 * @param[in ]            minor                minor device number
 * @param[in ]            config               optional module configuration
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
API_MOD_INIT(GPIO, void **device_handle, u8_t major, u8_t minor, const void *config)
{
        UNUSED_ARG1(config);

        if ((major > 2) or (minor != 0)) {
                return EINVAL;
        }

        int err = sys_zalloc(sizeof(GPIO_hdl_t), device_handle);
        if (err) {
                return err;
        }

        GPIO_hdl_t *hdl = *device_handle;
        hdl->major = major;
        hdl->minor = minor;

        SET_BIT(resets_hw->reset, GPIO_HW[hdl->major].resets_mask);
        CLEAR_BIT(resets_hw->reset, GPIO_HW[hdl->major].resets_mask);
        while ((resets_hw->reset_done & GPIO_HW[hdl->major].resets_done_mask) == 0) {
                sys_sleep_ms(1);
        }

        for (size_t i = 0; i < GPIO_HW[hdl->major].pins_count; i++) {
                pin_set_conf(hdl, &GPIO_HW[hdl->major].conf[i]);
        }

        return err;
}

//==============================================================================
/**
 * @brief Release device
 *
 * @param[in ]          *device_handle          device allocated memory
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
API_MOD_RELEASE(GPIO, void *device_handle)
{
        GPIO_hdl_t *hdl = device_handle;

        SET_BIT(resets_hw->reset, GPIO_HW[hdl->major].resets_mask);
        CLEAR_BIT(resets_hw->reset, GPIO_HW[hdl->major].resets_mask);
        while ((resets_hw->reset_done & GPIO_HW[hdl->major].resets_done_mask) == 0) {
                sys_sleep_ms(1);
        }

        sys_free(&device_handle);

        return ESUCC;
}

//==============================================================================
/**
 * @brief Open device
 *
 * @param[in ]          *device_handle          device allocated memory
 * @param[in ]           flags                  file operation flags (O_RDONLY, O_WRONLY, O_RDWR)
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
API_MOD_OPEN(GPIO, void *device_handle, u32_t flags)
{
        UNUSED_ARG2(device_handle, flags);

        return ESUCC;
}

//==============================================================================
/**
 * @brief Close device
 *
 * @param[in ]          *device_handle          device allocated memory
 * @param[in ]           force                  device force close (true)
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
API_MOD_CLOSE(GPIO, void *device_handle, bool force)
{
        UNUSED_ARG2(device_handle, force);

        return ESUCC;
}

//==============================================================================
/**
 * @brief Write data to device
 *
 * @param[in ]          *device_handle          device allocated memory
 * @param[in ]          *src                    data source
 * @param[in ]           count                  number of bytes to write
 * @param[in ][out]     *fpos                   file position
 * @param[out]          *wrcnt                  number of written bytes
 * @param[in ]           fattr                  file attributes
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
API_MOD_WRITE(GPIO, void *device_handle, const u8_t *src, size_t count, fpos_t *fpos, size_t *wrcnt,
        struct vfs_fattr fattr)
{
        UNUSED_ARG2(fpos, fattr);

        GPIO_hdl_t *hdl = device_handle;

        size_t words = count / sizeof(u32_t);
        const u32_t *out = cast(typeof(out), src);

        while (words) {
                for (size_t i = 0; i < GPIO_HW[hdl->major].pins_count; i++) {
                        if (*out & (1 << i)) {
                                pin_set(hdl, hdl->major, i);
                        } else {
                                pin_clear(hdl, hdl->major, i);
                        }
                }

                out++;
                words--;
        }

        *fpos += words * sizeof(u32_t);
        *wrcnt = words * sizeof(u32_t);

        return ESUCC;
}

//==============================================================================
/**
 * @brief Read data from device
 *
 * @param[in ]          *device_handle          device allocated memory
 * @param[out]          *dst                    data destination
 * @param[in ]           count                  number of bytes to read
 * @param[in ][out]     *fpos                   file position
 * @param[out]          *rdcnt                  number of read bytes
 * @param[in ]           fattr                  file attributes
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
API_MOD_READ(GPIO, void *device_handle, u8_t *dst, size_t count, fpos_t *fpos, size_t *rdcnt, struct vfs_fattr fattr)
{
        UNUSED_ARG2(fpos, fattr);

        GPIO_hdl_t *hdl = device_handle;

        size_t words = count / sizeof(u32_t);
        u32_t *in = cast(typeof(in), dst);
        const io_rw_32 *gpio_in = (hdl->major == 0) ? &sio_hw->gpio_in : &sio_hw->gpio_hi_in;

        while (words) {
                *in++ = *gpio_in;
                words--;
        }

        *fpos += words * sizeof(u32_t);
        *rdcnt = words * sizeof(u32_t);

        return ESUCC;
}

//==============================================================================
/**
 * @brief IO control
 *
 * @param[in ]          *device_handle          device allocated memory
 * @param[in ]           request                request
 * @param[in ][out]     *arg                    request's argument
 *
 * @return On success return 0. On error, -1 is returned.
 */
//==============================================================================
API_MOD_IOCTL(GPIO, void *device_handle, int request, void *arg)
{
        int err = EINVAL;

        GPIO_hdl_t *hdl = device_handle;

        if (arg) {
                switch (request) {
                case IOCTL_GPIO__SET_PIN: {
                        u8_t pin = *cast(u8_t *, arg);
                        err = pin_set(hdl, hdl->major, pin);
                        break;
                }

                case IOCTL_GPIO__CLEAR_PIN: {
                        u8_t pin = *cast(u8_t *, arg);
                        err = pin_clear(hdl, hdl->major, pin);
                        break;
                }

                case IOCTL_GPIO__TOGGLE_PIN: {
                        u8_t pin = *cast(u8_t *, arg);
                        err = pin_toggle(hdl, hdl->major, pin);
                        break;
                }

                case IOCTL_GPIO__SET_PIN_STATE: {
                        GPIO_pin_state_t *pinstate = arg;
                        if (pinstate->state) {
                                err = pin_set(hdl, hdl->major, pinstate->pin_idx);
                        } else {
                                err = pin_clear(hdl, hdl->major, pinstate->pin_idx);
                        }
                        break;
                }

                case IOCTL_GPIO__GET_PIN_STATE: {
                        GPIO_pin_state_t *pinstate = arg;

                        err = pin_get(hdl, hdl->major, pinstate->pin_idx, &pinstate->state);
                        break;
                }

                case IOCTL_GPIO__SET_PIN_IN_PORT: {
                        GPIO_pin_in_port_t *pin = arg;

                        err = pin_set(NULL, pin->port_idx, pin->pin_idx);
                        break;
                }

                case IOCTL_GPIO__CLEAR_PIN_IN_PORT: {
                        GPIO_pin_in_port_t *pin = arg;

                        err = pin_clear(NULL, pin->port_idx, pin->pin_idx);
                        break;
                }

                case IOCTL_GPIO__TOGGLE_PIN_IN_PORT: {
                        GPIO_pin_in_port_t *pin = arg;

                        err = pin_toggle(NULL, pin->port_idx, pin->pin_idx);
                        break;
                }

                case IOCTL_GPIO__SET_PIN_IN_PORT_STATE: {
                        GPIO_pin_in_port_state_t *pin = arg;
                        if (pin->state) {
                                err = pin_set(NULL, pin->pin.port_idx, pin->pin.pin_idx);
                        } else {
                                err = pin_clear(NULL, pin->pin.port_idx, pin->pin.pin_idx);
                        }
                        break;
                }

                case IOCTL_GPIO__GET_PIN_IN_PORT_STATE: {
                        GPIO_pin_in_port_state_t *pin = arg;

                        err = pin_get(NULL, pin->pin.port_idx, pin->pin.pin_idx, &pin->state);
                        break;
                }

                case IOCTL_GPIO__SET_PIN_IN_PORT_CONF:
                        err = pin_set_conf(NULL, arg);
                        break;

                case IOCTL_GPIO__GET_PIN_IN_PORT_CONF:
                        err = pin_get_conf(NULL, arg);
                        break;

                default:
                        err = EBADRQC;
                        break;
                }
        }

        return err;
}

//==============================================================================
/**
 * @brief Flush device
 *
 * @param[in ]          *device_handle          device allocated memory
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
API_MOD_FLUSH(GPIO, void *device_handle)
{
        UNUSED_ARG1(device_handle);

        return ESUCC;
}

//==============================================================================
/**
 * @brief Device information
 *
 * @param[in ]          *device_handle          device allocated memory
 * @param[out]          *device_stat            device status
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
API_MOD_STAT(GPIO, void *device_handle, struct vfs_dev_stat *device_stat)
{
        GPIO_hdl_t *hdl = device_handle;

        device_stat->st_size = sizeof(GPIO_HW[hdl->major].gpio_out);
        device_stat->st_major = hdl->major;
        device_stat->st_minor = hdl->minor;

        return ESUCC;
}

//==============================================================================
/**
 * @brief  Set pin.
 *
 * @param  hdl          driver handle (can be NULL, load hdl automatically)
 * @param  port         port major number
 * @param  pin          pin number
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
static int pin_set(GPIO_hdl_t *hdl, u8_t port, u8_t pin)
{
        if (hdl == NULL) {
                int err = sys_module_get_instance(port, 0, cast(void **, &hdl));
                if (err) {
                        return err;
                }
        }

        if (hdl->major != port) {
                return EINVAL;
        }

        if (pin >= GPIO_HW[hdl->major].pins_count) {
                return EINVAL;
        }

        u32_t pin_mask = (1 << pin);

        if (hdl->OD & pin_mask) {
                *GPIO_HW[port].gpio_oe_clr = pin_mask;
        }

        *GPIO_HW[port].gpio_out_set = pin_mask;

        return ESUCC;
}

//==============================================================================
/**
 * @brief  Clear pin.
 *
 * @param  hdl          driver handle (can be NULL, load hdl automatically)
 * @param  port         port major number
 * @param  pin          pin number
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
static int pin_clear(GPIO_hdl_t *hdl, u8_t port, u8_t pin)
{
        if (hdl == NULL) {
                int err = sys_module_get_instance(port, 0, cast(void **, &hdl));
                if (err) {
                        return err;
                }
        }

        if (hdl->major != port) {
                return EINVAL;
        }

        if (pin >= GPIO_HW[hdl->major].pins_count) {
                return EINVAL;
        }

        u32_t pin_mask = (1 << pin);

        *GPIO_HW[port].gpio_out_clr = pin_mask;

        if (hdl->OD & pin_mask) {
                *GPIO_HW[port].gpio_oe_set = pin_mask;
        }

        return ESUCC;
}

//==============================================================================
/**
 * @brief  Toggle pin.
 *
 * @param  hdl          driver handle (can be NULL, load hdl automatically)
 * @param  port         port major number
 * @param  pin          pin number
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
static int pin_toggle(GPIO_hdl_t *hdl, u8_t port, u8_t pin)
{
        if (hdl == NULL) {
                int err = sys_module_get_instance(port, 0, cast(void **, &hdl));
                if (err) {
                        return err;
                }
        }

        if (hdl->major != port) {
                return EINVAL;
        }

        if (pin >= GPIO_HW[hdl->major].pins_count) {
                return EINVAL;
        }

        u32_t pin_mask = (1 << pin);

        if (hdl->OD & pin_mask) {
                if (*GPIO_HW[port].gpio_out & pin_mask) {
                        *GPIO_HW[port].gpio_oe_set = pin_mask;
                } else {
                        *GPIO_HW[port].gpio_oe_clr = pin_mask;
                }
        }

        *GPIO_HW[port].gpio_out_togl = pin_mask;

        return ESUCC;
}

//==============================================================================
/**
 * @brief  Get pin.
 *
 * @param  hdl          driver handle (can be NULL, load hdl automatically)
 * @param  port         port major number
 * @param  pin          pin number
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
static int pin_get(GPIO_hdl_t *hdl, u8_t port, u8_t pin, int *state)
{
        if (hdl == NULL) {
                int err = sys_module_get_instance(port, 0, cast(void **, &hdl));
                if (err) {
                        return err;
                }
        }

        if (hdl->major != port) {
                return EINVAL;
        }

        if (pin >= GPIO_HW[hdl->major].pins_count) {
                return EINVAL;
        }

        u32_t pin_mask = (1 << pin);

        *state = (*GPIO_HW[port].gpio_in & pin_mask) ? 1 : 0;

        return ESUCC;
}

//==============================================================================
/**
 * @brief  Set pin configuration.
 *
 * @param  hdl          driver handle (can be NULL, load hdl automatically)
 * @param  conf         configuration
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
static int pin_set_conf(GPIO_hdl_t *hdl, const GPIO_pin_in_port_conf_t *conf)
{
        if (hdl == NULL) {
                int err = sys_module_get_instance(conf->pin.port_idx, 0, cast(void **, &hdl));
                if (err) {
                        return err;
                }
        }

        if (hdl->major != conf->pin.port_idx) {
                return EINVAL;
        }

        if (conf->pin.pin_idx >= GPIO_HW[hdl->major].pins_count) {
                return EINVAL;
        }

        if (conf->mode >= GPIO_pin_mode_amount) {
                return EINVAL;
        }

        if (conf->mux >= GPIO_pin_mux_amount) {
                return EINVAL;
        }

        if (conf->speed >= GPIO_pin_speed_amount) {
                return EINVAL;
        }

        if (conf->pull >= GPIO_pin_pull_mode_amount) {
                return EINVAL;
        }

        sys_critical_section_begin();

        u32_t pin_mask = (1 << conf->pin.pin_idx);

        io_rw_32 *pad_io;
        io_rw_32 *gpio_ctrl;

        if (hdl->major == 0) {
                pad_io = &pads_bank0_hw->io[conf->pin.pin_idx];
                gpio_ctrl = &io_bank0_hw->io[conf->pin.pin_idx].ctrl;

        } else {
                pad_io = &pads_qspi_hw->io[conf->pin.pin_idx];
                gpio_ctrl = &io_qspi_hw->io[conf->pin.pin_idx].ctrl;
        }

        if (conf->mode == GPIO_pin_mode__output_OD) {
                SET_BIT(hdl->OD, pin_mask);
                SET_BIT(*pad_io, PADS_BANK0_GPIO0_IE_BITS);

                if (conf->state) {
                        *GPIO_HW[hdl->major].gpio_oe_clr = pin_mask;
                        *GPIO_HW[hdl->major].gpio_out_set = pin_mask;
                } else {
                        *GPIO_HW[hdl->major].gpio_oe_set = pin_mask;
                        *GPIO_HW[hdl->major].gpio_out_clr = pin_mask;
                }

        } else if (conf->mode == GPIO_pin_mode__output_PP) {
                CLEAR_BIT(hdl->OD, pin_mask);
                SET_BIT(*pad_io, PADS_BANK0_GPIO0_IE_BITS);

                *GPIO_HW[hdl->major].gpio_oe_set = pin_mask;

                if (conf->state) {
                        *GPIO_HW[hdl->major].gpio_out_set = pin_mask;
                } else {
                        *GPIO_HW[hdl->major].gpio_out_clr = pin_mask;
                }

        } else if (conf->mode == GPIO_pin_mode__input) {
                CLEAR_BIT(hdl->OD, pin_mask);
                *GPIO_HW[hdl->major].gpio_oe_clr = pin_mask;
                SET_BIT(*pad_io, PADS_BANK0_GPIO0_IE_BITS);

        } else {
                CLEAR_BIT(hdl->OD, pin_mask);
                CLEAR_BIT(*pad_io, PADS_BANK0_GPIO0_IE_BITS);
                *GPIO_HW[hdl->major].gpio_oe_clr = pin_mask;
        }

        CLEAR_BIT(*pad_io, PADS_BANK0_GPIO0_DRIVE_BITS);
        SET_BIT(*pad_io, (conf->drive_strength << PADS_BANK0_GPIO0_DRIVE_LSB) & PADS_BANK0_GPIO0_DRIVE_BITS);

        CLEAR_BIT(*pad_io, PADS_BANK0_GPIO0_SCHMITT_BITS);
        SET_BIT(*pad_io, (conf->schmitt_input << PADS_BANK0_GPIO0_SCHMITT_LSB) & PADS_BANK0_GPIO0_SCHMITT_BITS);

        if (conf->pull == GPIO_pin_pull_mode__bus_keeper) {
                SET_BIT(*pad_io, PADS_BANK0_GPIO0_PUE_BITS);
                SET_BIT(*pad_io, PADS_BANK0_GPIO0_PDE_BITS);

        } else if (conf->pull == GPIO_pin_pull_mode__pull_down) {
                CLEAR_BIT(*pad_io, PADS_BANK0_GPIO0_PUE_BITS);
                SET_BIT(*pad_io, PADS_BANK0_GPIO0_PDE_BITS);

        } else if (conf->pull == GPIO_pin_pull_mode__pull_up) {
                SET_BIT(*pad_io, PADS_BANK0_GPIO0_PUE_BITS);
                CLEAR_BIT(*pad_io, PADS_BANK0_GPIO0_PDE_BITS);

        } else {
                CLEAR_BIT(*pad_io, PADS_BANK0_GPIO0_PUE_BITS);
                CLEAR_BIT(*pad_io, PADS_BANK0_GPIO0_PDE_BITS);
        }

        if (conf->speed == GPIO_pin_speed__fast) {
                SET_BIT(*pad_io, PADS_BANK0_GPIO10_SLEWFAST_BITS);
        } else {
                CLEAR_BIT(*pad_io, PADS_BANK0_GPIO10_SLEWFAST_BITS);
        }

        CLEAR_BIT(*gpio_ctrl, IO_BANK0_GPIO0_CTRL_FUNCSEL_BITS);
        SET_BIT(*gpio_ctrl, (conf->mux << IO_BANK0_GPIO0_CTRL_FUNCSEL_LSB) & IO_BANK0_GPIO0_CTRL_FUNCSEL_BITS);

        sys_critical_section_end();

        return 0;
}

//==============================================================================
/**
 * @brief  Get pin configuration.
 *
 * @param  hdl          driver handle (can be NULL, load hdl automatically)
 * @param  conf         configuration
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
static int pin_get_conf(GPIO_hdl_t *hdl, GPIO_pin_in_port_conf_t *conf)
{
        if (hdl == NULL) {
                int err = sys_module_get_instance(conf->pin.port_idx, 0, cast(void **, &hdl));
                if (err) {
                        return err;
                }
        }

        if (hdl->major != conf->pin.port_idx) {
                return EINVAL;
        }

        if (conf->pin.pin_idx >= GPIO_HW[hdl->major].pins_count) {
                return EINVAL;
        }

        sys_critical_section_begin();

        u32_t pin_mask = (1 << conf->pin.pin_idx);

        io_rw_32 *pad_io;
        io_rw_32 *gpio_ctrl;

        if (hdl->major == 0) {
                pad_io = &pads_bank0_hw->io[conf->pin.pin_idx];
                gpio_ctrl = &io_bank0_hw->io[conf->pin.pin_idx].ctrl;

        } else {
                pad_io = &pads_qspi_hw->io[conf->pin.pin_idx];
                gpio_ctrl = &io_qspi_hw->io[conf->pin.pin_idx].ctrl;
        }

        if (hdl->OD & pin_mask) {
                conf->mode = GPIO_pin_mode__output_OD;

        } else if (*GPIO_HW[hdl->major].gpio_oe & pin_mask) {
                conf->mode = GPIO_pin_mode__output_PP;

        } else if (*pad_io & PADS_BANK0_GPIO0_IE_BITS) {
                conf->mode = GPIO_pin_mode__input;

        } else {
                conf->mode = GPIO_pin_mode__analog;
        }

        conf->mux = *gpio_ctrl & IO_BANK0_GPIO0_CTRL_FUNCSEL_BITS;

        if ((*pad_io & (PADS_BANK0_GPIO0_PUE_BITS | PADS_BANK0_GPIO0_PDE_BITS)) ==
                (PADS_BANK0_GPIO0_PUE_BITS | PADS_BANK0_GPIO0_PDE_BITS)) {
                conf->pull = GPIO_pin_pull_mode__bus_keeper;

        } else if (*pad_io & (PADS_BANK0_GPIO0_PDE_BITS)) {
                conf->pull = GPIO_pin_pull_mode__pull_down;

        } else if (*pad_io & (PADS_BANK0_GPIO0_PUE_BITS)) {
                conf->pull = GPIO_pin_pull_mode__pull_up;

        } else {
                conf->pull = GPIO_pin_pull_mode__none;
        }

        conf->drive_strength = ((*pad_io & PADS_BANK0_GPIO0_DRIVE_BITS) >> PADS_BANK0_GPIO0_DRIVE_LSB);
        conf->schmitt_input = ((*pad_io & PADS_BANK0_GPIO0_SCHMITT_BITS) >> PADS_BANK0_GPIO0_SCHMITT_LSB);
        conf->state = *GPIO_HW[hdl->major].gpio_out & pin_mask;

        if (*pad_io & PADS_BANK0_GPIO10_SLEWFAST_BITS) {
                conf->speed = GPIO_pin_speed__fast;
        } else {
                conf->speed = GPIO_pin_speed__slow;
        }

        sys_critical_section_end();

        return ESUCC;
}

//==============================================================================
/**
 * @brief Function set pin to 1.
 * @param port_idx      Port index
 * @param pin_idx       Pin index
 */
//==============================================================================
void _GPIO_DDI_set_pin(u8_t port_idx, u8_t pin_idx)
{
        pin_set(NULL, port_idx, pin_idx);
}

//==============================================================================
/**
 * @brief Function clear pin to 0.
 * @param port_idx      Port index.
 * @param pin_idx       Pin index.
 */
//==============================================================================
void _GPIO_DDI_clear_pin(u8_t port_idx, u8_t pin_idx)
{
        pin_clear(NULL, port_idx, pin_idx);
}

//==============================================================================
/**
 * @brief  Function get pin state.
 * @param  port_idx      Port index.
 * @param  pin_idx       Pin index.
 * @return Pin value. On error -1.
 */
//==============================================================================
i8_t _GPIO_DDI_get_pin(u8_t port_idx, u8_t pin_idx)
{
        int state;
        if (pin_get(NULL, port_idx, pin_idx, &state) == 0) {
                return state;
        } else {
                return -1;
        }
}

//==============================================================================
/**
 * @brief  Function set pin configuration.
 * @param  conf         pin configuration
 * @return Errno value.
 */
//==============================================================================
int _GPIO_DDI_set_pin_conf(const GPIO_pin_in_port_conf_t *conf)
{
        return pin_set_conf(NULL, conf);
}

//==============================================================================
/**
 * @brief  Function set pin configuration.
 * @param  conf         pin configuration
 * @return Errno value.
 */
//==============================================================================
int _GPIO_DDI_get_pin_conf(GPIO_pin_in_port_conf_t *conf)
{
        return pin_get_conf(NULL, conf);
}

/*==============================================================================
  End of file
==============================================================================*/
