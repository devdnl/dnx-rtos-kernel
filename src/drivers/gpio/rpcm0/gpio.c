/*==============================================================================
@file    gpio.c

@author  Daniel Zorychta

@brief   This driver support GPIO. GPIO driver not provide any interface
         functions. All operations on ports should be made direct via definitions
         (much faster). When operation on a ports are needed please write own
         driver which controls pins directly and register it in the VFS if
         needed.

@note    Copyright (C) 2024  Daniel Zorychta <daniel.zorychta@gmail.com>

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
#include "rpcm0/gpio_cfg.h"
#include "../gpio_ioctl.h"

/*==============================================================================
  Local symbolic constants/macros
==============================================================================*/
#define PINS_PER_PORT 32

/*==============================================================================
  Local types, enums definitions
==============================================================================*/

/*==============================================================================
  Local function prototypes
==============================================================================*/

/*==============================================================================
  Local object definitions
==============================================================================*/

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

        return ENODEV;
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
        const struct GPIO_reg *hdl = device_handle;

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
API_MOD_WRITE(GPIO,
              void             *device_handle,
              const u8_t       *src,
              size_t            count,
              fpos_t           *fpos,
              size_t           *wrcnt,
              struct vfs_fattr  fattr)
{
        UNUSED_ARG2(fpos, fattr);

        const struct GPIO_reg *hdl = device_handle;

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
API_MOD_READ(GPIO,
             void            *device_handle,
             u8_t            *dst,
             size_t           count,
             fpos_t          *fpos,
             size_t          *rdcnt,
             struct vfs_fattr fattr)
{
        UNUSED_ARG2(fpos, fattr);

        const struct GPIO_reg *hdl = device_handle;

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

        const struct GPIO_reg *hdl = device_handle;

        if (arg) {
                switch (request) {
                case IOCTL_GPIO__SET_PIN: {
                        u8_t pin = *cast(u8_t*, arg);
                        if (pin < PINS_PER_PORT) {
                                err = ESUCC;
                        }
                        break;
                }

                case IOCTL_GPIO__CLEAR_PIN: {
                        u8_t pin = *cast(u8_t*, arg);
                        if (pin < PINS_PER_PORT) {
                                err = ESUCC;
                        }
                        break;
                }

                case IOCTL_GPIO__TOGGLE_PIN: {
                        u8_t pin = *cast(u8_t*, arg);
                        if (pin < PINS_PER_PORT) {
                                err = ESUCC;
                        }
                        break;
                }

                case IOCTL_GPIO__SET_PIN_STATE: {
                        GPIO_pin_state_t *pinstate = arg;
                        if (pinstate->pin_idx < PINS_PER_PORT) {
                                if (pinstate->state) {
                                } else {
                                }
                                err = ESUCC;
                        }
                        break;
                }

                case IOCTL_GPIO__GET_PIN_STATE: {
                        GPIO_pin_state_t *pinstate = arg;
                        if (pinstate->pin_idx < PINS_PER_PORT) {
                                err = ESUCC;
                        }
                        break;
                }

                case IOCTL_GPIO__SET_PIN_IN_PORT: {
                        GPIO_pin_in_port_t *pin = arg;
                        if ((pin->port_idx < 1) && (pin->pin_idx < 16)) {
                                err = ESUCC;
                        }
                        break;
                }

                case IOCTL_GPIO__CLEAR_PIN_IN_PORT: {
                        GPIO_pin_in_port_t *pin = arg;
                        if ((pin->port_idx < 1) && (pin->pin_idx < 16)) {
                                err = ESUCC;
                        }
                        break;
                }

                case IOCTL_GPIO__TOGGLE_PIN_IN_PORT: {
                        GPIO_pin_in_port_t *pin = arg;
                        if ((pin->port_idx < 1) && (pin->pin_idx < 16)) {
                                err = ESUCC;
                        }
                        break;
                }

                case IOCTL_GPIO__SET_PIN_IN_PORT_STATE: {
                        GPIO_pin_in_port_state_t *pin = arg;
                        if ((pin->pin.port_idx < 1) && (pin->pin.pin_idx < 16)) {
                                if (pin->state) {
                                } else {
                                }
                                err = ESUCC;
                        }
                        break;
                }

                case IOCTL_GPIO__GET_PIN_IN_PORT_STATE: {
                        GPIO_pin_in_port_state_t *pin = arg;
                        if ((pin->pin.port_idx < 1) && (pin->pin.pin_idx < 16)) {
                                err = ESUCC;
                        }
                        break;
                }

                case IOCTL_GPIO__SET_PIN_IN_PORT_CONF:
                        break;

                case IOCTL_GPIO__GET_PIN_IN_PORT_CONF:
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
        const struct GPIO_reg *hdl = device_handle;

        device_stat->st_size  = 4;
        device_stat->st_major = 0;
        device_stat->st_minor = 0;

        return ESUCC;
}

/*==============================================================================
  End of file
==============================================================================*/
