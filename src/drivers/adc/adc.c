/*==============================================================================
File    adc.c

Author  Daniel Zorychta

Brief   Analog-digital converter (common part of driver)

        Copyright (C) 2026 Daniel Zorychta <daniel.zorychta@gmail.com>

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
#include "adc.h"

/*==============================================================================
  Local macros
==============================================================================*/
#define DEFAULT_SAMPLE_NS 10000
#define DEFAULT_VREF_MV   3300
#define OVERSAMPLING_MAX  1024

/*==============================================================================
  Local object types
==============================================================================*/
typedef struct {
        kmtx_t *mtx;
        ADC_LLD_info_t lld;
        ADC_config_t conf;
        u8_t resolution;
        u16_t hw_ovs;
        u32_t sample_ns;
        u8_t major;
        u8_t minor;
} ADC_t;

/*==============================================================================
  Local function prototypes
==============================================================================*/
static int configure(ADC_t *hdl, const ADC_config_t *cfg);
static int convert(ADC_t *hdl, u8_t channel, u32_t *raw);

/*==============================================================================
  Local object
==============================================================================*/
MODULE_NAME(ADC);

/*==============================================================================
  Function definitions
==============================================================================*/

//==============================================================================
/**
 * @brief Initialize device.
 * @param[out]          **device_handle        device allocated memory
 * @param[in ]            major                major device number
 * @param[in ]            minor                minor device number
 * @param[in ]            config               optional module configuration
 * @return One of errno value (errno.h).
 */
//==============================================================================
API_MOD_INIT(ADC, void **device_handle, u8_t major, u8_t minor, const void *config)
{
        if (minor != 0) {
                return ENODEV;
        }

        int err = sys_zalloc(sizeof(ADC_t), device_handle);
        if (err) {
                return err;
        }

        ADC_t *hdl = *device_handle;
        hdl->major = major;
        hdl->minor = minor;

        err = sys_mutex_create(KMTX_TYPE_NORMAL, &hdl->mtx);
        if (!err) {
                err = _ADC_LLD__init(major, &hdl->lld);
                if (!err) {
                        static const ADC_config_t DEFAULT = {
                                .resolution = 0,
                                .oversampling = 1,
                                .vref_mV = DEFAULT_VREF_MV,
                                .sample_time_ns = DEFAULT_SAMPLE_NS,
                        };

                        err = configure(hdl, config ? config : &DEFAULT);
                        if (err) {
                                _ADC_LLD__deinit(major);
                        }
                }

                if (err) {
                        sys_mutex_destroy(hdl->mtx);
                }
        }

        if (err) {
                sys_free(device_handle);
        }

        return err;
}

//==============================================================================
/**
 * @brief Release device.
 * @param[in ]          *device_handle          device allocated memory
 * @return One of errno value (errno.h).
 */
//==============================================================================
API_MOD_RELEASE(ADC, void *device_handle)
{
        ADC_t *hdl = device_handle;

        int err = sys_mutex_lock(hdl->mtx, 0);
        if (!err) {
                _ADC_LLD__deinit(hdl->major);
                kmtx_t *mtx = hdl->mtx;
                sys_mutex_unlock(mtx);
                sys_mutex_destroy(mtx);
                sys_free(&device_handle);
        }

        return err;
}

//==============================================================================
/**
 * @brief Open device.
 * @param[in ]          *device_handle          device allocated memory
 * @param[in ]           flags                  file operation flags
 * @return One of errno value (errno.h).
 */
//==============================================================================
API_MOD_OPEN(ADC, void *device_handle, u32_t flags)
{
        UNUSED_ARG2(device_handle, flags);
        return ESUCC;
}

//==============================================================================
/**
 * @brief Close device.
 * @param[in ]          *device_handle          device allocated memory
 * @param[in ]           force                  device force close (true)
 * @return One of errno value (errno.h).
 */
//==============================================================================
API_MOD_CLOSE(ADC, void *device_handle, bool force)
{
        UNUSED_ARG2(device_handle, force);
        return ESUCC;
}

