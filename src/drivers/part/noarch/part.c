/*==============================================================================
File    part.c

Author  Daniel Zorychta

Brief   Partition driver

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
#include "drivers/driver.h"
#include "../part_ioctl.h"
#include "drivers/class/storage/mbr.h"

/*==============================================================================
  Local macros
==============================================================================*/
#define MUTEX_TIMEOUT_ms   5000
#define SECTOR_SIZE        512
#define MAX_PARTITIONS     15 /* partition numbers 1..15: minors 1..15 */
#define LABEL_MINOR_OFFSET 16 /* named GPT partition nodes: minors 17..31 */
#define NAME_LEN           37 /* GPT partition name in UTF-8 (with NUL) */
#define NODE_LEN           96
#define MBR_TYPE_GPT       0xEE
#define GPT_ENTRY_MIN_SIZE 128

/*==============================================================================
  Local object types
==============================================================================*/
enum {
        MODE_MANUAL, /* geometry from configuration */
        MODE_DISK,   /* whole device, optionally with automatically created partition nodes */
        MODE_PART,   /* partition selected by number */
        MODE_LABEL,  /* GPT partition selected by name */
};

typedef struct {
        u8_t major;
        u8_t minor;
        u8_t mode;
        u8_t part;
        u8_t opened;
        u16_t part_nodes;  /* disk: created partition nodes (bit n: partition n) */
        u16_t label_nodes; /* disk: created label nodes (bit n: partition n) */
        kfile_t *dev;
        kmtx_t *mutex;
        char *path;  /* device path */
        char *label; /* label instance: partition name, disk instance: node prefix */
        u32_t block_size;
        u64_t offset_bytes;
        u64_t total_bytes;
} PART_t;

typedef struct {
        u32_t first_lba;
        u32_t sectors;
        char name[NAME_LEN];
} entry_t;

typedef struct {
        u8_t sector[SECTOR_SIZE];
        entry_t entry[MAX_PARTITIONS]; /* index: partition number - 1, empty: sectors == 0 */
        char node[NODE_LEN];
} table_t;

/*==============================================================================
  Local function prototypes
==============================================================================*/
static int configure(PART_t *hdl, const PART_config_t *conf);
static int read_partition_table(PART_t *hdl, bool update_nodes);
static int release_instance(PART_t *hdl);
static void part_node(char *node, const char *prefix, int n);

