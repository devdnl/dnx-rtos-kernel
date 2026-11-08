/*==============================================================================
@file    driver.h

@author  Daniel Zorychta

@brief   This function provide all required function needed to write modules.

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

/**
 * @defgroup driver-h "drivers/driver.h"
 *
 * This library is used by each driver and contains driver-specific
 * functions and @subpage sysapi-h. This is main system library that shall
 * be used by drivers.
 *
 * @{
 */

#ifndef _DRIVER_H_
#define _DRIVER_H_

#ifdef __cplusplus
extern "C" {
#endif

/*==============================================================================
  Include files
==============================================================================*/
#include "kernel/sysapi.h"
#include "drivers/drvif.h"
#include "lib/vt100.h"

/*==============================================================================
  Exported symbolic constants/macros
==============================================================================*/
#undef errno

#ifndef DOXYGEN /* Doxygen documentation in sysapi.h */
#undef sys_zalloc
#define sys_zalloc(size_t__size, void__ppmem) \
        drv_mem_alloc(_module_name_, size_t__size, 0, NULL, _MM_FLAG__DMA, _MM_FLAG__DMA, true, void__ppmem)

#undef sys_malloc
#define sys_malloc(size_t__size, void__ppmem) \
        drv_mem_alloc(_module_name_, size_t__size, 0, NULL, _MM_FLAG__DMA, _MM_FLAG__DMA, false, void__ppmem)

/*
 * sys_zalloc2()/sys_malloc2(): allocation with selected alignment (power of 2,
 * e.g. _CPUCTL_CACHE_ALIGN for DMA buffers; 0: default heap alignment) in
 * preferred memory regions (NULL terminated list of region names, NULL: any)
 * with required features (_MM_FLAG__*). Aligned blocks have size rounded up
 * to the alignment, so they do not share cache lines with other blocks.
 * Memory is released by sys_free().
 */
#undef sys_zalloc2
#define sys_zalloc2(size_t__size, size_t__align, prefreg_name, required_features, required_features_mask, void__ppmem) \
        drv_mem_alloc(_module_name_, size_t__size, size_t__align, prefreg_name, required_features,                     \
                required_features_mask, true, void__ppmem)

#undef sys_malloc2
#define sys_malloc2(size_t__size, size_t__align, prefreg_name, required_features, required_features_mask, void__ppmem) \
        drv_mem_alloc(_module_name_, size_t__size, size_t__align, prefreg_name, required_features,                     \
                required_features_mask, false, void__ppmem)

#undef sys_free
#define sys_free(void__ppmem) drv_mem_free(_module_name_, void__ppmem)

#undef drv_get_instance
#define drv_get_instance(u8_t__major, u8_t__minor, void_pp__mem) \
        drv_module_get_instance(_module_name_, u8_t__major, u8_t__minor, void_pp__mem)
#endif /* DOXYGEN */

#ifndef DOXYGEN
#ifdef __cplusplus
#define MODULE_NAME(modname)                         \
        static const char *_module_name_ = #modname; \
        inline void *operator new(size_t size)       \
        {                                            \
                void *mem = NULL;                    \
                sys_malloc(size, &mem);              \
                return mem;                          \
        }                                            \
        inline void *operator new[](size_t size)     \
        {                                            \
                void *mem = NULL;                    \
                sys_malloc(size, &mem);              \
                return mem;                          \
        }                                            \
        inline void operator delete(void *ptr)       \
        {                                            \
                sys_free(&ptr);                      \
        }                                            \
        inline void operator delete[](void *ptr)     \
        {                                            \
                sys_free(&ptr);                      \
        }
#define _MODULE_EXTERN_C extern "C"
#else
#define MODULE_NAME(modname) static const char *_module_name_ = #modname
#define GET_MODULE_NAME()    _module_name_
#define _MODULE_EXTERN_C
#endif /* __cplusplus */
#else  /* defined(DOXYGEN) */
/**
 * @brief Macro set name of module.
 *
 * Macro is used in each source file of driver to set module name.
 *
 * @note Macro can be used only by driver code.
 *
 * @param modname       module name
 *
 * @b Example
 * @code
        // ...
        MODULE_NAME(UART);
        // ...
   @endcode
 */
#define MODULE_NAME(modname)
#endif /* DOXYGEN */

#ifdef DOXYGEN
/**
 * @brief Macro creates unique name of driver initialize function.
 *
 * Function created by this macro is called by system when driver is initialized.
 * Each driver with major and minor number can create own memory region.
 *
 * @note Macro can be used only by driver code.
 *
 * @param modname       module name
 * @param device_handle [<b>void **</b>]        driver memory (output)
 * @param major         [<b>u8_t</b>]           device major number
 * @param minor         [<b>u8_t</b>]           device minor number
 * @return One of @ref errno value.
 */
#define API_MOD_INIT(modname, device_handle, major, minor)
#else
#define API_MOD_INIT(modname, ...) _MODULE_EXTERN_C int _##modname##_init(__VA_ARGS__)
#endif

#ifdef DOXYGEN
/**
 * @brief Macro creates unique name of driver release function.
 *
 * Function created by this macro is called by system when driver is released.
 *
 * @note Macro can be used only by driver code.
 *
 * @param modname       module name
 * @param device_handle [<b>void *</b>]         memory region allocated by driver
 * @return One of @ref errno value.
 */
#define API_MOD_RELEASE(modname, device_handle)
#else
#define API_MOD_RELEASE(modname, ...) _MODULE_EXTERN_C int _##modname##_release(__VA_ARGS__)
#endif

#ifdef DOXYGEN
/**
 * @brief Macro creates unique name of driver open function.
 *
 * Function created by this macro is called by system when corresponding device
 * has to be opened.
 *
 * @note Macro can be used only by driver code.
 *
 * @param modname       module name
 * @param device_handle [<b>void *</b>]         memory region allocated by driver
 * @param flags         [<b>u32_t</b>]          flags
 * @return One of @ref errno value.
 *
 * @see O_RDONLY, O_WRONLY, O_RDWR
 */
#define API_MOD_OPEN(modname, device_handle, flags)
#else
#define API_MOD_OPEN(modname, ...) _MODULE_EXTERN_C int _##modname##_open(__VA_ARGS__)
#endif

#ifdef DOXYGEN
/**
 * @brief Macro creates unique name of driver close function.
 *
 * Function created by this macro is called by system when corresponding device
 * has to be closed. System can force close by setting <i>force</i> flag to
 * true. In this case driver shall close device safe and immediately.
 *
 * @note Macro can be used only by driver code.
 *
 * @param modname       module name
 * @param device_handle [<b>void *</b>]         memory region allocated by driver
 * @param force         [<b>bool</b>]           force close driver (system request)
 * @return One of @ref errno value.
 */
#define API_MOD_CLOSE(modname, device_handle, force)
#else
#define API_MOD_CLOSE(modname, ...) _MODULE_EXTERN_C int _##modname##_close(__VA_ARGS__)
#endif

#ifdef DOXYGEN
/**
 * @brief Macro creates unique name of driver write function.
 *
 * Function created by this macro is called by system when corresponding device
 * has to be written.
 *
 * @note Macro can be used only by driver code.
 *
 * @param modname       module name
 * @param device_handle [<b>void *</b>]         memory region allocated by driver
 * @param src           [<b>const u8_t *</b>]   source buffer
 * @param count         [<b>size_t</b>]         bytes to write
 * @param fpos          [<b>fpos_t *</b>]       file position indicator (can be modified)
 * @param wrcnt         [<b>size_t *</b>]       number of wrote bytes
 * @param fattr         [<b>struct vfs_fattr</b>] file access attributes
 * @return One of @ref errno value.
 *
 * @see struct vfs_fattr
 */
#define API_MOD_WRITE(modname, device_handle, src, count, fpos, wrcnt, fattr)
#else
#define API_MOD_WRITE(modname, ...) _MODULE_EXTERN_C int _##modname##_write(__VA_ARGS__)
#endif

#ifdef DOXYGEN
/**
 * @brief Macro creates unique name of driver read function.
 *
 * Function created by this macro is called by system when corresponding device
 * has to be read.
 *
 * @note Macro can be used only by driver code.
 *
 * @param modname       module name
 * @param device_handle [<b>void *</b>]         memory region allocated by driver
 * @param dst           [<b>u8_t *</b>]         destination buffer
 * @param count         [<b>size_t</b>]         bytes to read
 * @param fpos          [<b>fpos_t *</b>]       file position indicator (can be modified)
 * @param rdcnt         [<b>size_t *</b>]       number of read bytes
 * @param fattr         [<b>struct vfs_fattr</b>] file access attributes
 * @return One of @ref errno value.
 *
 * @see struct vfs_fattr
 */
#define API_MOD_READ(modname, device_handle, dst, count, fpos, rdcnt, fattr)
#else
#define API_MOD_READ(modname, ...) _MODULE_EXTERN_C int _##modname##_read(__VA_ARGS__)
#endif

#ifdef DOXYGEN
/**
 * @brief Macro creates unique name of driver IO control function.
 *
 * Function created by this macro is called by system when corresponding device
 * has to be controlled in non-standard way.
 *
 * @note Macro can be used only by driver code.
 *
 * @param modname       module name
 * @param device_handle [<b>void *</b>]         memory region allocated by driver
 * @param request       [<b>int</b>]            request
 * @param arg           [<b>void *</b>]         argument pointer
 * @return One of @ref errno value.
 */
#define API_MOD_IOCTL(modname, device_handle, request, arg)
#else
#define API_MOD_IOCTL(modname, ...) _MODULE_EXTERN_C int _##modname##_ioctl(__VA_ARGS__)
#endif

#ifdef DOXYGEN
/**
 * @brief Macro creates unique name of driver flush function.
 *
 * Function created by this macro is called by system when buffers of corresponding
 * device has to be flushed to the device.
 *
 * @note Macro can be used only by driver code.
 *
 * @param modname       module name
 * @param device_handle [<b>void *</b>]         memory region allocated by driver
 * @return One of @ref errno value.
 */
#define API_MOD_FLUSH(modname, device_handle)
#else
#define API_MOD_FLUSH(modname, ...) _MODULE_EXTERN_C int _##modname##_flush(__VA_ARGS__)
#endif

#ifdef DOXYGEN
/**
 * @brief Macro creates unique name of driver statistics function.
 *
 * Function created by this macro is called by system when device statistics
 * has to be read.
 *
 * @note Macro can be used only by driver code.
 *
 * @param modname       module name
 * @param device_handle [<b>void *</b>]         memory region allocated by driver
 * @param device_stat   [<b>struct vfs_dev_stat *</b>]    device statistics
 * @return One of @ref errno value.
 *
 * @see struct vfs_dev_stat
 */
#define API_MOD_STAT(modname, device_handle, device_stat)
#else
#define API_MOD_STAT(modname, ...) _MODULE_EXTERN_C int _##modname##_stat(__VA_ARGS__)
#endif

#ifdef DOXYGEN
/**
 * @brief Macro registers shared interrupt handler.
 *
 * Handler is called with other handlers registered for the same interrupt
 * (also by other drivers), so it shall check interrupt flags of own device
 * and return without action if interrupt is not requested by its device.
 * Handlers are stored in flash (8 bytes per handler), RAM is not used.
 * Exclusive handler has the name of the vector table (e.g. USART1_IRQHandler)
 * and does not need this macro.
 *
 * @note Macro can be used only by drivers of the drvset partition (drivers
 *       built separately from the kernel). The interrupt shall not be handled
 *       by the kernel image (checked at partition start).
 *
 * @param irqn          IRQ number (e.g. EXTI15_10_IRQn)
 * @param handler       handler function: void handler(void)
 *
 * @b Example
 * @code
        static void button_irq(void)
        {
                if (EXTI_D1->PR1 & EXTI_PR1_PR13) {
                        EXTI_D1->PR1 = EXTI_PR1_PR13;
                        // ...
                }
        }

        DRV_IRQ_SHARED(EXTI15_10_IRQn, button_irq);
   @endcode
 */
#define DRV_IRQ_SHARED(irqn, handler)
#elif defined(__DNX_DRVSET__)
#define _DRV_IRQ_SHARED_NAME2(_handler, _line) _drv_irq_shared_##_handler##_##_line
#define _DRV_IRQ_SHARED_NAME(_handler, _line)  _DRV_IRQ_SHARED_NAME2(_handler, _line)
#define DRV_IRQ_SHARED(_irqn, _handler)                                                                                \
        static const struct _drv_irq_shared _DRV_IRQ_SHARED_NAME(_handler, __LINE__)                                   \
                __attribute__((section(".drvset_irq_shared"), used, aligned(4))) = {.irq = (u32_t)(_irqn),             \
                        .handler = _handler}
#else
#define DRV_IRQ_SHARED(_irqn, _handler) \
        _Static_assert(0, "DRV_IRQ_SHARED(): shared interrupt handlers are supported only by drvset partition")
#endif

#define dev_dbg(_hdl, format, ...) \
        printk("%s:%u-%u: " format, GET_MODULE_NAME(), _hdl->major, _hdl->minor __VA_OPT__(, ) __VA_ARGS__)
#define dev_dbg2(_major, _minor, format, ...) \
        printk("%s:%u-%u: " format, GET_MODULE_NAME(), _major, _minor __VA_OPT__(, ) __VA_ARGS__)

/*==============================================================================
  Exported object types
==============================================================================*/
#ifdef DOXYGEN /* type in vfs.h */
/**
 * @brief Device statistic type.
 *
 * The type represents device information e.g. size and address.
 */
struct vfs_dev_stat {
        u64_t st_size;              /*!< Total size, in bytes.*/
        u32_t st_blksize;           /*!< Block size in bytes (block devices only, 0 if not used).*/
        u32_t st_rdbufalign;        /*!< Required read buffer alignment, power of 2 (block devices only, 0: any).*/
        u32_t st_wrbufalign;        /*!< Required write buffer alignment, power of 2 (block devices only, 0: any).*/
        const char **st_bufregions; /*!< Allowed memory regions of data buffer (block devices only, NULL: any).*/
        bool st_bufdma;             /*!< Data buffer must be in DMA capable memory (block devices only).*/
        u8_t st_major;              /*!< Device major number.*/
        u8_t st_minor;              /*!< Device minor number.*/
};

/**
 * @brief Device lock type.
 *
 * The type represents device lock object. Object is used to lock device access
 * from other processes. Type is a reference to other object address (with unique
 * address for each process). Object address stored in type is used only as
 * unique number that identify process.
 *
 * @note Type can be used only by driver code.
 */
typedef void *dev_lock_t;
#endif /* DOXYGEN*/

#ifndef DOXYGEN
/** device lock object */
typedef pid_t dev_lock_t;
#endif

/*==============================================================================
  Exported object declarations
==============================================================================*/

/*==============================================================================
  Exported function prototypes
==============================================================================*/
//==============================================================================
/**
 * @brief Function locks device for this process.
 *
 * Function is used to lock access to the device opened by this thread.
 *
 * @note Function can be used only by driver code.
 *
 * @param dev_lock      pointer to device lock object
 *
 * @return If device is successfully locked then @ref ESUCC is returned,
 *         otherwise @ref EBUSY.
 *
 * @see drv_unlock()
 */
//==============================================================================
extern int drv_lock(dev_lock_t *dev_lock);

//==============================================================================
/**
 * @brief Function unlocks locked device.
 *
 * @note Function can be used only by driver code.
 *
 * @param dev_lock      pointer to device lock object
 * @param force         true: force unlock
 *
 * @return If device is successfully unlocked then @ref ESUCC is returned,
 *         otherwise @ref EBUSY (locked by other process).
 *
 * @see drv_lock()
 */
//==============================================================================
extern int drv_unlock(dev_lock_t *dev_lock, bool force);

//==============================================================================
/**
 * @brief Function gets device access.
 *
 * Function gets device access when was locked by this process.
 *
 * @note Function can be used only by driver code.
 *
 * @param dev_lock      pointer to device lock object
 *
 * @return If device is successfully locked by this process then @ref ESUCC is
 *         returned, otherwise @ref EBUSY (locked by other process).
 *
 * @see drv_lock(), drv_unlock()
 */
//==============================================================================
extern int drv_check_access(dev_lock_t *dev_lock);

//==============================================================================
/**
 * @brief Function checks that device is locked.
 *
 * @note Function can be used only by driver code.
 *
 * @param dev_lock      pointer to device lock object
 *
 * @return If device is unlocked then @b true is returned, otherwise @b false.
 *
 * @see drv_lock(), drv_unlock(), drv_check_access()
 */
//==============================================================================
extern bool drv_is_locked(dev_lock_t *dev_lock);

#ifdef DOXYGEN /* Doxygen documentation only. Macro defined in this file. */
//==============================================================================
/**
 * @brief  Get module allocated memory for specified major and minor number.
 *
 * Function get memory instance of module that call this function. The major
 * and minor numbers specify the instance.
 *
 * @note Function can be used only by driver code.
 *
 * @param major         major number
 * @param minor         minor number
 * @param mem           driver instance memory
 *
 * @return One of @ref errno value.
 */
//==============================================================================
int drv_get_instance(u8_t major, u8_t minor, void **mem);

#endif

//==============================================================================
/**
 * @brief  Function check if selected address is DMA capable.
 *
 * @param  ptr          address to examine
 *
 * @return If ptr is DMA capable then true is returned, otherwise false.
 *         NULL address is not DMA capable.
 */
//==============================================================================
extern bool drv_mem_is_dma_capable(const void *ptr);

//==============================================================================
/**
 * @brief Function update all system clock after CPU frequency change.
 *
 * Function shall update all devices which base on main clock oscillator.
 * Function is called after clock/frequency change from clock management driver
 * (e.g. PLL).
 *
 * @note Function can be used only by file system or driver code.
 */
//==============================================================================
extern void drv_clock_changed(void);

//==============================================================================
/**
 * @brief  Function register new memory region. There is no possibility to
 *         remove added region.
 *
 * The region descriptor (with copy of region name) is allocated by system.
 *
 * @note Function can be used only by driver code.
 *
 * @param  start        region start address
 * @param  size         region size
 * @param  flags        region features (_MM_FLAG__*)
 * @param  name         region name
 *
 * @return One of errno value.
 *
 * @b Example
 * @code
        // ...

        int err = drv_mem_register_region((void *)0x20001000, 16384, _MM_FLAG__DMA, "RAM2");
        if (!err) {
                // ...
        }

        // ...
   @endcode
 */
//==============================================================================
extern int drv_mem_register_region(void *start, size_t size, u32_t flags, const char *name);

//==============================================================================
/**
 * @brief  Function checks if memory is cacheable.
 *
 * @note Function can be used only by driver code.
 *
 * @param ptr       memory to examine
 *
 * @return If memory is cacheable then @b true is returned, otherwise @b false.
 */
//==============================================================================
extern bool drv_mem_is_cacheable(const void *ptr);

//==============================================================================
/**
 * @brief  Function allocates memory for driver. Used by sys_malloc() family macros.
 *
 * @param module_name           module name (memory is accounted to the module)
 * @param size                  block size
 * @param align                 block alignment (0: default)
 * @param prefreg               preferred memory regions (NULL terminated, can be @ref NULL)
 * @param required_features     required region features (_MM_FLAG__*)
 * @param required_mask         required features mask (_MM_FLAG__*)
 * @param zero                  clear allocated memory
 * @param mem                   allocated memory
 *
 * @return One of @ref errno value.
 */
//==============================================================================
extern int drv_mem_alloc(const char *module_name, size_t size, size_t align, const char **prefreg,
        u32_t required_features, u32_t required_mask, bool zero, void **mem);

//==============================================================================
/**
 * @brief  Function frees memory of driver. Used by sys_free() macro.
 *
 * @param module_name           module name
 * @param mem                   memory to free (pointer is cleared)
 *
 * @return One of @ref errno value.
 */
//==============================================================================
extern int drv_mem_free(const char *module_name, void **mem);

//==============================================================================
/**
 * @brief  Function returns instance of selected module. Used by drv_get_instance() macro.
 *
 * @param module_name           module name
 * @param major                 major number
 * @param minor                 minor number
 * @param mem                   driver instance memory
 *
 * @return One of @ref errno value.
 */
//==============================================================================
extern int drv_module_get_instance(const char *module_name, u8_t major, u8_t minor, void **mem);

#ifdef __cplusplus
}
#endif

#endif /* _DRIVER_H_ */
/**@}*/
/*==============================================================================
  End of file
==============================================================================*/