//==============================================================================
/**
 * @brief Write data to device.
 * @return ENOTSUP.
 */
//==============================================================================
API_MOD_WRITE(ADC, void *device_handle, const u8_t *src, size_t count, fpos_t *fpos, size_t *wrcnt,
        struct vfs_fattr fattr)
{
        UNUSED_ARG6(device_handle, src, count, fpos, wrcnt, fattr);
        return ENOTSUP;
}

//==============================================================================
/**
 * @brief Read data from device: position / 2 is a channel, 16-bit samples.
 * @param[in ]          *device_handle          device allocated memory
 * @param[out]          *dst                    data destination
 * @param[in ]           count                  number of bytes to read
 * @param[in ][out]     *fpos                   file position
 * @param[out]          *rdcnt                  number of read bytes
 * @param[in ]           fattr                  file attributes
 * @return One of errno value (errno.h).
 */
//==============================================================================
API_MOD_READ(ADC, void *device_handle, u8_t *dst, size_t count, fpos_t *fpos, size_t *rdcnt, struct vfs_fattr fattr)
{
        UNUSED_ARG1(fattr);

        ADC_t *hdl = device_handle;

        if (*fpos % sizeof(u16_t)) {
                return EINVAL;
        }

        int err = sys_mutex_lock(hdl->mtx, _WAIT_FOREVER_MS);
        if (!err) {
                u32_t ch = *fpos / sizeof(u16_t);

                while (!err && (count >= sizeof(u16_t)) && (ch < hdl->lld.channels)) {
                        u32_t raw;
                        err = convert(hdl, ch, &raw);
                        if (!err) {
                                u16_t sample = raw;
                                memcpy(dst, &sample, sizeof(sample));
                                dst += sizeof(sample);
                                count -= sizeof(sample);
                                *rdcnt += sizeof(sample);
                                ch++;
                        }
                }

                sys_mutex_unlock(hdl->mtx);
        }

        return err;
}

//==============================================================================
/**
 * @brief IO control.
 * @param[in ]          *device_handle          device allocated memory
 * @param[in ]           request                request
 * @param[in ][out]     *arg                    request's argument
 * @return One of errno value (errno.h).
 */
//==============================================================================
API_MOD_IOCTL(ADC, void *device_handle, int request, void *arg)
{
        ADC_t *hdl = device_handle;

        if (!arg) {
                return EINVAL;
        }

        int err = sys_mutex_lock(hdl->mtx, _WAIT_FOREVER_MS);
        if (err) {
                return err;
        }

        switch (request) {
        case IOCTL_ADC__CONFIGURE:
                err = configure(hdl, arg);
                break;

        case IOCTL_ADC__CONFIGURE_STR: {
                ADC_config_t cfg = hdl->conf;
                cfg.resolution = sys_opt_get_int(arg, "resolution", cfg.resolution);
                cfg.oversampling = sys_opt_get_int(arg, "oversampling", cfg.oversampling);
                cfg.vref_mV = sys_opt_get_int(arg, "vref_mv", cfg.vref_mV);
                cfg.sample_time_ns = sys_opt_get_int(arg, "sample_time_ns", cfg.sample_time_ns);
                err = configure(hdl, &cfg);
                break;
        }

        case IOCTL_ADC__GET_CONFIGURATION:
                *cast(ADC_config_t *, arg) = hdl->conf;
                break;

        case IOCTL_ADC__GET_INFO: {
                ADC_info_t *info = arg;
                info->resolution_max = hdl->lld.resolution_max;
                info->resolution = hdl->resolution;
                info->channels = hdl->lld.channels;
                info->temp = hdl->lld.temp_channel != _ADC_NO_CHANNEL;
                info->vrefint = hdl->lld.vrefint_channel != _ADC_NO_CHANNEL;
                info->vref_mV = hdl->conf.vref_mV;
                info->oversampling = hdl->conf.oversampling;
                info->hw_oversampling = hdl->hw_ovs > 1;
                info->sample_time_ns = hdl->sample_ns;
                break;
        }

        case IOCTL_ADC__READ_CHANNEL: {
                ADC_measurement_t *m = arg;
                err = convert(hdl, m->channel, &m->raw);
                if (!err) {
                        u64_t max = (1ULL << hdl->resolution) - 1;
                        m->uV = (cast(u64_t, m->raw) * hdl->conf.vref_mV * 1000) / max;
                }
                break;
        }

        case IOCTL_ADC__READ_TEMPERATURE: {
                u32_t raw;
                err = convert(hdl, ADC_CHANNEL__TEMP, &raw);
                if (!err) {
                        *cast(i32_t *, arg) = _ADC_LLD__temperature(hdl->major, raw, hdl->resolution,
                                hdl->conf.vref_mV);
                }
                break;
        }

        default:
                err = EBADRQC;
                break;
        }

        sys_mutex_unlock(hdl->mtx);

        return err;
}

