/*==============================================================================
@file    gpio.c

@author  Daniel Zorychta

@brief   Virtual GPIO driver (POSIX simulator). Pins are stored in memory,
         input state is the same as output state.

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
#include "drivers/driver.h"
#include "posix/gpio_cfg.h"
#include "../gpio_ioctl.h"
#include "../gpio_ddi.h"

/*==============================================================================
  Local symbolic constants/macros
==============================================================================*/
#define NUMBER_OF_PORTS __GPIO_POSIX_NUMBER_OF_PORTS__
#define PINS_IN_PORT    _GPIO_POSIX_PINS_IN_PORT

/*==============================================================================
  Local types, enums definitions
==============================================================================*/
typedef struct {
        u8_t major;
        u8_t minor;
} GPIO_hdl_t;

typedef struct {
        u32_t state;
        GPIO_pin_in_port_conf_t conf[PINS_IN_PORT];
} port_t;

/*==============================================================================
  Local function prototypes
==============================================================================*/
static int pin_set_state(u8_t port_idx, u8_t pin_idx, int state);
static int pin_toggle(u8_t port_idx, u8_t pin_idx);
static int pin_get(u8_t port_idx, u8_t pin_idx, int *state);
static int pin_set_conf(const GPIO_pin_in_port_conf_t *conf);
static int pin_get_conf(GPIO_pin_in_port_conf_t *conf);

/*==============================================================================
  Local object definitions
==============================================================================*/
MODULE_NAME(GPIO);

static port_t port[NUMBER_OF_PORTS];

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

        if ((major >= NUMBER_OF_PORTS) or (minor != 0)) {
                return ENODEV;
        }

        int err = sys_zalloc(sizeof(GPIO_hdl_t), device_handle);
        if (!err) {
                GPIO_hdl_t *hdl = *device_handle;
                hdl->major = major;
                hdl->minor = minor;

                port[major].state = 0;

                for (u8_t i = 0; i < PINS_IN_PORT; i++) {
                        GPIO_pin_in_port_conf_t *conf = &port[major].conf[i];
                        memset(conf, 0, sizeof(*conf));
                        conf->pin.port_idx = major;
                        conf->pin.pin_idx = i;
                        conf->mode = GPIO_pin_mode__input;
                        conf->mux = GPIO_pin_mux__disable;
                }
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
        return sys_free(&device_handle);
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
 * @brief Write data to device (32-bit words are written to port).
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

        for (size_t i = 0; i < words; i++) {
                u32_t value;
                memcpy(&value, &src[i * sizeof(u32_t)], sizeof(u32_t));

                sys_critical_section_begin();
                port[hdl->major].state = value;
                sys_critical_section_end();
        }

        *wrcnt = words * sizeof(u32_t);

        return ESUCC;
}

//==============================================================================
/**
 * @brief Read data from device (port state as 32-bit words).
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

        for (size_t i = 0; i < words; i++) {
                u32_t value = port[hdl->major].state;
                memcpy(&dst[i * sizeof(u32_t)], &value, sizeof(u32_t));
        }

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
 * @return One of errno value (errno.h)
 */