/*==============================================================================
  Local object
==============================================================================*/
MODULE_NAME(PART);

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
API_MOD_INIT(PART, void **device_handle, u8_t major, u8_t minor, const void *config)
{
        int err = sys_zalloc(sizeof(PART_t), device_handle);
        if (!err) {
                PART_t *hdl = *device_handle;

                hdl->major = major;
                hdl->minor = minor;

                err = sys_mutex_create(KMTX_TYPE_RECURSIVE, &hdl->mutex);
                if (!err) {
                        if (config) {
                                err = configure(hdl, config);
                        }

                        if (err) {
                                release_instance(hdl);
                                *device_handle = NULL;
                        }
                } else {
                        sys_free(device_handle);
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
API_MOD_RELEASE(PART, void *device_handle)
{
        PART_t *hdl = device_handle;

        int err = sys_mutex_lock(hdl->mutex, MUTEX_TIMEOUT_ms);
        if (!err) {
                err = hdl->opened ? EBUSY : ESUCC;

                /* automatically created partitions are released with the disk */
                for (int n = 1; !err && (n <= MAX_PARTITIONS); n++) {
                        if (hdl->label_nodes & (1 << n)) {
                                PART_t *sub;
                                if (drv_get_instance(hdl->major, LABEL_MINOR_OFFSET + n, cast(void **, &sub)) == 0) {
                                        char node[NODE_LEN];
                                        sys_strfmt(node, sizeof(node), "%s-%s", hdl->label, sub->label);
                                        err = drv_instance_release(hdl->major, LABEL_MINOR_OFFSET + n, node);
                                }
                                if (!err) {
                                        hdl->label_nodes &= ~(1 << n);
                                }
                        }
                }

                for (int n = 1; !err && (n <= MAX_PARTITIONS); n++) {
                        if (hdl->part_nodes & (1 << n)) {
                                char node[NODE_LEN];
                                part_node(node, hdl->label, n);
                                err = drv_instance_release(hdl->major, n, node);
                                if (!err) {
                                        hdl->part_nodes &= ~(1 << n);
                                }
                        }
                }

                sys_mutex_unlock(hdl->mutex);

                if (!err) {
                        err = release_instance(hdl);
                }
        }

        return err;
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
API_MOD_OPEN(PART, void *device_handle, u32_t flags)
{
        UNUSED_ARG1(flags);

        PART_t *hdl = device_handle;

        int err = sys_mutex_lock(hdl->mutex, MUTEX_TIMEOUT_ms);
        if (!err) {
                if (hdl->opened < UINT8_MAX) {
                        hdl->opened++;
                } else {
                        err = EMFILE;
                }

                sys_mutex_unlock(hdl->mutex);
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
API_MOD_CLOSE(PART, void *device_handle, bool force)
{
        UNUSED_ARG1(force);

        PART_t *hdl = device_handle;

        int err = sys_mutex_lock(hdl->mutex, MUTEX_TIMEOUT_ms);
        if (!err) {
                if (hdl->opened) {
                        hdl->opened--;
                }

                sys_mutex_unlock(hdl->mutex);
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
API_MOD_WRITE(PART, void *device_handle, const u8_t *src, size_t count, fpos_t *fpos, size_t *wrcnt,
        struct vfs_fattr fattr)
{
        UNUSED_ARG1(fattr);

        PART_t *hdl = device_handle;

        int err = sys_mutex_lock(hdl->mutex, MUTEX_TIMEOUT_ms);
        if (!err) {

                u64_t addr = *fpos;

                if (!hdl->dev) {
                        err = ENXIO;

                } else if (addr < hdl->total_bytes) {

                        if ((addr + count) >= hdl->total_bytes) {
                                count = hdl->total_bytes - addr;
                        }

                        addr += hdl->offset_bytes;
                        err = sys_fseek(hdl->dev, addr, VFS_SEEK_SET);
                        if (!err) {
                                err = sys_fwrite(src, count, wrcnt, hdl->dev);
                        }
                }

                sys_mutex_unlock(hdl->mutex);
        }

        return err;
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
API_MOD_READ(PART, void *device_handle, u8_t *dst, size_t count, fpos_t *fpos, size_t *rdcnt, struct vfs_fattr fattr)
{
        UNUSED_ARG1(fattr);

        PART_t *hdl = device_handle;

        int err = sys_mutex_lock(hdl->mutex, MUTEX_TIMEOUT_ms);
        if (!err) {

                u64_t addr = *fpos;

                if (!hdl->dev) {
                        err = ENXIO;

                } else if (addr < hdl->total_bytes) {

                        if ((addr + count) >= hdl->total_bytes) {
                                count = hdl->total_bytes - addr;
                        }

                        addr += hdl->offset_bytes;
                        err = sys_fseek(hdl->dev, addr, VFS_SEEK_SET);
                        if (!err) {
                                err = sys_fread(dst, count, rdcnt, hdl->dev);
                        }
                }

                sys_mutex_unlock(hdl->mutex);
        }

        return err;
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
API_MOD_IOCTL(PART, void *device_handle, int request, void *arg)
{
        PART_t *hdl = device_handle;

        int err = EINVAL;

        switch (request) {
        case IOCTL_PART__CONFIGURE:
                if (arg) {
                        err = configure(hdl, arg);
                }
                break;

        case IOCTL_PART__CONFIGURE_STR:
                if (arg) {
                        char path[64];
                        char label[NAME_LEN];
                        char nodes[48];
                        PART_config_t cfg = {
                                .path = path,
                                .block_size = sys_opt_get_int(arg, "block_size", SECTOR_SIZE),
                                .offset_blocks = sys_opt_get_int(arg, "offset_blocks", 0),
                                .total_blocks = sys_opt_get_int(arg, "total_blocks", 0),
                                .part = sys_opt_get_int(arg, "part", 0),
                                .label = sys_opt_get_str_copy(arg, "label", label, sizeof(label)) ? label : NULL,
                                .nodes = sys_opt_get_str_copy(arg, "nodes", nodes, sizeof(nodes)) ? nodes : NULL,
                        };

                        if (sys_opt_get_str_copy(arg, "path", path, sizeof(path)) && (cfg.block_size > 0)) {
                                err = configure(hdl, &cfg);
                        }
                }
                break;

        case IOCTL_PART__READ_PARTITION_TABLE:
                err = read_partition_table(hdl, true);
                break;

        case IOCTL_PART__GET_CONFIGRATION:
                if (arg) {
                        err = sys_mutex_lock(hdl->mutex, MUTEX_TIMEOUT_ms);
                        if (!err) {
                                PART_config_t *cfg = arg;
                                memset(cfg, 0, sizeof(*cfg));
                                cfg->block_size = hdl->block_size;
                                if (hdl->block_size) {
                                        cfg->offset_blocks = hdl->offset_bytes / hdl->block_size;
                                        cfg->total_blocks = hdl->total_bytes / hdl->block_size;
                                }
                                cfg->part = hdl->part;
                                sys_mutex_unlock(hdl->mutex);
                        }
                }
                break;

        default:
                err = hdl->dev ? sys_ioctl(hdl->dev, request, arg) : ENXIO;
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
API_MOD_FLUSH(PART, void *device_handle)
{
        PART_t *hdl = device_handle;

        return hdl->dev ? sys_fflush(hdl->dev) : ESUCC;
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
API_MOD_STAT(PART, void *device_handle, struct vfs_dev_stat *device_stat)
{
        PART_t *hdl = device_handle;

        device_stat->st_size = hdl->total_bytes;
        device_stat->st_blksize = 1; // requests are forwarded, alignment is handled by the underlying device

        return ESUCC;
}

//==============================================================================
/**
 * @brief  Function frees instance resources.
 *
 * @param  hdl          device handle
 *
 * @return One of errno value (errno.h).
 */
//==============================================================================
static int release_instance(PART_t *hdl)
{
        if (hdl->dev) {
                sys_fclose(hdl->dev);
        }

        sys_free(cast(void **, &hdl->path));
        sys_free(cast(void **, &hdl->label));
        sys_mutex_destroy(hdl->mutex);
        sys_free(cast(void **, &hdl));

        return ESUCC;
}

//==============================================================================
/**
 * @brief  Function allocates copy of string.
 *
 * @param  str          string (NULL or empty: no copy)
 * @param  copy         copy
 *
 * @return One of errno value (errno.h).
 */
//==============================================================================
static int strdup_or_null(const char *str, char **copy)
{
        sys_free(cast(void **, copy));

        if (str && str[0]) {
                size_t size = strlen(str) + 1;
                int err = sys_malloc(size, cast(void **, copy));
                if (!err) {
                        memcpy(*copy, str, size);
                }
                return err;
        }

        return ESUCC;
}

//==============================================================================
/**
 * @brief  Function set driver configuration.
 *
 * @param  hdl          device handle
 * @param  conf         configuration
 *
 * @return One of errno value (errno.h).
 */
//==============================================================================
static int configure(PART_t *hdl, const PART_config_t *conf)
{
        if (!conf->path || (conf->part > MAX_PARTITIONS)) {
                return EINVAL;
        }

        int err = sys_mutex_lock(hdl->mutex, MUTEX_TIMEOUT_ms);
        if (!err) {

                if (hdl->dev) {
                        sys_fclose(hdl->dev);
                        hdl->dev = NULL;
                }

                err = sys_fopen(conf->path, O_RDWR, 0, &hdl->dev);
                if (err == EROFS || err == EACCES) {
                        err = sys_fopen(conf->path, O_RDONLY, 0, &hdl->dev);
                }

                if (!err) {
                        err = strdup_or_null(conf->path, &hdl->path);
                }

                if (!err) {
                        hdl->part = conf->part;
                        hdl->block_size = conf->block_size ? conf->block_size : SECTOR_SIZE;
                        hdl->total_bytes = cast(u64_t, conf->total_blocks) * hdl->block_size;
                        hdl->offset_bytes = cast(u64_t, conf->offset_blocks) * hdl->block_size;

                        if (conf->label && conf->label[0]) {
                                hdl->mode = MODE_LABEL;
                                err = strdup_or_null(conf->label, &hdl->label);

                        } else if (conf->part) {
                                hdl->mode = MODE_PART;

                        } else if (conf->total_blocks) {
                                hdl->mode = MODE_MANUAL;

                        } else {
                                hdl->mode = MODE_DISK;
                                err = strdup_or_null(conf->nodes, &hdl->label);
                        }
                }

                /* partitions configured without geometry and disks are resolved from the partition table */
                if (!err && (hdl->mode != MODE_MANUAL) && !conf->total_blocks) {
                        read_partition_table(hdl, false);
                }

                if (err && hdl->dev) {
                        sys_fclose(hdl->dev);
                        hdl->dev = NULL;
                }

                sys_mutex_unlock(hdl->mutex);
        }

        return err;
}

//==============================================================================
/**
 * @brief  Function calculates CRC32 (IEEE 802.3) of buffer.
 *
 * @param  crc          CRC of previous data (0 at start)
 * @param  buf          buffer
 * @param  len          buffer length
 *
 * @return CRC32.
 */
//==============================================================================
static u32_t crc32(u32_t crc, const u8_t *buf, size_t len)
{
        crc = ~crc;

        while (len--) {
                crc ^= *buf++;
                for (int i = 0; i < 8; i++) {
                        crc = (crc >> 1) ^ (0xEDB88320 & -(crc & 1));
                }
        }

        return ~crc;
}

//==============================================================================
/**
 * @brief  Function reads little-endian 32-bit number.
 */
//==============================================================================
static u32_t le32(const u8_t *buf)
{
        return buf[0] | (buf[1] << 8) | (buf[2] << 16) | (cast(u32_t, buf[3]) << 24);
}

//==============================================================================
/**
 * @brief  Function reads little-endian 64-bit number.
 */
//==============================================================================
static u64_t le64(const u8_t *buf)
{
        return le32(buf) | (cast(u64_t, le32(buf + 4)) << 32);
}

//==============================================================================
/**
 * @brief  Function reads selected sector of device.
 *
 * @param  dev          device
 * @param  lba          sector number
 * @param  buf          sector buffer
 *
 * @return One of errno value (errno.h).
 */
//==============================================================================
static int read_sector(kfile_t *dev, u64_t lba, u8_t *buf)
{
        size_t rdctr = 0;
        int err = sys_fseek(dev, lba * SECTOR_SIZE, VFS_SEEK_SET);
        if (!err) {
                err = sys_fread(buf, SECTOR_SIZE, &rdctr, dev);
        }

        return (err || (rdctr != SECTOR_SIZE)) ? EIO : ESUCC;
}

//==============================================================================
/**
 * @brief  Function reads GPT header and partition entries.
 *
 * @param  dev          device
 * @param  lba          header sector
 * @param  tbl          partition table
 *
 * @return One of errno value (errno.h).
 */
//==============================================================================
static int read_gpt(kfile_t *dev, u64_t lba, table_t *tbl)
{
        u8_t *hdr = tbl->sector;

        int err = read_sector(dev, lba, hdr);
        if (err) {
                return err;
        }

        u32_t hdr_size = le32(hdr + 12);
        u32_t hdr_crc = le32(hdr + 16);
        memset(hdr + 16, 0, 4);

        if ((memcmp(hdr, "EFI PART", 8) != 0) || (hdr_size < 92) || (hdr_size > SECTOR_SIZE) ||
                (crc32(0, hdr, hdr_size) != hdr_crc) || (le64(hdr + 24) != lba)) {
                return EMEDIUMTYPE;
        }

        u64_t entries_lba = le64(hdr + 72);
        u32_t entries = le32(hdr + 80);
        u32_t entry_size = le32(hdr + 84);
        u32_t entries_crc = le32(hdr + 88);

        if ((entry_size < GPT_ENTRY_MIN_SIZE) || (SECTOR_SIZE % entry_size) || (entries > 1024)) {
                return EMEDIUMTYPE;
        }

        memset(tbl->entry, 0, sizeof(tbl->entry));

        u32_t crc = 0;
        u32_t n = 0;

        while (!err && (n < entries)) {
                err = read_sector(dev, entries_lba++, tbl->sector);

                for (size_t pos = 0; !err && (pos < SECTOR_SIZE) && (n < entries); pos += entry_size, n++) {
                        const u8_t *e = tbl->sector + pos;
                        crc = crc32(crc, e, entry_size);

                        static const u8_t unused[16];
                        if ((n >= MAX_PARTITIONS) || (memcmp(e, unused, 16) == 0)) {
                                continue;
                        }

                        u64_t first = le64(e + 32);
                        u64_t last = le64(e + 40);
                        if ((last < first) || (last > UINT32_MAX)) {
                                continue;
                        }

                        entry_t *ent = &tbl->entry[n];
                        ent->first_lba = first;
                        ent->sectors = last - first + 1;

                        /* name UTF-16LE to UTF-8 (basic multilingual plane) */
                        char *name = ent->name;
                        char *end = name + NAME_LEN - 1;
                        for (int i = 0; i < 36; i++) {
                                u16_t c = e[56 + 2 * i] | (e[57 + 2 * i] << 8);
                                if (c == 0) {
                                        break;
                                } else if (c < 0x80) {
                                        if (name + 1 > end) {
                                                break;
                                        }
                                        *name++ = c;
                                } else if (c < 0x800) {
                                        if (name + 2 > end) {
                                                break;
                                        }
                                        *name++ = 0xC0 | (c >> 6);
                                        *name++ = 0x80 | (c & 0x3F);
                                } else {
                                        if (name + 3 > end) {
                                                break;
                                        }
                                        *name++ = 0xE0 | (c >> 12);
                                        *name++ = 0x80 | ((c >> 6) & 0x3F);
                                        *name++ = 0x80 | (c & 0x3F);
                                }
                        }
                }
        }

        if (!err && (crc != entries_crc)) {
                err = EMEDIUMTYPE;
        }

        return err;
}

//==============================================================================
/**
 * @brief  Function reads partition table (GPT or MBR) of device.
 *
 * @param  dev          device
 * @param  tbl          partition table
 *
 * @return One of errno value (errno.h).
 */
//==============================================================================
static int read_table(kfile_t *dev, table_t *tbl)
{
        int err = read_sector(dev, 0, tbl->sector);
        if (err) {
                return err;
        }

        /*
         * GPT: protective MBR (type 0xEE). Damaged protective MBR (no signature
         * or no partitions): GPT is used when its header is valid.
         */
        bool mbr = (MBR_get_boot_signature(tbl->sector) == MBR_SIGNATURE);
        bool gpt = !mbr;
        bool empty = true;

        for (int i = 0; mbr && (i < 4); i++) {
                const u8_t *entry = tbl->sector + MBR_PARTITION_1_ENTRY_OFFSET + MBR_PARTITION_ENTRY_SIZE * i;
                if (entry[MBR_PARTITION_ENTRY_PARTITION_TYPE_OFFSET] == MBR_TYPE_GPT) {
                        gpt = true;
                }
                if (MBR_get_partition_number_of_sectors(i + 1, tbl->sector)) {
                        empty = false;
                }
        }

        if (gpt || empty) {
                memset(tbl->entry, 0, sizeof(tbl->entry));

                err = read_gpt(dev, 1, tbl);

                if (err == EMEDIUMTYPE) {
                        /* primary header or entries damaged: backup header in the last sector */
                        struct stat st;
                        err = sys_fstat(dev, &st);
                        if (!err) {
                                err = (st.st_size >= 3 * SECTOR_SIZE)
                                        ? read_gpt(dev, (st.st_size / SECTOR_SIZE) - 1, tbl)
                                        : EMEDIUMTYPE;
                        }
                }

                /* MBR without partitions and without GPT: empty partition table */
                if (!gpt && mbr && (err == EMEDIUMTYPE)) {
                        memset(tbl->entry, 0, sizeof(tbl->entry));
                        err = ESUCC;
                }

                return err;
        }

        for (int i = 0; i < 4; i++) {
                tbl->entry[i].first_lba = MBR_get_partition_first_LBA_sector(i + 1, tbl->sector);
                tbl->entry[i].sectors = MBR_get_partition_number_of_sectors(i + 1, tbl->sector);
        }

        return ESUCC;
}

//==============================================================================
/**
 * @brief  Function creates path of partition node: <prefix><n>, <prefix>p<n>
 *         when prefix ends with digit.
 *
 * @param  node         node path buffer (NODE_LEN)
 * @param  prefix       node prefix
 * @param  n            partition number
 */
//==============================================================================
static void part_node(char *node, const char *prefix, int n)
{
        char last = prefix[strlen(prefix) - 1];
        sys_strfmt(node, NODE_LEN, "%s%s%d", prefix, ((last >= '0') && (last <= '9')) ? "p" : "", n);
}

//==============================================================================
/**
 * @brief  Function checks that partition name can be used in node path.
 *
 * @param  name         partition name
 *
 * @return true if name is valid.
 */
//==============================================================================
static bool is_node_name(const char *name)
{
        if (!name[0]) {
                return false;
        }

        for (; *name; name++) {
                if ((*name <= ' ') || (*name == '/')) {
                        return false;
                }
        }

        return true;
}

//==============================================================================
/**
 * @brief  Function creates and releases partition nodes of disk instance.
 *
 * @param  disk         disk instance (locked)
 * @param  tbl          partition table
 */
//==============================================================================
static void update_nodes(PART_t *disk, table_t *tbl)
{
        const char *prefix = disk->label;

        for (int n = 1; n <= MAX_PARTITIONS; n++) {
                const entry_t *ent = &tbl->entry[n - 1];
                const u16_t bit = 1 << n;
                const u8_t lminor = LABEL_MINOR_OFFSET + n;

                /* label node: released when partition or its name was changed */
                if (disk->label_nodes & bit) {
                        PART_t *sub;
                        if (drv_get_instance(disk->major, lminor, cast(void **, &sub)) == 0) {
                                if (!ent->sectors || !isstreq(sub->label, ent->name)) {
                                        sys_strfmt(tbl->node, NODE_LEN, "%s-%s", prefix, sub->label);
                                        if (drv_instance_release(disk->major, lminor, tbl->node) == 0) {
                                                disk->label_nodes &= ~bit;
                                        }
                                }
                        } else {
                                disk->label_nodes &= ~bit;
                        }
                }

                part_node(tbl->node, prefix, n);

                if (!ent->sectors) {
                        if ((disk->part_nodes & bit) && (drv_instance_release(disk->major, n, tbl->node) == 0)) {
                                disk->part_nodes &= ~bit;
                        }
                        continue;
                }

                PART_config_t cfg = {
                        .path = disk->path,
                        .block_size = SECTOR_SIZE,
                        .offset_blocks = ent->first_lba,
                        .total_blocks = ent->sectors,
                        .part = n,
                };

                if (!(disk->part_nodes & bit) && (drv_instance_init(disk->major, n, tbl->node, &cfg) == 0)) {
                        disk->part_nodes |= bit;
                }

                if (!(disk->label_nodes & bit) && is_node_name(ent->name)) {
                        sys_strfmt(tbl->node, NODE_LEN, "%s-%s", prefix, ent->name);
                        cfg.part = 0;
                        cfg.label = ent->name;
                        if (drv_instance_init(disk->major, lminor, tbl->node, &cfg) == 0) {
                                disk->label_nodes |= bit;
                        }
                }
        }
}

//==============================================================================
/**
 * @brief  Function updates geometry of partition instance.
 *
 * @param  hdl          partition instance
 * @param  tbl          partition table (NULL: no partition table)
 */
//==============================================================================
static void update_geometry(PART_t *hdl, const table_t *tbl)
{
        if (sys_mutex_lock(hdl->mutex, MUTEX_TIMEOUT_ms) == 0) {

                if (hdl->mode == MODE_DISK) {
                        struct stat st;
                        hdl->offset_bytes = 0;
                        hdl->total_bytes = (sys_fstat(hdl->dev, &st) == 0) ? st.st_size : 0;

                } else if (hdl->mode != MODE_MANUAL) {
                        const entry_t *ent = NULL;

                        for (int i = 0; tbl && (i < MAX_PARTITIONS) && !ent; i++) {
                                if (tbl->entry[i].sectors &&
                                        ((hdl->mode == MODE_PART) ? (hdl->part == i + 1)
                                                                  : isstreq(hdl->label, tbl->entry[i].name))) {
                                        ent = &tbl->entry[i];
                                }
                        }

                        /* geometry of mounted partition is not changed */
                        if (!hdl->opened || ent) {
                                hdl->block_size = SECTOR_SIZE;
                                hdl->offset_bytes = ent ? cast(u64_t, ent->first_lba) * SECTOR_SIZE : 0;
                                hdl->total_bytes = ent ? cast(u64_t, ent->sectors) * SECTOR_SIZE : 0;
                        }
                }

                sys_mutex_unlock(hdl->mutex);
        }
}

//==============================================================================
/**
 * @brief  Function reads partition table (GPT or MBR) and updates all instances
 *         of the major number connected to the same device. Disk instance (minor 0)
 *         with node prefix creates and releases partition nodes.
 *
 * @param  hdl          device handle
 * @param  nodes        update nodes of disk instance
 *
 * @return One of errno value (errno.h).
 */
//==============================================================================
static int read_partition_table(PART_t *hdl, bool nodes)
{
        if (!hdl->dev) {
                return ENXIO;
        }

        table_t *tbl;
        int err = sys_zalloc(sizeof(table_t), cast(void **, &tbl));
        if (err) {
                return err;
        }

        err = sys_mutex_lock(hdl->mutex, MUTEX_TIMEOUT_ms);
        if (!err) {
                err = read_table(hdl->dev, tbl);
                sys_mutex_unlock(hdl->mutex);
        }

        /* read error: instances are not changed, missing table: no partitions */
        if ((err != EIO) && (err != ETIME)) {
                if (err) {
                        memset(tbl->entry, 0, sizeof(tbl->entry));
                }

                PART_t *disk;
                if (((hdl->mode == MODE_DISK) || nodes) &&
                        (drv_get_instance(hdl->major, 0, cast(void **, &disk)) == 0) && (disk->mode == MODE_DISK) &&
                        disk->label && isstreq(disk->path, hdl->path) &&
                        (sys_mutex_lock(disk->mutex, MUTEX_TIMEOUT_ms) == 0)) {

                        update_nodes(disk, tbl);
                        sys_mutex_unlock(disk->mutex);
                }

                if (nodes) {
                        for (int minor = 0; minor <= UINT8_MAX; minor++) {
                                PART_t *sub;
                                if ((drv_get_instance(hdl->major, minor, cast(void **, &sub)) == 0) && sub->path &&
                                        isstreq(sub->path, hdl->path)) {
                                        update_geometry(sub, tbl);
                                }
                        }
                } else {
                        update_geometry(hdl, tbl);
                }
        }

        sys_free(cast(void **, &tbl));

        return err;
}

/*==============================================================================
  End of file
==============================================================================*/