//==============================================================================
/**
 * @brief Flush device.
 * @param[in ]          *device_handle          device allocated memory
 * @return One of errno value (errno.h).
 */
//==============================================================================
API_MOD_FLUSH(ADC, void *device_handle)
{
        UNUSED_ARG1(device_handle);
        return ESUCC;
}

//==============================================================================
/**
 * @brief Device information.
 * @param[in ]          *device_handle          device allocated memory
 * @param[out]          *device_stat            device status
 * @return One of errno value (errno.h).
 */
//==============================================================================
API_MOD_STAT(ADC, void *device_handle, struct vfs_dev_stat *device_stat)
{
        ADC_t *hdl = device_handle;
        device_stat->st_size = hdl->lld.channels * sizeof(u16_t);
        return ESUCC;
}

//==============================================================================
/**
 * @brief  Function configures converter.
 * @param  hdl          driver handle
 * @param  cfg          configuration
 * @return One of errno value (errno.h).
 */
//==============================================================================
static int configure(ADC_t *hdl, const ADC_config_t *cfg)
{
        u16_t ovs = cfg->oversampling;
        u8_t res = cfg->resolution ? cfg->resolution : hdl->lld.resolution_max;

        if ((ovs == 0) || (ovs > OVERSAMPLING_MAX) || (ovs & (ovs - 1)) || (cfg->vref_mV == 0) ||
                (res > hdl->lld.resolution_max)) {
                return EINVAL;
        }

        u16_t hw_ovs = (ovs <= hdl->lld.oversampling_hw_max) ? ovs : 1;

        int err = _ADC_LLD__configure(hdl->major, res, cfg->sample_time_ns, hw_ovs, &hdl->sample_ns);
        if (!err) {
                hdl->conf = *cfg;
                hdl->resolution = res;
                hdl->hw_ovs = hw_ovs;
        }

        return err;
}

//==============================================================================
/**
 * @brief  Function converts channel (with software oversampling).
 * @param  hdl          driver handle
 * @param  channel      channel number or internal channel
 * @param  raw          result
 * @return One of errno value (errno.h).
 */
//==============================================================================
static int convert(ADC_t *hdl, u8_t channel, u32_t *raw)
{
        if (channel == ADC_CHANNEL__TEMP) {
                channel = hdl->lld.temp_channel;
        } else if (channel == ADC_CHANNEL__VREFINT) {
                channel = hdl->lld.vrefint_channel;
        } else if (channel >= hdl->lld.channels) {
                return ENODEV;
        }

        if (channel == _ADC_NO_CHANNEL) {
                return ENODEV;
        }

        u16_t n = (hdl->hw_ovs > 1) ? 1 : hdl->conf.oversampling;
        u32_t sum = 0;

        for (u16_t i = 0; i < n; i++) {
                u32_t val;
                int err = _ADC_LLD__convert(hdl->major, channel, &val);
                if (err) {
                        return err;
                }
                sum += val;
        }

        *raw = (sum + (n / 2)) / n;

        return ESUCC;
}

/*==============================================================================
  End of file
==============================================================================*/