//==============================================================================
API_MOD_IOCTL(GPIO, void *device_handle, int request, void *arg)
{
        GPIO_hdl_t *hdl = device_handle;

        int err = EINVAL;

        if (arg) {
                switch (request) {
                case IOCTL_GPIO__SET_PIN:
                        err = pin_set_state(hdl->major, *cast(u8_t *, arg), 1);
                        break;

                case IOCTL_GPIO__CLEAR_PIN:
                        err = pin_set_state(hdl->major, *cast(u8_t *, arg), 0);
                        break;

                case IOCTL_GPIO__TOGGLE_PIN:
                        err = pin_toggle(hdl->major, *cast(u8_t *, arg));
                        break;

                case IOCTL_GPIO__SET_PIN_STATE: {
                        GPIO_pin_state_t *pinstate = arg;
                        err = pin_set_state(hdl->major, pinstate->pin_idx, pinstate->state);
                        break;
                }

                case IOCTL_GPIO__GET_PIN_STATE: {
                        GPIO_pin_state_t *pinstate = arg;
                        err = pin_get(hdl->major, pinstate->pin_idx, &pinstate->state);
                        break;
                }

                case IOCTL_GPIO__SET_PIN_IN_PORT: {
                        GPIO_pin_in_port_t *pin = arg;
                        err = pin_set_state(pin->port_idx, pin->pin_idx, 1);
                        break;
                }

                case IOCTL_GPIO__CLEAR_PIN_IN_PORT: {
                        GPIO_pin_in_port_t *pin = arg;
                        err = pin_set_state(pin->port_idx, pin->pin_idx, 0);
                        break;
                }

                case IOCTL_GPIO__TOGGLE_PIN_IN_PORT: {
                        GPIO_pin_in_port_t *pin = arg;
                        err = pin_toggle(pin->port_idx, pin->pin_idx);
                        break;
                }

                case IOCTL_GPIO__SET_PIN_IN_PORT_STATE: {
                        GPIO_pin_in_port_state_t *pin = arg;
                        err = pin_set_state(pin->pin.port_idx, pin->pin.pin_idx, pin->state);
                        break;
                }

                case IOCTL_GPIO__GET_PIN_IN_PORT_STATE: {
                        GPIO_pin_in_port_state_t *pin = arg;
                        err = pin_get(pin->pin.port_idx, pin->pin.pin_idx, &pin->state);
                        break;
                }

                case IOCTL_GPIO__SET_PIN_IN_PORT_CONF:
                        err = pin_set_conf(arg);
                        break;

                case IOCTL_GPIO__GET_PIN_IN_PORT_CONF:
                        err = pin_get_conf(arg);
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
        UNUSED_ARG1(device_handle);
        device_stat->st_size = sizeof(u32_t);
        return ESUCC;
}

//==============================================================================
/**
 * @brief  Check if pin address is correct.
 *
 * @param  port_idx     port index
 * @param  pin_idx      pin index
 *
 * @return If pin exists true is returned.
 */
//==============================================================================
static bool is_pin_valid(u8_t port_idx, u8_t pin_idx)
{
        return (port_idx < NUMBER_OF_PORTS) && (pin_idx < PINS_IN_PORT);
}

//==============================================================================
/**
 * @brief  Set pin state.
 *
 * @param  port_idx     port index
 * @param  pin_idx      pin index
 * @param  state        pin state
 *
 * @return One of errno value.
 */
//==============================================================================
static int pin_set_state(u8_t port_idx, u8_t pin_idx, int state)
{
        if (not is_pin_valid(port_idx, pin_idx)) {
                return EINVAL;
        }

        sys_critical_section_begin();
        if (state) {
                port[port_idx].state |= (1UL << pin_idx);
        } else {
                port[port_idx].state &= ~(1UL << pin_idx);
        }
        sys_critical_section_end();

        return ESUCC;
}

//==============================================================================
/**
 * @brief  Toggle pin state.
 *
 * @param  port_idx     port index
 * @param  pin_idx      pin index
 *
 * @return One of errno value.
 */
//==============================================================================
static int pin_toggle(u8_t port_idx, u8_t pin_idx)
{
        if (not is_pin_valid(port_idx, pin_idx)) {
                return EINVAL;
        }

        sys_critical_section_begin();
        port[port_idx].state ^= (1UL << pin_idx);
        sys_critical_section_end();

        return ESUCC;
}

//==============================================================================
/**
 * @brief  Get pin state.
 *
 * @param  port_idx     port index
 * @param  pin_idx      pin index
 * @param  state        pin state
 *
 * @return One of errno value.
 */
//==============================================================================
static int pin_get(u8_t port_idx, u8_t pin_idx, int *state)
{
        if (not is_pin_valid(port_idx, pin_idx)) {
                return EINVAL;
        }

        *state = (port[port_idx].state & (1UL << pin_idx)) ? 1 : 0;

        return ESUCC;
}

//==============================================================================
/**
 * @brief  Set pin configuration.
 *
 * @param  conf         pin configuration
 *
 * @return One of errno value.
 */
//==============================================================================
static int pin_set_conf(const GPIO_pin_in_port_conf_t *conf)
{
        if (not is_pin_valid(conf->pin.port_idx, conf->pin.pin_idx)) {
                return EINVAL;
        }

        port[conf->pin.port_idx].conf[conf->pin.pin_idx] = *conf;

        return pin_set_state(conf->pin.port_idx, conf->pin.pin_idx, conf->state);
}

//==============================================================================
/**
 * @brief  Get pin configuration.
 *
 * @param  conf         pin configuration (pin address is input)
 *
 * @return One of errno value.
 */
//==============================================================================
static int pin_get_conf(GPIO_pin_in_port_conf_t *conf)
{
        if (not is_pin_valid(conf->pin.port_idx, conf->pin.pin_idx)) {
                return EINVAL;
        }

        GPIO_pin_in_port_t pin = conf->pin;
        *conf = port[pin.port_idx].conf[pin.pin_idx];
        conf->pin = pin;

        int state = 0;
        pin_get(pin.port_idx, pin.pin_idx, &state);
        conf->state = state;

        return ESUCC;
}

//==============================================================================
/**
 * @brief  Function set pin to 1.
 *
 * @param  port_idx     port index
 * @param  pin_idx      pin index
 */
//==============================================================================
void _GPIO_DDI_set_pin(u8_t port_idx, u8_t pin_idx)
{
        pin_set_state(port_idx, pin_idx, 1);
}

//==============================================================================
/**
 * @brief  Function clear pin to 0.
 *
 * @param  port_idx     port index
 * @param  pin_idx      pin index
 */
//==============================================================================
void _GPIO_DDI_clear_pin(u8_t port_idx, u8_t pin_idx)
{
        pin_set_state(port_idx, pin_idx, 0);
}

//==============================================================================
/**
 * @brief  Function get pin state.
 *
 * @param  port_idx     port index
 * @param  pin_idx      pin index
 *
 * @return Pin value. On error -1.
 */
//==============================================================================
i8_t _GPIO_DDI_get_pin(u8_t port_idx, u8_t pin_idx)
{
        int state = -1;
        if (pin_get(port_idx, pin_idx, &state) != ESUCC) {
                return -1;
        }
        return state;
}

//==============================================================================
/**
 * @brief  Function set pin configuration.
 *
 * @param  conf         pin configuration
 *
 * @return One of errno value.
 */
//==============================================================================
int _GPIO_DDI_set_pin_conf(const GPIO_pin_in_port_conf_t *conf)
{
        return pin_set_conf(conf);
}

//==============================================================================
/**
 * @brief  Function get pin configuration.
 *
 * @param  conf         pin configuration
 *
 * @return One of errno value.
 */
//==============================================================================
int _GPIO_DDI_get_pin_conf(GPIO_pin_in_port_conf_t *conf)
{
        return pin_get_conf(conf);
}

/*==============================================================================
  End of file
==============================================================================*/
