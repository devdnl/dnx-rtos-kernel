/*==============================================================================
File    ltdc.c

Author  Daniel Zorychta

Brief   LCD-TFT Display Controller module of the POSIX simulator. The
        framebuffer is allocated in the system memory and is shown by the
        board panel in a web browser (simulator option --panel, the host
        sends changed lines of the framebuffer to the panel periodically).

        Copyright (C) 2026 Daniel Zorychta <daniel.zorychta@gmail.com>

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
#include "../ltdc_ioctl.h"
#include "posix/host/host.h"

/*==============================================================================
  Local macros
==============================================================================*/
#define BITS_IN_BYTE 8

/*==============================================================================
  Local object types
==============================================================================*/
typedef struct {
        u8_t major;
        u8_t minor;
        LTDC_config_t config;
        bool user_framebuffer;
        size_t framebuffer_size;
        void *framebuffer;
} LTDC_t;

/*==============================================================================
  Local function prototypes
==============================================================================*/
static int configure(LTDC_t *hdl, const LTDC_config_t *config);
static void release_framebuffer(LTDC_t *hdl);

/*==============================================================================
  Local object
==============================================================================*/
MODULE_NAME(LTDC);

static const u8_t PIXEL_FORMAT_TO_PIXEL_SIZE[] = {
        [LTDC_PIXEL_FORMAT__ARGB8888] = 4,
        [LTDC_PIXEL_FORMAT__RGB888] = 3,
        [LTDC_PIXEL_FORMAT__RGB565] = 2,
        [LTDC_PIXEL_FORMAT__ARGB1555] = 2,
        [LTDC_PIXEL_FORMAT__ARGB4444] = 2,
        [LTDC_PIXEL_FORMAT__L8] = 1,
        [LTDC_PIXEL_FORMAT__AL44] = 1,
        [LTDC_PIXEL_FORMAT__AL88] = 2,
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
API_MOD_INIT(LTDC, void **device_handle, u8_t major, u8_t minor, const void *config)
{
        int err = EFAULT;

        if (major == 0 && minor == 0) {
                err = sys_zalloc(sizeof(LTDC_t), device_handle);
                if (!err) {
                        LTDC_t *hdl = *device_handle;
                        hdl->major = major;
                        hdl->minor = minor;

                        if (config) {
                                err = configure(hdl, config);
                        }

                        if (err) {
                                sys_free(device_handle);
                        }
                }
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
API_MOD_RELEASE(LTDC, void *device_handle)
{
        LTDC_t *hdl = device_handle;
        release_framebuffer(hdl);
        return sys_free(&device_handle);
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
API_MOD_OPEN(LTDC, void *device_handle, u32_t flags)
{
        UNUSED_ARG2(device_handle, flags);
        return ESUCC;
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
API_MOD_CLOSE(LTDC, void *device_handle, bool force)
{
        UNUSED_ARG2(device_handle, force);
        return ESUCC;
}

//==============================================================================
/**
 * @brief Write data to device (framebuffer).
 *
 * @param[in ]          *device_handle          device allocated memory
 * @param[in ]          *src                    data source
 * @param[in ]           count                  number of bytes to write
 * @param[in ][out]     *fpos                   file position
 * @param[out]          *wrctr                  number of written bytes
 * @param[in ]           fattr                  file attributes
 *
 * @return One of errno value (errno.h).
 */
//==============================================================================
API_MOD_WRITE(LTDC, void *device_handle, const u8_t *src, size_t count, fpos_t *fpos, size_t *wrctr,
        struct vfs_fattr fattr)
{
        UNUSED_ARG1(fattr);

        LTDC_t *hdl = device_handle;

        if (*fpos < hdl->framebuffer_size) {
                size_t to_write = min(count, hdl->framebuffer_size - *fpos);
                memcpy(cast(u8_t *, hdl->framebuffer) + *fpos, src, to_write);
                *wrctr = to_write;
        } else {
                *wrctr = 0;
        }

        return ESUCC;
}

//==============================================================================
/**
 * @brief Read data from device (framebuffer).
 *
 * @param[in ]          *device_handle          device allocated memory
 * @param[out]          *dst                    data destination
 * @param[in ]           count                  number of bytes to read
 * @param[in ][out]     *fpos                   file position
 * @param[out]          *rdctr                  number of read bytes
 * @param[in ]           fattr                  file attributes
 *
 * @return One of errno value (errno.h).
 */
//==============================================================================
API_MOD_READ(LTDC, void *device_handle, u8_t *dst, size_t count, fpos_t *fpos, size_t *rdctr, struct vfs_fattr fattr)
{
        UNUSED_ARG1(fattr);

        LTDC_t *hdl = device_handle;

        if (*fpos < hdl->framebuffer_size) {
                size_t to_read = min(count, hdl->framebuffer_size - *fpos);
                memcpy(dst, cast(u8_t *, hdl->framebuffer) + *fpos, to_read);
                *rdctr = to_read;
        } else {
                *rdctr = 0;
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
API_MOD_IOCTL(LTDC, void *device_handle, int request, void *arg)
{
        LTDC_t *hdl = device_handle;

        int err = EINVAL;

        switch (request) {
        case IOCTL_LTDC__CONFIGURE:
                if (arg) {
                        err = configure(hdl, arg);
                }
                break;

        case IOCTL_LTDC__CONFIGURE_STR: {
                static const char *const PIXEL_FORMAT[] = {
                        [LTDC_PIXEL_FORMAT__ARGB8888] = "argb8888",
                        [LTDC_PIXEL_FORMAT__RGB888] = "rgb888",
                        [LTDC_PIXEL_FORMAT__RGB565] = "rgb565",
                        [LTDC_PIXEL_FORMAT__ARGB1555] = "argb1555",
                        [LTDC_PIXEL_FORMAT__ARGB4444] = "argb4444",
                        [LTDC_PIXEL_FORMAT__L8] = "l8",
                        [LTDC_PIXEL_FORMAT__AL44] = "al44",
                        [LTDC_PIXEL_FORMAT__AL88] = "al88",
                };

                if (arg) {
                        LTDC_config_t cfg = hdl->config;
                        int fmt = sys_opt_get_enum(arg, "pixel_format", PIXEL_FORMAT, ARRAY_SIZE(PIXEL_FORMAT),
                                cfg.pixel_format);
                        cfg.background_color_R = sys_opt_get_int(arg, "bg_r", cfg.background_color_R);
                        cfg.background_color_G = sys_opt_get_int(arg, "bg_g", cfg.background_color_G);
                        cfg.background_color_B = sys_opt_get_int(arg, "bg_b", cfg.background_color_B);

                        /* memory regions are not used in the simulator (mem_region is accepted) */
                        cfg.mem_region_name = NULL;

                        if (fmt >= 0) {
                                cfg.pixel_format = fmt;
                                err = configure(hdl, &cfg);
                        }
                }
                break;
        }

        case IOCTL_LTDC__GET_FEATURES:
                if (arg) {
                        LTDC_features_t *feat = arg;
                        feat->bits_per_pixel = PIXEL_FORMAT_TO_PIXEL_SIZE[hdl->config.pixel_format] * BITS_IN_BYTE;
                        feat->framebuffer_addr = hdl->framebuffer;
                        feat->framebuffer_size = hdl->framebuffer_size;
                        feat->pixel_format = hdl->config.pixel_format;
                        feat->width = __LTDC_WIDTH__;
                        feat->height = __LTDC_HEIGHT__;
                        err = ESUCC;
                }
                break;

        default:
                err = EBADRQC;
                break;
        }

        return err;
}

//==============================================================================
/**
 * @brief Flush device.
 *
 * @param[in ]          *device_handle          device allocated memory
 *
 * @return One of errno value (errno.h).
 */
//==============================================================================
API_MOD_FLUSH(LTDC, void *device_handle)
{
        UNUSED_ARG1(device_handle);
        return ESUCC;
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
API_MOD_STAT(LTDC, void *device_handle, struct vfs_dev_stat *device_stat)
{
        LTDC_t *hdl = device_handle;
        device_stat->st_size = hdl->framebuffer_size;
        return ESUCC;
}

//==============================================================================
/**
 * @brief  Detach framebuffer from the panel and release it.
 *
 * @param  hdl          device handle
 */
//==============================================================================
static void release_framebuffer(LTDC_t *hdl)
{
        if (hdl->framebuffer) {
                _host_lcd_attach(NULL, 0, 0, 0, 0, 0, 0);

                if (not hdl->user_framebuffer) {
                        sys_free(&hdl->framebuffer);
                }

                hdl->framebuffer = NULL;
        }
}

//==============================================================================
/**
 * @brief  LTDC configuration.
 *
 * @param  hdl          device handle
 * @param  config       device configuration
 *
 * @return One of errno value (errno.h).
 */
//==============================================================================
static int configure(LTDC_t *hdl, const LTDC_config_t *config)
{
        if (config->pixel_format >= _LTDC_PIXEL_FORMAT__AMOUNT) {
                dev_dbg(hdl, "invalid pixel format");
                return EINVAL;
        }

        size_t size = __LTDC_WIDTH__ * __LTDC_HEIGHT__ * PIXEL_FORMAT_TO_PIXEL_SIZE[config->pixel_format];

        if (config->framebuffer_addr && (config->framebuffer_size < size)) {
                printk("too small framebuffer");
                return EINVAL;
        }

        release_framebuffer(hdl);

        if (config->framebuffer_addr) {
                hdl->framebuffer = config->framebuffer_addr;
                hdl->user_framebuffer = true;
        } else {
                int err = sys_zalloc(size, &hdl->framebuffer);
                if (err) {
                        return err;
                }
                hdl->user_framebuffer = false;
        }

        hdl->framebuffer_size = size;
        hdl->config = *config;
        hdl->config.mem_region_name = NULL;

        dev_dbg(hdl, "framebuffer %ux%u px @ %p", __LTDC_WIDTH__, __LTDC_HEIGHT__, hdl->framebuffer);

        _host_lcd_attach(hdl->framebuffer, __LTDC_WIDTH__, __LTDC_HEIGHT__, config->pixel_format,
                         config->background_color_R, config->background_color_G, config->background_color_B);

        return ESUCC;
}

/*==============================================================================
  End of file
==============================================================================*/
