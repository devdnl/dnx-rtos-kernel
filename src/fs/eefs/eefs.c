/*==============================================================================
File     eefs.c

Author   Daniel Zorychta

Brief    EEPROM File System. File system for small memories 1-64KiB EEPROM.

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
#include "fs/fs.h"

/*==============================================================================
  Local symbolic constants/macros
==============================================================================*/
#define MSG(...) printk("EEFS: "__VA_ARGS__)

/* file system check at mount (option "check"), enabled when not configured */
#ifndef __EEFS_CHECK_ENABLE__
#define __EEFS_CHECK_ENABLE__ 1
#endif

#if __EEFS_LOG_ENABLE__ > 0
#define DBG(...) MSG(__VA_ARGS__)
#else
#define DBG(...)
#endif

#define BUSY_TIMEOUT 20000

#define BLOCK_SIZE     128
#define BLOCKS_IN_BYTE 8

#define MAIN_BLOCK_ADDR 0

#define ENTRY_TYPE_DIR  0xFE
#define ENTRY_TYPE_FILE 0xFD
#define ENTRY_TYPE_NODE 0xFC

#define BLOCK_MAGIC_MAIN      0x53464545
#define BLOCK_MAGIC_BITMAP    0x20504D42
#define BLOCK_MAGIC_DIR       0x30524944
#define BLOCK_MAGIC_DIR_ENTRY 0x45524944
#define BLOCK_MAGIC_FILE      0x454C4946
#define BLOCK_MAGIC_FILE_DATA 0x41544144
#define BLOCK_MAGIC_NODE      0x30564544

#define DIR_DESC_MAGIC  0x24526701
#define FILE_DESC_MAGIC 0xA2751FF7

#define NAME_LEN 21 // note: modify with care

#define FLAG_RDONLY (1 << 0)
#define FLAG_CHECK  (1 << 1)

#define LOST_FOUND_NAME "lost+found"

#define block_is_dir(block_buf)       (block_buf.buf.dir.magic == BLOCK_MAGIC_DIR)
#define block_is_dir_entry(block_buf) (block_buf.buf.dir_entry.magic == BLOCK_MAGIC_DIR_ENTRY)
#define block_is_file(block_buf)      (block_buf.buf.file.magic == BLOCK_MAGIC_FILE)
#define block_is_file_data(block_buf) (block_buf.buf.file.magic == BLOCK_MAGIC_FILE_DATA)
#define block_is_node(block_buf)      (block_buf.buf.node.magic == BLOCK_MAGIC_NODE)

#define DEFAULT_MODE (S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH)

#define BMP_MAIN_BITS     (sizeof(((block_main_t *)0)->bitmap) * BLOCKS_IN_BYTE) // 952 blocks
#define BMP_BLK_BITS      (sizeof(((block_bitmap_t *)0)->map) * BLOCKS_IN_BYTE)  // 976 blocks
#define BITMAP_BLOCKS_MAX 67                                                     // enough for 65535 blocks
#define FILE_HDR_DATA     sizeof(((block_file_t *)0)->data)
#define FILE_BLK_DATA     sizeof(((block_file_data_t *)0)->data)
#define FILE_SIZE_MAX     UINT16_MAX

/* 0 is the regular chain terminator, 0xFFFF is accepted for robustness */
#define CHAIN_END(n) (((n) == 0) || ((n) == 0xFFFF))

#ifndef ENODATA
#define ENODATA ENOENT
#endif

#ifndef EFBIG
#define EFBIG ENOSPC
#endif

#ifndef EIO
#define EIO EFAULT
#endif

/*==============================================================================
  Local types, enums definitions
==============================================================================*/
/**
 * FS structure:
 * Block        Structure
 * 0            block_main (defines number of additional bitmap blocks)
 * 1            block_bitmap 1 (if needed)
 * 2            block_bitmap 2 (if needed)
 * n            block_bitmap n (if needed)
 * n+1          dir_entry root (position calculated by using block_main definitions)
 *
 * Next blocks according to block position address defined in root dir entry.
 *
 * Allocation order rule (power-loss safety): a new block is marked in the
 * bitmap before anything points to it and a block is released in the bitmap
 * only after nothing points to it. A power loss can therefore only leak a block
 * (repaired by fsckeefs), never cause two objects to share one block.
 */

/**
 * Generic checksum block (calculation purposes).
 */
typedef struct PACKED {
        uint8_t buf[126];
        uint16_t checksum;
} block_chsum_t;

/**
 * Main block definition (block 0).
 */
typedef struct PACKED {
        uint32_t magic;        //!< FS identifier ('EEFS')
        uint16_t blocks;       //!< number of memory 128B blocks
        uint8_t bitmap_blocks; //!< number of next bitmap blocks
        uint8_t bitmap[119];   //!< empty block bitmap
        uint16_t checksum;     //!< checksum
} block_main_t;

/**
 * Bitmap block definition.
 */
typedef struct PACKED {
        uint32_t magic;    //!< block magic number
        uint8_t map[122];  //!< bitmap
        uint16_t checksum; //!< checksum
} block_bitmap_t;

/**
 * Single directory entry.
 */
typedef struct PACKED {
        uint16_t block_addr; //!< block address with object
        uint8_t type;        //!< entry type
        char name[NAME_LEN]; //!< entry name
} dir_entry_t;

/**
 * Directory block.
 */
typedef struct PACKED {
        uint32_t magic;       //!< block magic number
        uint32_t ctime;       //!< create timestamp
        uint32_t mtime;       //!< modification timestamp
        uint16_t gid;         //!< group ID
        uint16_t uid;         //!< user ID
        uint16_t mode;        //!< mode
        uint16_t parent;      //!< parent entry block number
        uint16_t next;        //!< next directory entries
        uint16_t all_items;   //!< number of items in all chains
        uint8_t _[6];         //!< reserved
        dir_entry_t entry[4]; //!< directory entry
        uint16_t checksum;    //!< checksum
} block_dir_t;

/**
 * Directory entry block.
 */
typedef struct PACKED {
        uint32_t magic;       //!< block magic number
        dir_entry_t entry[5]; //!< directory entries
        uint16_t next;        //!< next directory entries
        uint16_t checksum;    //!< checksum
} block_dir_entry_t;

/**
 * File block.
 *
 * Note: structure has 126 bytes, the 'checksum' field is not used (the real
 * checksum is located at offset 126, see block_chsum_t). Layout is kept
 * unchanged for compatibility with existing media.
 */
typedef struct PACKED {
        uint32_t magic;     //!< block magic number
        uint32_t ctime;     //!< create timestamp
        uint32_t mtime;     //!< modification timestamp
        uint16_t gid;       //!< group ID
        uint16_t uid;       //!< user ID
        uint16_t mode;      //!< mode
        uint16_t size;      //!< file size in bytes
        uint8_t data[102];  //!< file data
        uint16_t data_next; //!< next file data
        uint16_t checksum;  //!< not used (see note above)
} block_file_t;

/**
 * File data block.
 */
typedef struct PACKED {
        uint32_t magic;     //!< block magic number
        uint8_t data[120];  //!< data
        uint16_t data_next; //!< next data block
        uint16_t checksum;  //!< checksum
} block_file_data_t;

/**
 * Device file block.
 */
typedef struct PACKED {
        uint32_t magic;    //!< block magic number
        uint32_t ctime;    //!< create timestamp
        uint32_t mtime;    //!< modification timestamp
        uint32_t dev;      //!< device number
        uint16_t gid;      //!< group ID
        uint16_t uid;      //!< user ID
        uint16_t mode;     //!< mode
        uint8_t _[104];    //!< reserved
        uint16_t checksum; //!< checksum
} block_node_t;

/**
 * Generic block.
 */
typedef union {
        block_main_t main;
        block_bitmap_t bitmap;
        block_dir_t dir;
        block_dir_entry_t dir_entry;
        block_file_t file;
        block_file_data_t file_data;
        block_node_t node;
        block_chsum_t chsum;
} block_t;

_Static_assert(sizeof(block_t) == BLOCK_SIZE, "EEFS: invalid block size");
_Static_assert(sizeof(block_main_t) == BLOCK_SIZE, "EEFS: invalid main block size");
_Static_assert(sizeof(block_bitmap_t) == BLOCK_SIZE, "EEFS: invalid bitmap block size");
_Static_assert(sizeof(block_dir_t) == BLOCK_SIZE, "EEFS: invalid dir block size");
_Static_assert(sizeof(block_dir_entry_t) == BLOCK_SIZE, "EEFS: invalid dir entry block size");
_Static_assert(sizeof(block_file_data_t) == BLOCK_SIZE, "EEFS: invalid file data block size");
_Static_assert(sizeof(block_node_t) == BLOCK_SIZE, "EEFS: invalid node block size");
_Static_assert(sizeof(block_file_t) == BLOCK_SIZE - 2, "EEFS: file block layout changed");
_Static_assert(sizeof(dir_entry_t) == 24, "EEFS: invalid dir entry size");

/**
 * Dir descriptor.
 */
typedef struct dir_desc {
        struct dir_desc *next;
        uint32_t magic;
        uint16_t block_num;
        char name[NAME_LEN];
} dir_desc_t;

/**
 * File descriptor.
 */
typedef struct file_desc {
        struct file_desc *next;
        uint32_t magic;
        uint32_t block_file_magic;
        uint32_t block_dev;
        uint32_t flags;
        uint16_t block_num;
} file_desc_t;

/**
 * Type contains block buffer and position on disc.
 */
typedef struct {
        u16_t num;
        block_t buf;
} block_buf_t;

/**
 * Type contains block cache buffer.
 */
typedef struct {
        bool used;
        u32_t hit;
        block_buf_t buf;
} cache_buf_t;

/**
 * File system handle.
 */
typedef struct {
        kfile_t *srcdev;
        kmtx_t *lock_mtx;
        dir_desc_t *open_dirs;
        file_desc_t *open_files;
        uint16_t root_dir_block;
        uint16_t blocks;       //!< number of blocks (copy of main block value)
        uint8_t bitmap_blocks; //!< number of additional bitmap blocks
        block_buf_t block;
        block_buf_t tmpblock;
        u8_t flag;
#if __EEFS_CACHE_SIZE__ > 0
        cache_buf_t cache_block[__EEFS_CACHE_SIZE__];
#endif
} EEFS_t;

/*==============================================================================
  Local function prototypes
==============================================================================*/
static uint16_t fletcher16(uint8_t const *data, size_t bytes);
static int block_read(EEFS_t *hdl, block_buf_t *blk);
static int block_write(EEFS_t *hdl, block_buf_t *blk);
static bool is_entry_item_used(const dir_entry_t *entry);
static bool is_entry_name(const dir_entry_t *entry, const char *name, size_t len);
static bool is_object_open(EEFS_t *hdl, u16_t blknum);
static int block_load(EEFS_t *hdl, const char *path);
static int block_load_by_type(EEFS_t *hdl, const char *path, uint32_t type);
static int block_get_file_stat(EEFS_t *hdl, struct stat *stat);
static int bmp_block_find_empty(EEFS_t *hdl, uint16_t *blknum);
static int bmp_block_alloc_ctrl(EEFS_t *hdl, uint16_t blknum, bool allocate);
static int bmp_block_alloc(EEFS_t *hdl, uint16_t blknum);
static int bmp_block_free(EEFS_t *hdl, uint16_t blknum);
static int bmp_get_free_blocks(EEFS_t *hdl, u32_t *blkfree);
static const char *path_get_next_item(const char *path, char **name, size_t *len, bool *last);
static int path_alloc_dirname(const char *path, char **path_base);
static const char *path_get_basename(const char *path, size_t *len);
static int dir_get_entry_params(EEFS_t *hdl, dir_entry_t **dirent, u8_t *items, u16_t *blknext);
static int dir_set_next(EEFS_t *hdl, u16_t next);
static int dir_add_chain(EEFS_t *hdl);
static int dir_find_entry(EEFS_t *hdl, u16_t blkdir, const char *name, size_t len, dir_entry_t **entry, u16_t *blkprev,
        u8_t *used);
static int dir_find_free_slot(EEFS_t *hdl, u16_t blkdir, dir_entry_t **slot);
static int dir_detach_entry(EEFS_t *hdl, dir_entry_t *entry, u16_t blkprev, u8_t used);
static int dir_update_items(EEFS_t *hdl, u16_t blkdir, int delta);
static int dir_is_empty(EEFS_t *hdl, u16_t blkdir);
static int obj_create(EEFS_t *hdl, u32_t type, u16_t blkparent, mode_t mode, u32_t dev, u16_t *blkobj);
static int obj_release(EEFS_t *hdl, u16_t blkobj);
static int dir_add_entry(EEFS_t *hdl, const char *path, uint32_t type, mode_t mode, u32_t dev);
static int dir_rm_entry(EEFS_t *hdl, const char *path);
static int dir_rename(EEFS_t *hdl, const char *old_name, const char *new_name);
static int dir_get_size(EEFS_t *hdl, u16_t blkdir, u16_t *dirsize);
static int dir_read_entry(EEFS_t *hdl, dir_desc_t *dd, dir_entry_t *eefs_entry, dirent_t *dirent);
static int file_truncate(EEFS_t *hdl);
static int file_add_chain(EEFS_t *hdl);
static int file_write(EEFS_t *hdl, const u8_t *src, size_t count, fpos_t *fpos, size_t *wrcnt);
static int file_read(EEFS_t *hdl, u8_t *dst, size_t count, fpos_t *fpos, size_t *rdcnt);
#if __EEFS_CHECK_ENABLE__ > 0
static int fs_check(EEFS_t *hdl, bool bitmap_damaged);
#endif

/*==============================================================================
  Local object definitions
==============================================================================*/
static const u8_t zero_block[FILE_BLK_DATA];

/*==============================================================================
  Function definitions
==============================================================================*/
//==============================================================================
/**
 * @brief Initialize file system
 *
 * @param[out]          **fs_handle             file system allocated memory
 * @param[in ]           *src_path              file source path
 * @param[in ]           *opts                  file system options (can be NULL)
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
API_FS_INIT(eefs, void **fs_handle, const char *src_path, const char *opts)
{
        int err = sys_zalloc(sizeof(EEFS_t), fs_handle);
        if (err) {
                return err;
        }

        EEFS_t *hdl = *fs_handle;

        if (!isstrempty(opts)) {
                if (sys_stropt_is_flag(opts, "ro")) {
                        hdl->flag |= FLAG_RDONLY;
                        MSG("mounted read-only");
                }

                if (sys_stropt_is_flag(opts, "check")) {
#if __EEFS_CHECK_ENABLE__ > 0
                        hdl->flag |= FLAG_CHECK;
                        MSG("enabled file system force check");
#else
                        MSG("file system check disabled in configuration, option ignored");
#endif
                }
        }

        err = sys_fopen(src_path, O_RDWR, 0, &hdl->srcdev);
        if (err) {
                goto finish;
        }

        err = sys_mutex_create(KMTX_TYPE_NORMAL, &hdl->lock_mtx);
        if (err) {
                goto finish;
        }

        hdl->block.num = MAIN_BLOCK_ADDR;
        err = block_read(hdl, &hdl->block);
        if (err) {
                if (err == EILSEQ) {
                        MSG("main block damaged");
                        err = EMEDIUMTYPE;
                }
                goto finish;
        }

        if ((hdl->block.buf.main.magic != BLOCK_MAGIC_MAIN) || (hdl->block.buf.main.blocks < 8) ||
                (hdl->block.buf.main.bitmap_blocks > BITMAP_BLOCKS_MAX) ||
                ((2U + hdl->block.buf.main.bitmap_blocks) > hdl->block.buf.main.blocks)) {

                err = EMEDIUMTYPE;
                goto finish;
        }

        // values are copied before the block buffer is reused
        hdl->blocks = hdl->block.buf.main.blocks;
        hdl->bitmap_blocks = hdl->block.buf.main.bitmap_blocks;
        hdl->root_dir_block = 1 + hdl->bitmap_blocks;

        bool bitmap_damaged = false;

        for (u16_t blk = 1; blk <= hdl->bitmap_blocks; blk++) {
                hdl->block.num = blk;
                err = block_read(hdl, &hdl->block);

                if (!err && (hdl->block.buf.bitmap.magic != BLOCK_MAGIC_BITMAP)) {
                        err = EILSEQ;
                }

                if (err == EILSEQ) {
                        MSG("broken bitmap in block %u", blk);
                        err = EMEDIUMTYPE;

                        // bitmap is rebuilt by file system check
                        if (hdl->flag & FLAG_CHECK) {
                                bitmap_damaged = true;
                                err = ESUCC;
                        }
                }

                if (err) {
                        break;
                }
        }

        if (!err) {
                hdl->block.num = hdl->root_dir_block;
                err = block_read(hdl, &hdl->block);

                if (!err && !block_is_dir(hdl->block)) {
                        err = EILSEQ;
                }

                if (err == EILSEQ) {
                        MSG("broken root directory in block %u", hdl->root_dir_block);
                        err = EMEDIUMTYPE;

                        // root directory is recreated by file system check
                        if (hdl->flag & FLAG_CHECK) {
                                err = ESUCC;
                        }
                }
        }

#if __EEFS_CHECK_ENABLE__ > 0
        if (!err && (hdl->flag & FLAG_CHECK)) {
                err = fs_check(hdl, bitmap_damaged);
        }
#else
        UNUSED_ARG1(bitmap_damaged);
#endif

        if (!err) {
                if (__EEFS_CACHE_SIZE__ > 0) {
                        MSG("using cache write-through");
                }
        }

finish:
        if (err) {
                DBG("init error %d", err);

                if (hdl->srcdev) {
                        sys_fclose(hdl->srcdev);
                }

                if (hdl->lock_mtx) {
                        sys_mutex_destroy(hdl->lock_mtx);
                }

                sys_free(fs_handle);
        }

        return err;
}

//==============================================================================
/**
 * @brief Release file system
 *
 * @param[in ]          *fs_handle              file system allocated memory
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
API_FS_RELEASE(eefs, void *fs_handle)
{
        EEFS_t *hdl = fs_handle;

        int err = sys_mutex_lock(hdl->lock_mtx, BUSY_TIMEOUT);
        if (!err) {
                if ((hdl->open_files == NULL) && (hdl->open_dirs == NULL)) {

                        sys_fclose(hdl->srcdev);

                        kmtx_t *mtx = hdl->lock_mtx;

                        memset(hdl, 0, sizeof(EEFS_t));

                        sys_mutex_unlock(mtx);
                        sys_mutex_destroy(mtx);

                        sys_free(cast(void *, &hdl));

                        return err;
                } else {
                        err = EBUSY;
                }

                sys_mutex_unlock(hdl->lock_mtx);
        }

        return err;
}

//==============================================================================
/**
 * @brief Create node for driver file
 *
 * @param[in ]          *fs_handle              file system allocated memory
 * @param[in ]          *path                   name of created node
 * @param[in ]           dev                    driver id
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
API_FS_MKNOD(eefs, void *fs_handle, const char *path, const dev_t dev)
{
        EEFS_t *hdl = fs_handle;

        if (isstrempty(path) || (LAST_CHARACTER(path) == '/')) {
                return EINVAL;
        }

        int err = sys_mutex_lock(hdl->lock_mtx, BUSY_TIMEOUT);
        if (!err) {
                err = dir_add_entry(hdl, path, BLOCK_MAGIC_NODE, DEFAULT_MODE, cast(u32_t, dev));
                sys_mutex_unlock(hdl->lock_mtx);
        }

        DBG("mknod '%s' (%d)", path, err);

        return err;
}

//==============================================================================
/**
 * @brief Create directory
 *
 * @param[in ]          *fs_handle              file system allocated memory
 * @param[in ]          *path                   name of created directory
 * @param[in ]           mode                   dir mode
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
API_FS_MKDIR(eefs, void *fs_handle, const char *path, mode_t mode)
{
        EEFS_t *hdl = fs_handle;

        int err = sys_mutex_lock(hdl->lock_mtx, BUSY_TIMEOUT);
        if (!err) {
                err = dir_add_entry(hdl, path, BLOCK_MAGIC_DIR, mode, 0);
                sys_mutex_unlock(hdl->lock_mtx);
        }

        DBG("mkdir '%s' (%d)", path, err);

        return err;
}

//==============================================================================
/**
 * @brief Create pipe
 *
 * @param[in ]          *fs_handle              file system allocated memory
 * @param[in ]          *path                   name of created pipe
 * @param[in ]           mode                   pipe mode
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
API_FS_MKFIFO(eefs, void *fs_handle, const char *path, mode_t mode)
{
        UNUSED_ARG3(fs_handle, path, mode);

        return ENOTSUP;
}

//==============================================================================
/**
 * @brief Open directory
 *
 * @param[in ]          *fs_handle              file system allocated memory
 * @param[in ]          *path                   name of opened directory
 * @param[in ]          *dir                    directory object
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
API_FS_OPENDIR(eefs, void *fs_handle, const char *path, kdir_t *dir)
{
        EEFS_t *hdl = fs_handle;

        int err = sys_mutex_lock(hdl->lock_mtx, BUSY_TIMEOUT);
        if (!err) {

                err = block_load_by_type(hdl, path, BLOCK_MAGIC_DIR);
                if (!err) {

                        err = sys_malloc(sizeof(dir_desc_t), &dir->d_hdl);
                        if (!err) {
                                dir_desc_t *dd = dir->d_hdl;

                                memset(dd, 0, sizeof(dir_desc_t));
                                dd->block_num = hdl->block.num;
                                dd->magic = DIR_DESC_MAGIC;
                                dd->next = hdl->open_dirs;
                                hdl->open_dirs = dd;

                                dir->d_items = hdl->block.buf.dir.all_items;
                                dir->d_seek = 0;
                        }
                }

                sys_mutex_unlock(hdl->lock_mtx);
        }

        DBG("opendir '%s' (%d)", path, err);

        return err;
}

//==============================================================================
/**
 * @brief Function close dir
 *
 * @param[in] *fs_handle        FS handle
 * @param[in] *dir              directory info
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
API_FS_CLOSEDIR(eefs, void *fs_handle, kdir_t *dir)
{
        EEFS_t *hdl = fs_handle;
        dir_desc_t *dd = dir->d_hdl;

        if (!dd) {
                return EINVAL;
        }

        int err = sys_mutex_lock(hdl->lock_mtx, BUSY_TIMEOUT);
        if (!err) {
                if (dd->magic == DIR_DESC_MAGIC) {

                        err = EFAULT;

                        dir_desc_t *dirp = hdl->open_dirs;
                        dir_desc_t *prev = NULL;

                        while (dirp) {
                                if (dirp == dd) {
                                        if (prev) {
                                                prev->next = dirp->next;
                                        } else {
                                                hdl->open_dirs = dirp->next;
                                        }

                                        memset(dirp, 0, sizeof(dir_desc_t));
                                        sys_free(&dir->d_hdl);

                                        err = ESUCC;
                                        break;
                                }

                                prev = dirp;
                                dirp = dirp->next;
                        }
                } else {
                        err = EILSEQ;
                }

                sys_mutex_unlock(hdl->lock_mtx);
        }

        DBG("closedir (%d)", err);

        return err;
}

//==============================================================================
/**
 * @brief Read directory
 *
 * @param[in ]          *fs_handle              file system allocated memory
 * @param[in,out]       *dir                    directory object
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
API_FS_READDIR(eefs, void *fs_handle, kdir_t *dir)
{
        EEFS_t *hdl = fs_handle;

        dir_desc_t *dd = dir->d_hdl;
        if (!dd || dd->magic != DIR_DESC_MAGIC || dd->block_num == 0) {
                DBG("readdir, broken descriptor");
                return EINVAL;
        }

        int err = sys_mutex_lock(hdl->lock_mtx, BUSY_TIMEOUT);
        if (!err) {

                u32_t seek = dir->d_seek;
                u16_t guard = hdl->blocks;
                bool found = false;

                hdl->block.num = dd->block_num;

                memset(dd->name, 0, sizeof(dd->name));
                dir->dirent.d_name = dd->name;
                dir->dirent.dev = 0;
                dir->dirent.size = 0;
                dir->dirent.mode = 0;

                while (!err && !found) {
                        err = block_read(hdl, &hdl->block);
                        if (err) {
                                break;
                        }

                        dir_entry_t *entry = NULL;
                        uint8_t items = 0;
                        uint16_t blknext = 0;

                        err = dir_get_entry_params(hdl, &entry, &items, &blknext);

                        for (u8_t i = 0; !err && (i < items); i++, entry++) {
                                if (is_entry_item_used(entry)) {
                                        if (seek == 0) {
                                                err = dir_read_entry(hdl, dd, entry, &dir->dirent);
                                                dir->d_seek++;
                                                found = true;
                                                break;
                                        } else {
                                                seek--;
                                        }
                                }
                        }

                        if (!err && !found) {
                                if (CHAIN_END(blknext)) {
                                        err = ENOENT;
                                } else if (--guard == 0) {
                                        err = EILSEQ;
                                } else {
                                        hdl->block.num = blknext;
                                }
                        }
                }

                sys_mutex_unlock(hdl->lock_mtx);
        }

        DBG("readdir (%d)", err);

        return err;
}

//==============================================================================
/**
 * @brief Remove file/directory
 *
 * @param[in ]          *fs_handle              file system allocated memory
 * @param[in ]          *path                   name of removed file/directory
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
API_FS_REMOVE(eefs, void *fs_handle, const char *path)
{
        EEFS_t *hdl = fs_handle;

        int err = sys_mutex_lock(hdl->lock_mtx, BUSY_TIMEOUT);
        if (!err) {

                err = dir_rm_entry(hdl, path);

                sys_mutex_unlock(hdl->lock_mtx);
        }

        DBG("remove '%s' (%d)", path, err);

        return err;
}

//==============================================================================
/**
 * @brief Rename file/directory
 *
 * Object can be renamed in the same directory or moved to another directory
 * of the same file system. Existing destination is not replaced (EEXIST).
 *
 * @param[in ]          *fs_handle              file system allocated memory
 * @param[in ]          *old_name               old object name
 * @param[in ]          *new_name               new object name
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
API_FS_RENAME(eefs, void *fs_handle, const char *old_name, const char *new_name)
{
        EEFS_t *hdl = fs_handle;

        int err = sys_mutex_lock(hdl->lock_mtx, BUSY_TIMEOUT);
        if (!err) {

                err = dir_rename(hdl, old_name, new_name);

                sys_mutex_unlock(hdl->lock_mtx);
        }

        DBG("rename '%s' -> '%s' (%d)", old_name, new_name, err);

        return err;
}

//==============================================================================
/**
 * @brief Change file's mode
 *
 * @param[in ]          *fs_handle              file system allocated memory
 * @param[in ]          *path                   file path
 * @param[in ]           mode                   new file mode
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
API_FS_CHMOD(eefs, void *fs_handle, const char *path, mode_t mode)
{
        EEFS_t *hdl = fs_handle;

        int err = sys_mutex_lock(hdl->lock_mtx, BUSY_TIMEOUT);
        if (!err) {

                err = block_load(hdl, path);
                if (!err) {
                        switch (hdl->block.buf.dir.magic) {
                        case BLOCK_MAGIC_DIR:
                                hdl->block.buf.dir.mode = mode;
                                break;

                        case BLOCK_MAGIC_FILE:
                                hdl->block.buf.file.mode = mode;
                                break;

                        case BLOCK_MAGIC_NODE:
                                hdl->block.buf.node.mode = mode;
                                break;

                        default:
                                err = EILSEQ;
                                break;
                        }

                        if (!err) {
                                err = block_write(hdl, &hdl->block);
                        }
                }

                sys_mutex_unlock(hdl->lock_mtx);
        }

        DBG("chmod '%s' 0%o (%d)", path, mode, err);

        return err;
}

//==============================================================================
/**
 * @brief Change file's owner and group
 *
 * @param[in ]          *fs_handle              file system allocated memory
 * @param[in ]          *path                   file path
 * @param[in ]           owner                  new file owner
 * @param[in ]           group                  new file group
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
API_FS_CHOWN(eefs, void *fs_handle, const char *path, uid_t owner, gid_t group)
{
        EEFS_t *hdl = fs_handle;

        int err = sys_mutex_lock(hdl->lock_mtx, BUSY_TIMEOUT);
        if (!err) {

                err = block_load(hdl, path);
                if (!err) {
                        switch (hdl->block.buf.dir.magic) {
                        case BLOCK_MAGIC_DIR:
                                hdl->block.buf.dir.uid = owner;
                                hdl->block.buf.dir.gid = group;
                                break;

                        case BLOCK_MAGIC_FILE:
                                hdl->block.buf.file.uid = owner;
                                hdl->block.buf.file.gid = group;
                                break;

                        case BLOCK_MAGIC_NODE:
                                hdl->block.buf.node.uid = owner;
                                hdl->block.buf.node.gid = group;
                                break;

                        default:
                                err = EILSEQ;
                                break;
                        }

                        if (!err) {
                                err = block_write(hdl, &hdl->block);
                        }
                }

                sys_mutex_unlock(hdl->lock_mtx);
        }

        DBG("chown '%s' %d %d (%d)", path, owner, group, err);

        return err;
}

//==============================================================================
/**
 * @brief Return file/dir status
 *
 * @param[in ]          *fs_handle              file system allocated memory
 * @param[in ]          *path                   file path
 * @param[out]          *stat                   file status
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
API_FS_STAT(eefs, void *fs_handle, const char *path, struct stat *stat)
{
        EEFS_t *hdl = fs_handle;

        int err = sys_mutex_lock(hdl->lock_mtx, BUSY_TIMEOUT);
        if (!err) {

                err = block_load(hdl, path);
                if (!err) {
                        err = block_get_file_stat(hdl, stat);
                }

                sys_mutex_unlock(hdl->lock_mtx);
        }

        DBG("stat '%s' (%d)", path, err);

        return err;
}

//==============================================================================
/**
 * @brief Return file status
 *
 * @param[in ]          *fs_handle              file system allocated memory
 * @param[in ]          *fhdl                   file handle
 * @param[out]          *stat                   file status
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
API_FS_FSTAT(eefs, void *fs_handle, void *fhdl, struct stat *stat)
{
        EEFS_t *hdl = fs_handle;
        file_desc_t *fd = fhdl;

        int err = EILSEQ;

        if (fd->magic == FILE_DESC_MAGIC) {

                err = sys_mutex_lock(hdl->lock_mtx, BUSY_TIMEOUT);
                if (!err) {

                        hdl->block.num = fd->block_num;
                        err = block_read(hdl, &hdl->block);

                        if (!err) {
                                err = block_get_file_stat(hdl, stat);
                        }

                        sys_mutex_unlock(hdl->lock_mtx);
                }
        }

        DBG("fstat (%d)", err);

        return err;
}

//==============================================================================
/**
 * @brief Return file system status
 *
 * @param[in ]          *fs_handle              file system allocated memory
 * @param[out]          *statfs                 file system status
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
API_FS_STATFS(eefs, void *fs_handle, struct statfs *statfs)
{
        EEFS_t *hdl = fs_handle;

        statfs->f_bsize = sizeof(block_t);
        statfs->f_ffree = 0;
        statfs->f_files = 0;
        statfs->f_fsname = "eefs";
        statfs->f_type = SYS_FS_TYPE__SOLID;

        int err = sys_mutex_lock(hdl->lock_mtx, BUSY_TIMEOUT);
        if (!err) {

                statfs->f_blocks = hdl->blocks;

                u32_t blkfree = 0;
                err = bmp_get_free_blocks(hdl, &blkfree);
                if (!err) {
                        statfs->f_bfree = blkfree;
                }

                sys_mutex_unlock(hdl->lock_mtx);
        }

        DBG("statfs (%d)", err);

        return err;
}

//==============================================================================
/**
 * @brief Open file
 *
 * @param[in ]          *fs_handle              file system allocated memory
 * @param[out]          *fhdl                   file handle
 * @param[out]          *fpos                   file position
 * @param[in]           *path                   file path
 * @param[in]            flags                  file open flags
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
API_FS_OPEN(eefs, void *fs_handle, void **fhdl, fpos_t *fpos, const char *path, u32_t flags)
{
        EEFS_t *hdl = fs_handle;

        int err = sys_mutex_lock(hdl->lock_mtx, BUSY_TIMEOUT);
        if (!err) {

                file_desc_t *fd;
                err = sys_malloc(sizeof(file_desc_t), cast(void *, &fd));
                if (!err) {
                        memset(fd, 0, sizeof(file_desc_t));

                        err = block_load(hdl, path);

#ifdef O_EXCL
                        if (!err && (flags & O_CREAT) && (flags & O_EXCL)) {
                                err = EEXIST;
                        }
#endif
                        if ((err == ENOENT) && (flags & O_CREAT)) {
                                // new object is loaded to the block buffer
                                err = dir_add_entry(hdl, path, BLOCK_MAGIC_FILE, DEFAULT_MODE, 0);
                        }

                        if (!err) {
                                if (block_is_file(hdl->block) || block_is_node(hdl->block)) {

                                        if (flags & O_TRUNC) {
                                                err = file_truncate(hdl);
                                        }

                                        if (!err) {
                                                if (block_is_node(hdl->block)) {
                                                        *fpos = 0;
                                                        err = sys_driver_open(hdl->block.buf.node.dev, flags);

                                                } else {
                                                        *fpos = !(flags & O_APPEND) ? 0 : hdl->block.buf.file.size;
                                                }
                                        }

                                        if (!err) {
                                                fd->block_num = hdl->block.num;
                                                fd->block_file_magic = hdl->block.buf.node.magic;
                                                fd->block_dev = block_is_node(hdl->block) ? hdl->block.buf.node.dev : 0;
                                                fd->magic = FILE_DESC_MAGIC;
                                                fd->flags = flags;
                                                fd->next = hdl->open_files;
                                                hdl->open_files = fd;
                                                *fhdl = fd;
                                        }

                                } else if (block_is_dir(hdl->block)) {
                                        err = EISDIR;

                                } else {
                                        err = EILSEQ;
                                }
                        }

                        if (err) {
                                sys_free(cast(void *, &fd));
                        }
                }

                sys_mutex_unlock(hdl->lock_mtx);
        }

        DBG("file open '%s' (%d)", path, err);

        return err;
}

//==============================================================================
/**
 * @brief Close file
 *
 * Note: the file should always be closed (resources should be released) even
 *       function return error.
 *
 * @param[in ]          *fs_handle              file system allocated memory
 * @param[in ]          *fhdl                   file handle
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
API_FS_CLOSE(eefs, void *fs_handle, void *fhdl)
{
        EEFS_t *hdl = fs_handle;
        file_desc_t *fd = fhdl;
        u16_t block_num = 0;

        int err = sys_mutex_lock(hdl->lock_mtx, _MAX_DELAY_MS);
        if (!err) {
                if (fd->magic == FILE_DESC_MAGIC) {

                        err = EFAULT;
                        block_num = fd->block_num;

                        file_desc_t *file = hdl->open_files;
                        file_desc_t *prev = NULL;

                        while (file) {
                                if (file == fd) {
                                        if (fd->block_file_magic == BLOCK_MAGIC_NODE) {
                                                err = sys_driver_close(fd->block_dev, true);
                                        } else {
                                                err = ESUCC;
                                        }

                                        if (prev) {
                                                prev->next = file->next;
                                        } else {
                                                hdl->open_files = file->next;
                                        }

                                        memset(fd, 0, sizeof(file_desc_t));

                                        sys_free(&fhdl);

                                        break;
                                }

                                prev = file;
                                file = file->next;
                        }
                } else {
                        printk("EEFS: invalid file descriptor!");
                        err = EILSEQ;
                }

                sys_mutex_unlock(hdl->lock_mtx);
        }

        DBG("close file '%u' (%d)", block_num, err);
        UNUSED_ARG1(block_num);

        return err;
}

//==============================================================================
/**
 * @brief Write data to the file
 *
 * @param[in ]          *fs_handle              file system allocated memory
 * @param[in ]          *fhdl                   file handle
 * @param[in ]          *src                    data source
 * @param[in ]           count                  number of bytes to write
 * @param[in ]          *fpos                   position in file
 * @param[out]          *wrcnt                  number of written bytes
 * @param[in ]           fattr                  file attributes
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
API_FS_WRITE(eefs, void *fs_handle, void *fhdl, const u8_t *src, size_t count, fpos_t *fpos, size_t *wrcnt,
        struct vfs_fattr fattr)
{
        EEFS_t *hdl = fs_handle;
        file_desc_t *fd = fhdl;

        if (fd->magic != FILE_DESC_MAGIC) {
                return EILSEQ;
        }

        int err = sys_mutex_lock(hdl->lock_mtx, BUSY_TIMEOUT);
        if (err) {
                return err;
        }

        hdl->block.num = fd->block_num;
        err = block_read(hdl, &hdl->block);

        if (!err) {
                if (block_is_node(hdl->block)) {
                        dev_t dev = hdl->block.buf.node.dev;
                        sys_mutex_unlock(hdl->lock_mtx);

                        return sys_driver_write(dev, src, count, fpos, wrcnt, fattr);

                } else if (block_is_file(hdl->block)) {
                        err = file_write(hdl, src, count, fpos, wrcnt);

                } else {
                        err = EILSEQ;
                }
        }

        sys_mutex_unlock(hdl->lock_mtx);

        return err;
}

//==============================================================================
/**
 * @brief Read data from file
 *
 * @param[in ]          *fs_handle              file system allocated memory
 * @param[in ]          *fhdl                   file handle
 * @param[out]          *dst                    data destination
 * @param[in ]           count                  number of bytes to read
 * @param[in ]          *fpos                   position in file
 * @param[out]          *rdcnt                  number of read bytes
 * @param[in ]           fattr                  file attributes
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
API_FS_READ(eefs, void *fs_handle, void *fhdl, u8_t *dst, size_t count, fpos_t *fpos, size_t *rdcnt,
        struct vfs_fattr fattr)
{
        EEFS_t *hdl = fs_handle;
        file_desc_t *fd = fhdl;

        if (fd->magic != FILE_DESC_MAGIC) {
                return EILSEQ;
        }

        int err = sys_mutex_lock(hdl->lock_mtx, BUSY_TIMEOUT);
        if (err) {
                return err;
        }

        hdl->block.num = fd->block_num;
        err = block_read(hdl, &hdl->block);

        if (!err) {
                if (block_is_node(hdl->block)) {
                        dev_t dev = hdl->block.buf.node.dev;
                        sys_mutex_unlock(hdl->lock_mtx);

                        return sys_driver_read(dev, dst, count, fpos, rdcnt, fattr);

                } else if (block_is_file(hdl->block)) {
                        err = file_read(hdl, dst, count, fpos, rdcnt);

                } else {
                        err = EILSEQ;
                }
        }

        sys_mutex_unlock(hdl->lock_mtx);

        return err;
}

//==============================================================================
/**
 * @brief IO operations on files
 *
 * @param[in ]          *fs_handle              file system allocated memory
 * @param[in ]          *fhdl                   file handle
 * @param[in ]           request                request
 * @param[in ][out]     *arg                    request's argument
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
API_FS_IOCTL(eefs, void *fs_handle, void *fhdl, int request, void *arg)
{
        EEFS_t *hdl = fs_handle;
        file_desc_t *fd = fhdl;

        int err = EILSEQ;

        if (fd->magic == FILE_DESC_MAGIC) {

                err = sys_mutex_lock(hdl->lock_mtx, BUSY_TIMEOUT);
                if (!err) {
                        bool node = false;
                        dev_t dev = 0;

                        hdl->block.num = fd->block_num;
                        err = block_read(hdl, &hdl->block);

                        if (!err && block_is_node(hdl->block)) {
                                dev = hdl->block.buf.node.dev;
                                node = true;
                        }

                        sys_mutex_unlock(hdl->lock_mtx);

                        if (node) {
                                err = sys_driver_ioctl(dev, request, arg);
                        }
                }
        }

        return err;
}

//==============================================================================
/**
 * @brief Flush file data
 *
 * @param[in ]          *fs_handle              file system allocated memory
 * @param[in ]          *fhdl                   file handle
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
API_FS_FLUSH(eefs, void *fs_handle, void *fhdl)
{
        EEFS_t *hdl = fs_handle;
        file_desc_t *fd = fhdl;

        int err = EILSEQ;

        if (fd->magic == FILE_DESC_MAGIC) {

                err = sys_mutex_lock(hdl->lock_mtx, BUSY_TIMEOUT);
                if (!err) {
                        bool node = false;
                        dev_t dev = 0;

                        hdl->block.num = fd->block_num;
                        err = block_read(hdl, &hdl->block);

                        if (!err && block_is_node(hdl->block)) {
                                dev = hdl->block.buf.node.dev;
                                node = true;
                        }

                        sys_mutex_unlock(hdl->lock_mtx);

                        if (node) {
                                err = sys_driver_flush(dev);
                        }
                }
        }

        return err;
}

//==============================================================================
/**
 * @brief Synchronize all buffers to a medium.
 *
 * @param[in ]          *fs_handle              file system allocated memory
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
API_FS_SYNC(eefs, void *fs_handle)
{
        UNUSED_ARG1(fs_handle);

        return ESUCC;
}

//==============================================================================
/**
 * @brief  Function calculate fletcher 16 checksum.
 *
 * @param  data         buffer
 * @param  bytes        buffer size
 *
 * @return Checksum.
 */
//==============================================================================
static uint16_t fletcher16(uint8_t const *data, size_t bytes)
{
        uint16_t sum1 = 0xff, sum2 = 0xff;
        size_t tlen;

        while (bytes) {
                tlen = ((bytes >= 20) ? 20 : bytes);
                bytes -= tlen;
                do {
                        sum2 += sum1 += *data++;
                        tlen--;
                } while (tlen);

                sum1 = (sum1 & 0xff) + (sum1 >> 8);
                sum2 = (sum2 & 0xff) + (sum2 >> 8);
        }

        /* Second reduction step to reduce sums to 8 bits */
        sum1 = (sum1 & 0xff) + (sum1 >> 8);
        sum2 = (sum2 & 0xff) + (sum2 >> 8);

        return (sum2 << 8) | sum1;
}

//==============================================================================
/**
 * @brief Function read block from memory. Function uses caching subsystem.
 *
 * @param  hdl          FS handle.
 * @param  blk          block to read.
 *
 * @return One of errno value.
 */
//==============================================================================
static int block_read(EEFS_t *hdl, block_buf_t *blk)
{
        int err = EFAULT;

        // block numbers are validated after mount (pointers from media are not trusted)
        if ((hdl->blocks > 0) && (blk->num >= hdl->blocks)) {
                DBG("block %u out of range", blk->num);
                return EILSEQ;
        }

#if __EEFS_CACHE_SIZE__ > 0
        for (size_t i = 0; i < ARRAY_SIZE(hdl->cache_block); i++) {
                if (hdl->cache_block[i].used) {
                        if (hdl->cache_block[i].buf.num == blk->num) {
                                memcpy(blk, &hdl->cache_block[i].buf, sizeof(*blk));
                                hdl->cache_block[i].hit++;
                                err = 0;
                        } else {
                                if (hdl->cache_block[i].hit > 0) {
                                        hdl->cache_block[i].hit--;
                                }
                        }
                }
        }

        if (!err) {
                return err;
        }
#endif

        for (int retry = 0; retry < 3; retry++) {

                memset(&blk->buf, 0, BLOCK_SIZE);

                size_t rdcnt = 0;
                err = sys_fseek(hdl->srcdev, cast(i64_t, blk->num) * BLOCK_SIZE, SEEK_SET);
                if (!err) {
                        err = sys_fread(&blk->buf, BLOCK_SIZE, &rdcnt, hdl->srcdev);
                }

                if (!err && (rdcnt != BLOCK_SIZE)) {
                        err = EIO;
                }

                if (!err) {
                        u16_t chsum = fletcher16(blk->buf.chsum.buf, sizeof(blk->buf.chsum.buf));
                        chsum ^= blk->num;

                        err = (chsum == blk->buf.chsum.checksum) ? ESUCC : EILSEQ;

                        if (!err) {
#if __EEFS_CACHE_SIZE__ > 0
                                uint32_t hit_min = UINT32_MAX;

                                // find not used block
                                for (size_t i = 0; i < ARRAY_SIZE(hdl->cache_block); i++) {
                                        if (!hdl->cache_block[i].used) {
                                                memcpy(&hdl->cache_block[i].buf, blk, sizeof(*blk));
                                                hdl->cache_block[i].used = true;
                                                hdl->cache_block[i].hit = 0;
                                                return 0;

                                        } else {
                                                hit_min = min(hit_min, hdl->cache_block[i].hit);
                                        }
                                }

                                // if empty block not found, then select rare used
                                for (size_t i = 0; i < ARRAY_SIZE(hdl->cache_block); i++) {
                                        if (hdl->cache_block[i].hit == hit_min) {
                                                hdl->cache_block[i].hit = 0;
                                                memcpy(&hdl->cache_block[i].buf, blk, sizeof(*blk));
                                                return 0;
                                        }
                                }
#endif
                                break;

                        } else {
                                DBG("Block %d checksum fail", blk->num);
                        }
                }

                sys_sleep_ms(10);
        }

        return err;
}

//==============================================================================
/**
 * @brief Function write block to memory. Function uses caching subsystem.
 *
 * @param  hdl          FS handle.
 * @param  blk          block to write.
 *
 * @return One of errno value.
 */
//==============================================================================
static int block_write(EEFS_t *hdl, block_buf_t *blk)
{
        if (hdl->flag & FLAG_RDONLY) {
                return EROFS;
        }

        if (blk->num >= hdl->blocks) {
                DBG("write: block %u out of range", blk->num);
                return EILSEQ;
        }

        blk->buf.chsum.checksum = fletcher16(blk->buf.chsum.buf, sizeof(blk->buf.chsum.buf));
        blk->buf.chsum.checksum ^= blk->num;

        size_t wrcnt = 0;
        int err = sys_fseek(hdl->srcdev, cast(i64_t, blk->num) * BLOCK_SIZE, SEEK_SET);
        if (!err) {
                err = sys_fwrite(&blk->buf, BLOCK_SIZE, &wrcnt, hdl->srcdev);
        }

        if (!err && (wrcnt != BLOCK_SIZE)) {
                err = EIO;
        }

#if __EEFS_CACHE_SIZE__ > 0
        // cache is updated only when medium contains the same data
        for (size_t i = 0; i < ARRAY_SIZE(hdl->cache_block); i++) {
                if (hdl->cache_block[i].used && hdl->cache_block[i].buf.num == blk->num) {
                        if (!err) {
                                memcpy(&hdl->cache_block[i].buf, blk, sizeof(*blk));
                        } else {
                                hdl->cache_block[i].used = false;
                        }
                        break;
                }
        }
#endif

        return err;
}

//==============================================================================
/**
 * @brief  Function check if entry is used.
 *
 * @param  entry    directory entry
 *
 * @return If entry is used then true is returned otherwise false.
 */
//==============================================================================
static bool is_entry_item_used(const dir_entry_t *entry)
{
        return (entry->type == ENTRY_TYPE_DIR || entry->type == ENTRY_TYPE_FILE || entry->type == ENTRY_TYPE_NODE);
}

//==============================================================================
/**
 * @brief  Function check if entry has selected name.
 *
 * @param  entry    directory entry
 * @param  name     name (not necessarily nul terminated)
 * @param  len      name length
 *
 * @return If names are equal then true is returned otherwise false.
 */
//==============================================================================
static bool is_entry_name(const dir_entry_t *entry, const char *name, size_t len)
{
        return (len < NAME_LEN) && (strnlen(entry->name, NAME_LEN) == len) && (memcmp(entry->name, name, len) == 0);
}

//==============================================================================
/**
 * @brief  Function check if selected object is opened (file or directory).
 *
 * @param  hdl          EEFS handle
 * @param  blknum       object block
 *
 * @return If object is opened then true is returned otherwise false.
 */
//==============================================================================
static bool is_object_open(EEFS_t *hdl, u16_t blknum)
{
        for (dir_desc_t *dd = hdl->open_dirs; dd; dd = dd->next) {
                if (dd->block_num == blknum) {
                        return true;
                }
        }

        for (file_desc_t *fd = hdl->open_files; fd; fd = fd->next) {
                if (fd->block_num == blknum) {
                        return true;
                }
        }

        return false;
}

//==============================================================================
/**
 * @brief  Function load block by using path.
 *
 * @param  hdl          EEFS handle
 * @param  path         path
 *
 * @return One of errno value.
 */
//==============================================================================
static int block_load(EEFS_t *hdl, const char *path)
{
        int err = ESUCC;
        char *name;
        size_t namelen;
        bool namelast;

        bool found = false;
        u16_t guard = hdl->blocks;
        hdl->block.num = hdl->root_dir_block;

        if (isstrempty(path)) {
                return ENOENT;
        }

        if (isstreq(path, "/")) {
                return block_read(hdl, &hdl->block);
        }

        const char *path_ref = path_get_next_item(path, &name, &namelen, &namelast);

        if (!path_ref) {
                DBG("load_block() '%s'", path);
                return ENOENT;
        }

        do {
                if (namelen == 0) {
                        err = ENOENT;
                        break;
                }

                if (namelen >= NAME_LEN) {
                        err = ENAMETOOLONG;
                        break;
                }

                err = block_read(hdl, &hdl->block);
                if (err) {
                        break;
                }

                dir_entry_t *dirent = NULL;
                uint8_t items = 0;
                uint16_t blknext = 0;

                err = dir_get_entry_params(hdl, &dirent, &items, &blknext);

                for (u8_t i = 0; !err && (i < items); i++, dirent++) {

                        if (is_entry_item_used(dirent) && is_entry_name(dirent, name, namelen)) {

                                if (namelast) {
                                        found = true;
                                        hdl->block.num = dirent->block_addr;
                                        err = block_read(hdl, &hdl->block);

                                } else if (dirent->type == ENTRY_TYPE_DIR) {
                                        blknext = dirent->block_addr;
                                        path_ref = path_get_next_item(path_ref, &name, &namelen, &namelast);
                                        if (!path_ref) {
                                                err = ENOENT;
                                        }

                                } else {
                                        err = ENOTDIR;
                                }

                                break;
                        }
                }

                // load next chain
                if (!err && !found) {
                        if (CHAIN_END(blknext)) {
                                err = ENOENT;
                        } else if (--guard == 0) {
                                err = EILSEQ;
                        } else {
                                hdl->block.num = blknext;
                        }
                }

        } while (!found && !err);

        return err;
}

//==============================================================================
/**
 * @brief  Function load block by path and type.
 *
 * @param  hdl          EEFS handle
 * @param  path         path
 * @param  type         block type
 *
 * @return One of errno value.
 */
//==============================================================================
static int block_load_by_type(EEFS_t *hdl, const char *path, uint32_t type)
{
        int err = block_load(hdl, path);
        if (!err) {
                if (hdl->block.buf.dir.magic != type) {
                        DBG("Invalid block type");
                        err = (type == BLOCK_MAGIC_DIR) ? ENOTDIR : ENOENT;
                }
        }

        return err;
}

//==============================================================================
/**
 * @brief  Function get file statistics from current block.
 *
 * @param  hdl          EEFS handle
 * @param  stat         file statistics
 *
 * @return One of errno value.
 */
//==============================================================================
static int block_get_file_stat(EEFS_t *hdl, struct stat *stat)
{
        int err = ESUCC;

        switch (hdl->block.buf.dir.magic) {
        case BLOCK_MAGIC_DIR: {
                u16_t size = 0;
                err = dir_get_size(hdl, hdl->block.num, &size);

                stat->st_size = size;
                stat->st_dev = 0;
                stat->st_mode = S_IFDIR | hdl->block.buf.dir.mode;
                stat->st_uid = hdl->block.buf.dir.uid;
                stat->st_gid = hdl->block.buf.dir.gid;
                stat->st_ctime = hdl->block.buf.dir.ctime;
                stat->st_mtime = hdl->block.buf.dir.mtime;
                break;
        }

        case BLOCK_MAGIC_FILE:
                stat->st_size = hdl->block.buf.file.size;
                stat->st_dev = 0;
                stat->st_mode = S_IFREG | hdl->block.buf.file.mode;
                stat->st_uid = hdl->block.buf.file.uid;
                stat->st_gid = hdl->block.buf.file.gid;
                stat->st_ctime = hdl->block.buf.file.ctime;
                stat->st_mtime = hdl->block.buf.file.mtime;
                break;

        case BLOCK_MAGIC_NODE: {
                struct vfs_dev_stat dev_stat;
                err = sys_driver_stat(hdl->block.buf.node.dev, &dev_stat);

                if (!err) {
                        stat->st_size = dev_stat.st_size;
                }
                if (err == ENODEV) {
                        stat->st_size = 0;
                        err = ESUCC;
                }

                stat->st_dev = hdl->block.buf.node.dev;
                stat->st_mode = S_IFDEV | hdl->block.buf.node.mode;
                stat->st_uid = hdl->block.buf.node.uid;
                stat->st_gid = hdl->block.buf.node.gid;
                stat->st_ctime = hdl->block.buf.node.ctime;
                stat->st_mtime = hdl->block.buf.node.mtime;
                break;
        }

        default:
                err = EILSEQ;
                break;
        }

        return err;
}

//==============================================================================
/**
 * @brief  Function load selected bitmap block to the temporary block buffer.
 *
 * @param  hdl          EEFS handle
 * @param  bmpblk       bitmap block number (0: main block)
 * @param  map          bitmap pointer
 * @param  mapsz        bitmap size in bytes
 *
 * @return One of errno value.
 */
//==============================================================================
static int bmp_load(EEFS_t *hdl, u16_t bmpblk, u8_t **map, u16_t *mapsz)
{
        hdl->tmpblock.num = bmpblk;
        int err = block_read(hdl, &hdl->tmpblock);
        if (!err) {
                if (bmpblk == MAIN_BLOCK_ADDR) {
                        if (hdl->tmpblock.buf.main.magic == BLOCK_MAGIC_MAIN) {
                                *map = hdl->tmpblock.buf.main.bitmap;
                                *mapsz = sizeof(hdl->tmpblock.buf.main.bitmap);
                        } else {
                                err = EILSEQ;
                        }
                } else {
                        if (hdl->tmpblock.buf.bitmap.magic == BLOCK_MAGIC_BITMAP) {
                                *map = hdl->tmpblock.buf.bitmap.map;
                                *mapsz = sizeof(hdl->tmpblock.buf.bitmap.map);
                        } else {
                                err = EILSEQ;
                        }
                }
        }

        return err;
}

//==============================================================================
/**
 * @brief  Function return number of first block described by bitmap block.
 *
 * @param  bmpblk       bitmap block number (0: main block)
 *
 * @return Block number.
 */
//==============================================================================
static u32_t bmp_base(u16_t bmpblk)
{
        return (bmpblk == 0) ? 0 : (BMP_MAIN_BITS + (cast(u32_t, bmpblk) - 1) * BMP_BLK_BITS);
}

//==============================================================================
/**
 * @brief  Function find empty block by using bitmap.
 *
 * @param  hdl          EEFS handle
 * @param  blknum       found empty block
 *
 * @return One of errno value.
 */
//==============================================================================
static int bmp_block_find_empty(EEFS_t *hdl, uint16_t *blknum)
{
        for (u16_t b = 0; b <= hdl->bitmap_blocks; b++) {

                u32_t base = bmp_base(b);
                if (base >= hdl->blocks) {
                        break;
                }

                u8_t *map = NULL;
                u16_t mapsz = 0;
                int err = bmp_load(hdl, b, &map, &mapsz);
                if (err) {
                        DBG("Invalid bitmap block %u", b);
                        return err;
                }

                for (u16_t i = 0; i < mapsz; i++) {
                        u32_t first = base + (cast(u32_t, i) * BLOCKS_IN_BYTE);

                        if (first >= hdl->blocks) {
                                return ENOSPC;
                        }

                        if (map[i] == 0) {
                                continue;
                        }

                        for (u8_t bit = 0; bit < BLOCKS_IN_BYTE; bit++) {
                                u32_t n = first + bit;

                                if (n >= hdl->blocks) {
                                        return ENOSPC;
                                }

                                // metadata blocks are never given (protection against broken bitmap)
                                if ((map[i] & (1 << bit)) && (n > hdl->root_dir_block)) {
                                        *blknum = n;
                                        return ESUCC;
                                }
                        }
                }
        }

        return ENOSPC;
}

//==============================================================================
/**
 * @brief  Function allocate/release block.
 *
 * @param  hdl          EEFS handle
 * @param  blknum       block number to allocate/release
 * @param  allocate     allocate block if true, otherwise release
 *
 * @return One of errno value.
 */
//==============================================================================
static int bmp_block_alloc_ctrl(EEFS_t *hdl, uint16_t blknum, bool allocate)
{
        if (blknum >= hdl->blocks) {
                return ENOSPC;
        }

        if (blknum <= hdl->root_dir_block) {
                DBG("attempt to %s metadata block %u", allocate ? "alloc" : "free", blknum);
                return EILSEQ;
        }

        u16_t bmpblk;
        u16_t byte;

        if (blknum < BMP_MAIN_BITS) {
                bmpblk = MAIN_BLOCK_ADDR;
                byte = blknum / BLOCKS_IN_BYTE;
        } else {
                u32_t rel = blknum - BMP_MAIN_BITS;
                bmpblk = 1 + (rel / BMP_BLK_BITS);
                byte = (rel % BMP_BLK_BITS) / BLOCKS_IN_BYTE;
        }

        // BMP_MAIN_BITS and BMP_BLK_BITS are multiples of 8
        u8_t mask = 1 << (blknum % BLOCKS_IN_BYTE);

        if (bmpblk > hdl->bitmap_blocks) {
                return ENOSPC; // block not covered by bitmap (old mkeefs rounding)
        }

        u8_t *map = NULL;
        u16_t mapsz = 0;
        int err = bmp_load(hdl, bmpblk, &map, &mapsz);
        if (!err) {
                if (allocate) {
                        if (!(map[byte] & mask)) {
                                return EADDRINUSE;
                        }

                        map[byte] &= ~mask;

                } else {
                        if (map[byte] & mask) {
                                return ESUCC; // already free, no write needed
                        }

                        map[byte] |= mask;
                }

                err = block_write(hdl, &hdl->tmpblock);
        }

        return err;
}

//==============================================================================
/**
 * @brief  Function allocate selected block.
 *
 * @param  hdl          EEFS handle
 * @param  blknum       block to allocate
 *
 * @return One of errno value.
 */
//==============================================================================
static int bmp_block_alloc(EEFS_t *hdl, uint16_t blknum)
{
        return bmp_block_alloc_ctrl(hdl, blknum, true);
}

//==============================================================================
/**
 * @brief  Function free selected block.
 *
 * @param  hdl          EEFS handle
 * @param  blknum       block to free
 *
 * @return One of errno value.
 */
//==============================================================================
static int bmp_block_free(EEFS_t *hdl, uint16_t blknum)
{
        return bmp_block_alloc_ctrl(hdl, blknum, false);
}

//==============================================================================
/**
 * @brief  Function get number of free blocks.
 *
 * @param  hdl          EEFS handle
 * @param  blkfree      number of free blocks
 *
 * @return One of errno value.
 */
//==============================================================================
static int bmp_get_free_blocks(EEFS_t *hdl, u32_t *blkfree)
{
        *blkfree = 0;

        for (u16_t b = 0; b <= hdl->bitmap_blocks; b++) {

                u32_t base = bmp_base(b);
                if (base >= hdl->blocks) {
                        break;
                }

                u8_t *map = NULL;
                u16_t mapsz = 0;
                int err = bmp_load(hdl, b, &map, &mapsz);
                if (err) {
                        return err;
                }

                for (u16_t i = 0; i < mapsz; i++) {
                        for (u8_t bit = 0; bit < BLOCKS_IN_BYTE; bit++) {
                                u32_t n = base + (cast(u32_t, i) * BLOCKS_IN_BYTE) + bit;

                                if (n >= hdl->blocks) {
                                        return ESUCC;
                                }

                                if ((map[i] & (1 << bit)) && (n > hdl->root_dir_block)) {
                                        (*blkfree)++;
                                }
                        }
                }
        }

        return ESUCC;
}

//==============================================================================
/**
 * @brief  Function return next item from path (subdir).
 *
 * @param [in]  path        path
 * @param [out] name        name of subitem
 * @param [out] len         subitem name length
 * @param [out] last        subitem is the last one
 *
 * @return Beginning of subitem pointer (slash position).
 */
//==============================================================================
static const char *path_get_next_item(const char *path, char **name, size_t *len, bool *last)
{
        if (FIRST_CHARACTER(path) == '/') {
                path++;
        }

        if (FIRST_CHARACTER(path) == '\0') {
                return NULL;
        }

        size_t itemlen = 0;
        const char *slash = strchr(path, '/');

        if (slash) {
                itemlen = cast(size_t, slash - path);
                *last = (slash[0] == '/' && slash[1] == '\0');
        } else {
                itemlen = strnlen(path, NAME_LEN);
                slash = path + itemlen;
                *last = true;
        }

        *len = itemlen;
        *name = cast(char *, path);

        return slash;
}

//==============================================================================
/**
 * @brief  Function return pointer to basename and its length. Trailing slash
 *         is not part of basename.
 *
 * @param  path     path ("/a/b", "/a/b/")
 * @param  len      basename length (can be NULL)
 *
 * @return Pointer to basename string.
 */
//==============================================================================
static const char *path_get_basename(const char *path, size_t *len)
{
        size_t end = strlen(path);

        if ((end > 1) && (path[end - 1] == '/')) {
                end--;
        }

        size_t start = end;
        while ((start > 0) && (path[start - 1] != '/')) {
                start--;
        }

        if (len) {
                *len = end - start;
        }

        return path + start;
}

//==============================================================================
/**
 * @brief  Function allocate name for dirname. Dirname ends with slash.
 *         Should be freed by using sys_free().
 *
 * @param  path         path
 * @param  path_base    output of allocated dirname
 *
 * @return One of errno value.
 */
//==============================================================================
static int path_alloc_dirname(const char *path, char **path_base)
{
        size_t baselen = 0;
        const char *base = path_get_basename(path, &baselen);
        size_t sz = cast(size_t, base - path);

        if ((baselen == 0) || (sz == 0)) {
                return ENOENT;
        }

        int err = sys_zalloc(sz + 1, cast(void *, path_base));
        if (!err) {
                memcpy(*path_base, path, sz);
        }

        return err;
}

//==============================================================================
/**
 * @brief  Function gets entry parameters of dir/dir_entry blocks.
 *
 * @param  hdl          EEFS handle
 * @param  dirent       pointer to dir entry
 * @param  items        number of direntries
 * @param  blknext      next block number
 *
 * @return One of errno value.
 */
//==============================================================================
static int dir_get_entry_params(EEFS_t *hdl, dir_entry_t **dirent, u8_t *items, u16_t *blknext)
{
        int err = EILSEQ;

        if (block_is_dir(hdl->block)) {
                *dirent = hdl->block.buf.dir.entry;
                *items = ARRAY_SIZE(hdl->block.buf.dir.entry);
                *blknext = hdl->block.buf.dir.next;
                err = ESUCC;

        } else if (block_is_dir_entry(hdl->block)) {
                *dirent = hdl->block.buf.dir_entry.entry;
                *items = ARRAY_SIZE(hdl->block.buf.dir_entry.entry);
                *blknext = hdl->block.buf.dir_entry.next;
                err = ESUCC;
        } else {
                DBG("invalid dir/dir_entry magic in block %u", hdl->block.num);
        }

        return err;
}

//==============================================================================
/**
 * @brief  Function set next chain pointer of dir/dir_entry block loaded in
 *         block buffer (block is not written).
 *
 * @param  hdl          EEFS handle
 * @param  next         next block number
 *
 * @return One of errno value.
 */
//==============================================================================
static int dir_set_next(EEFS_t *hdl, u16_t next)
{
        if (block_is_dir(hdl->block)) {
                hdl->block.buf.dir.next = next;
                return ESUCC;

        } else if (block_is_dir_entry(hdl->block)) {
                hdl->block.buf.dir_entry.next = next;
                return ESUCC;

        } else {
                return EILSEQ;
        }
}

//==============================================================================
/**
 * @brief  Function adds dir chain to the dir/dir_entry block loaded in block
 *         buffer. New (empty) chain is loaded to block buffer.
 *
 * @param  hdl          EEFS handle
 *
 * @return One of errno value.
 */
//==============================================================================
static int dir_add_chain(EEFS_t *hdl)
{
        u16_t blk = 0;
        int err = bmp_block_find_empty(hdl, &blk);
        if (!err) {
                err = bmp_block_alloc(hdl, blk);
        }

        if (err) {
                return err;
        }

        memset(&hdl->tmpblock.buf, 0xFF, sizeof(hdl->tmpblock.buf));
        hdl->tmpblock.num = blk;
        hdl->tmpblock.buf.dir_entry.next = 0;
        hdl->tmpblock.buf.dir_entry.magic = BLOCK_MAGIC_DIR_ENTRY;

        err = block_write(hdl, &hdl->tmpblock);

        if (!err) {
                err = dir_set_next(hdl, blk);
        }

        if (!err) {
                err = block_write(hdl, &hdl->block);
        }

        if (!err) {
                memcpy(&hdl->block, &hdl->tmpblock, sizeof(hdl->block));
        } else {
                bmp_block_free(hdl, blk);
        }

        return err;
}

//==============================================================================
/**
 * @brief  Function find entry in directory.
 *
 * On success the chain block that contains entry is loaded to block buffer.
 *
 * @param  hdl          EEFS handle
 * @param  blkdir       directory block
 * @param  name         entry name
 * @param  len          name length
 * @param  entry        found entry (pointer to block buffer)
 * @param  blkprev      previous chain block (0 if entry is in directory block)
 * @param  used         number of used entries in the chain block
 *
 * @return One of errno value.
 */
//==============================================================================
static int dir_find_entry(EEFS_t *hdl, u16_t blkdir, const char *name, size_t len, dir_entry_t **entry, u16_t *blkprev,
        u8_t *used)
{
        u16_t guard = hdl->blocks;
        u16_t prev = 0;

        hdl->block.num = blkdir;

        for (;;) {
                int err = block_read(hdl, &hdl->block);
                if (err) {
                        return err;
                }

                dir_entry_t *dirent = NULL;
                u8_t items = 0;
                u16_t blknext = 0;

                err = dir_get_entry_params(hdl, &dirent, &items, &blknext);
                if (err) {
                        return err;
                }

                dir_entry_t *found = NULL;
                u8_t cnt = 0;

                for (u8_t i = 0; i < items; i++, dirent++) {
                        if (is_entry_item_used(dirent)) {
                                cnt++;

                                if (!found && is_entry_name(dirent, name, len)) {
                                        found = dirent;
                                }
                        }
                }

                if (found) {
                        *entry = found;
                        *blkprev = prev;
                        *used = cnt;
                        return ESUCC;
                }

                if (CHAIN_END(blknext)) {
                        return ENOENT;
                }

                if (--guard == 0) {
                        return EILSEQ;
                }

                prev = hdl->block.num;
                hdl->block.num = blknext;
        }
}

//==============================================================================
/**
 * @brief  Function find free slot in directory. If directory is full then new
 *         chain is added. The chain block with slot is loaded to block buffer.
 *
 * @param  hdl          EEFS handle
 * @param  blkdir       directory block
 * @param  slot         free slot (pointer to block buffer)
 *
 * @return One of errno value.
 */
//==============================================================================
static int dir_find_free_slot(EEFS_t *hdl, u16_t blkdir, dir_entry_t **slot)
{
        u16_t guard = hdl->blocks;

        hdl->block.num = blkdir;

        int err = block_read(hdl, &hdl->block);

        while (!err) {
                dir_entry_t *dirent = NULL;
                u8_t items = 0;
                u16_t blknext = 0;

                err = dir_get_entry_params(hdl, &dirent, &items, &blknext);
                if (err) {
                        break;
                }

                for (u8_t i = 0; i < items; i++, dirent++) {
                        if (!is_entry_item_used(dirent)) {
                                *slot = dirent;
                                return ESUCC;
                        }
                }

                if (--guard == 0) {
                        err = EILSEQ;

                } else if (CHAIN_END(blknext)) {
                        err = dir_add_chain(hdl);

                } else {
                        hdl->block.num = blknext;
                        err = block_read(hdl, &hdl->block);
                }
        }

        return err;
}

//==============================================================================
/**
 * @brief  Function remove entry from chain block loaded in block buffer. If
 *         entry is the last one in additional chain block then the block is
 *         unlinked from chain and released.
 *
 * @param  hdl          EEFS handle
 * @param  entry        entry to remove (pointer to block buffer)
 * @param  blkprev      previous chain block (0 if none)
 * @param  used         number of used entries in chain block
 *
 * @return One of errno value.
 */
//==============================================================================
static int dir_detach_entry(EEFS_t *hdl, dir_entry_t *entry, u16_t blkprev, u8_t used)
{
        int err;

        if ((used == 1) && (blkprev != 0) && block_is_dir_entry(hdl->block)) {
                u16_t blkchain = hdl->block.num;
                u16_t blknext = hdl->block.buf.dir_entry.next;

                // unlink first, then release (power-loss safety)
                hdl->block.num = blkprev;
                err = block_read(hdl, &hdl->block);

                if (!err) {
                        err = dir_set_next(hdl, blknext);
                }

                if (!err) {
                        err = block_write(hdl, &hdl->block);
                }

                if (!err) {
                        err = bmp_block_free(hdl, blkchain);
                }

        } else {
                memset(entry, 0, sizeof(dir_entry_t));
                err = block_write(hdl, &hdl->block);
        }

        return err;
}

//==============================================================================
/**
 * @brief  Function update number of items and modification time of directory.
 *         Directory block is loaded to block buffer.
 *
 * @param  hdl          EEFS handle
 * @param  blkdir       directory block
 * @param  delta        number of added (removed) items
 *
 * @return One of errno value.
 */
//==============================================================================
static int dir_update_items(EEFS_t *hdl, u16_t blkdir, int delta)
{
        hdl->block.num = blkdir;
        int err = block_read(hdl, &hdl->block);
        if (!err) {
                if (block_is_dir(hdl->block)) {
                        if (delta < 0) {
                                if (hdl->block.buf.dir.all_items > 0) {
                                        hdl->block.buf.dir.all_items--;
                                }
                        } else if (delta > 0) {
                                hdl->block.buf.dir.all_items++;
                        }

                        time_t time = 0;
                        sys_gettime(&time);
                        hdl->block.buf.dir.mtime = time;

                        err = block_write(hdl, &hdl->block);
                } else {
                        err = EILSEQ;
                }
        }

        return err;
}

//==============================================================================
/**
 * @brief  Function check if directory is empty (entries are scanned, counter
 *         is not trusted). Function uses temporary block buffer.
 *
 * @param  hdl          EEFS handle
 * @param  blkdir       directory block
 *
 * @return ESUCC if empty, ENOTEMPTY if not empty, or other errno value.
 */
//==============================================================================
static int dir_is_empty(EEFS_t *hdl, u16_t blkdir)
{
        u16_t guard = hdl->blocks;

        hdl->tmpblock.num = blkdir;

        for (;;) {
                int err = block_read(hdl, &hdl->tmpblock);
                if (err) {
                        return err;
                }

                dir_entry_t *dirent;
                u8_t items;
                u16_t next;

                if (block_is_dir(hdl->tmpblock)) {
                        dirent = hdl->tmpblock.buf.dir.entry;
                        items = ARRAY_SIZE(hdl->tmpblock.buf.dir.entry);
                        next = hdl->tmpblock.buf.dir.next;

                } else if (block_is_dir_entry(hdl->tmpblock)) {
                        dirent = hdl->tmpblock.buf.dir_entry.entry;
                        items = ARRAY_SIZE(hdl->tmpblock.buf.dir_entry.entry);
                        next = hdl->tmpblock.buf.dir_entry.next;

                } else {
                        return EILSEQ;
                }

                for (u8_t i = 0; i < items; i++) {
                        if (is_entry_item_used(&dirent[i])) {
                                return ENOTEMPTY;
                        }
                }

                if (CHAIN_END(next)) {
                        return ESUCC;
                }

                if (--guard == 0) {
                        return EILSEQ;
                }

                hdl->tmpblock.num = next;
        }
}

//==============================================================================
/**
 * @brief  Function create new object block (directory, file or node).
 *
 * @param  hdl          EEFS handle
 * @param  type         object type (block magic)
 * @param  blkparent    parent directory block
 * @param  mode         object mode
 * @param  dev          device number (node only)
 * @param  blkobj       created object block
 *
 * @return One of errno value.
 */
//==============================================================================
static int obj_create(EEFS_t *hdl, u32_t type, u16_t blkparent, mode_t mode, u32_t dev, u16_t *blkobj)
{
        u16_t blk = 0;
        int err = bmp_block_find_empty(hdl, &blk);
        if (!err) {
                err = bmp_block_alloc(hdl, blk);
        }

        if (err) {
                return err;
        }

        time_t time = 0;
        sys_gettime(&time);

        memset(&hdl->tmpblock.buf, 0xFF, sizeof(hdl->tmpblock.buf));
        hdl->tmpblock.num = blk;

        switch (type) {
        case BLOCK_MAGIC_DIR:
                hdl->tmpblock.buf.dir.magic = type;
                hdl->tmpblock.buf.dir.ctime = time;
                hdl->tmpblock.buf.dir.mtime = time;
                hdl->tmpblock.buf.dir.mode = mode;
                hdl->tmpblock.buf.dir.gid = 0;
                hdl->tmpblock.buf.dir.uid = 0;
                hdl->tmpblock.buf.dir.all_items = 0;
                hdl->tmpblock.buf.dir.next = 0;
                hdl->tmpblock.buf.dir.parent = blkparent;
                break;

        case BLOCK_MAGIC_FILE:
                hdl->tmpblock.buf.file.magic = type;
                hdl->tmpblock.buf.file.ctime = time;
                hdl->tmpblock.buf.file.mtime = time;
                hdl->tmpblock.buf.file.data_next = 0;
                hdl->tmpblock.buf.file.gid = 0;
                hdl->tmpblock.buf.file.uid = 0;
                hdl->tmpblock.buf.file.mode = mode;
                hdl->tmpblock.buf.file.size = 0;
                memset(hdl->tmpblock.buf.file.data, 0, sizeof(hdl->tmpblock.buf.file.data));
                break;

        case BLOCK_MAGIC_NODE:
                hdl->tmpblock.buf.node.magic = type;
                hdl->tmpblock.buf.node.ctime = time;
                hdl->tmpblock.buf.node.mtime = time;
                hdl->tmpblock.buf.node.mode = mode;
                hdl->tmpblock.buf.node.gid = 0;
                hdl->tmpblock.buf.node.uid = 0;
                hdl->tmpblock.buf.node.dev = dev;
                break;

        default:
                err = EINVAL;
                break;
        }

        if (!err) {
                err = block_write(hdl, &hdl->tmpblock);
        }

        if (!err) {
                *blkobj = blk;
        } else {
                bmp_block_free(hdl, blk);
        }

        return err;
}

//==============================================================================
/**
 * @brief  Function release object block (and its data/chain blocks). Object
 *         shall be already removed from directory.
 *
 * @param  hdl          EEFS handle
 * @param  blkobj       object block
 *
 * @return One of errno value.
 */
//==============================================================================
static int obj_release(EEFS_t *hdl, u16_t blkobj)
{
        u16_t chain = 0;

        hdl->block.num = blkobj;
        int err = block_read(hdl, &hdl->block);
        if (!err) {
                if (block_is_file(hdl->block)) {
                        chain = hdl->block.buf.file.data_next;

                } else if (block_is_dir(hdl->block)) {
                        chain = hdl->block.buf.dir.next; // empty chain blocks
                }
        } else {
                DBG("unable to read released object %u (%d)", blkobj, err);
        }

        // object is not referenced anymore, so it can be released first
        err = bmp_block_free(hdl, blkobj);

        // release data/chain blocks (errors only leak blocks, repaired by fsck)
        u16_t guard = hdl->blocks;
        while (!err && !CHAIN_END(chain) && (--guard > 0)) {
                hdl->block.num = chain;
                if (block_read(hdl, &hdl->block) != ESUCC) {
                        break;
                }

                u16_t next;
                if (block_is_file_data(hdl->block)) {
                        next = hdl->block.buf.file_data.data_next;
                } else if (block_is_dir_entry(hdl->block)) {
                        next = hdl->block.buf.dir_entry.next;
                } else {
                        break;
                }

                err = bmp_block_free(hdl, chain);
                chain = next;
        }

        return err;
}

//==============================================================================
/**
 * @brief  Function adds entry to selected dir by path. On success, the created
 *         object is loaded to block buffer.
 *
 * @param  hdl          EEFS handle
 * @param  path         item path to create
 * @param  type         item type (block type)
 * @param  mode         item mode
 * @param  dev          device number (node only)
 *
 * @return One of errno value.
 */
//==============================================================================
static int dir_add_entry(EEFS_t *hdl, const char *path, uint32_t type, mode_t mode, u32_t dev)
{
        size_t namelen = 0;
        const char *name = path_get_basename(path, &namelen);

        if (namelen == 0) {
                return EEXIST; // root directory
        }

        if (namelen >= NAME_LEN) {
                return ENAMETOOLONG;
        }

        if (hdl->flag & FLAG_RDONLY) {
                return EROFS;
        }

        int err = block_load(hdl, path);
        if (!err) {
                return EEXIST;
        } else if (err != ENOENT) {
                return err;
        }

        char *dirname = NULL;
        err = path_alloc_dirname(path, &dirname);
        if (err) {
                return err;
        }

        err = block_load_by_type(hdl, dirname, BLOCK_MAGIC_DIR);
        sys_free(cast(void *, &dirname));

        if (err) {
                return err;
        }

        u16_t blkparent = hdl->block.num;
        u16_t blkobj = 0;

        // 1. new object (allocated and written, not referenced yet)
        err = obj_create(hdl, type, blkparent, mode, dev, &blkobj);
        if (err) {
                return err;
        }

        // 2. reference in directory
        dir_entry_t *slot = NULL;
        err = dir_find_free_slot(hdl, blkparent, &slot);
        if (!err) {
                slot->block_addr = blkobj;
                slot->type = (type == BLOCK_MAGIC_DIR) ? ENTRY_TYPE_DIR
                        : (type == BLOCK_MAGIC_FILE)   ? ENTRY_TYPE_FILE
                                                       : ENTRY_TYPE_NODE;
                memset(slot->name, 0, sizeof(slot->name));
                memcpy(slot->name, name, namelen);

                err = block_write(hdl, &hdl->block);
        }

        if (err) {
                bmp_block_free(hdl, blkobj);
                return err;
        }

        // 3. directory counter (only informative, fsck can repair it)
        err = dir_update_items(hdl, blkparent, 1);

        // 4. created object to block buffer
        if (!err) {
                hdl->block.num = blkobj;
                err = block_read(hdl, &hdl->block);
        }

        return err;
}

//==============================================================================
/**
 * @brief  Function remove entry pointed by path.
 *
 * @param  hdl          EEFS handle
 * @param  path         item path
 *
 * @return One of errno value.
 */
//==============================================================================
static int dir_rm_entry(EEFS_t *hdl, const char *path)
{
        size_t namelen = 0;
        const char *name = path_get_basename(path, &namelen);

        if (namelen == 0) {
                return EBUSY; // root directory
        }

        if (namelen >= NAME_LEN) {
                return ENAMETOOLONG;
        }

        char *dirname = NULL;
        int err = path_alloc_dirname(path, &dirname);
        if (err) {
                return err;
        }

        err = block_load_by_type(hdl, dirname, BLOCK_MAGIC_DIR);
        sys_free(cast(void *, &dirname));

        if (err) {
                return err;
        }

        u16_t blkdir = hdl->block.num;
        dir_entry_t *entry = NULL;
        u16_t blkprev = 0;
        u8_t used = 0;

        err = dir_find_entry(hdl, blkdir, name, namelen, &entry, &blkprev, &used);
        if (err) {
                return err;
        }

        u16_t blkobj = entry->block_addr;

        if (is_object_open(hdl, blkobj)) {
                return EBUSY;
        }

        if (entry->type == ENTRY_TYPE_DIR) {
                err = dir_is_empty(hdl, blkobj); // tmpblock used, entry block is kept
                if (err) {
                        return err;
                }
        }

        if (hdl->flag & FLAG_RDONLY) {
                return EROFS;
        }

        // 1. remove reference
        err = dir_detach_entry(hdl, entry, blkprev, used);

        // 2. directory counter
        if (!err) {
                err = dir_update_items(hdl, blkdir, -1);
        }

        // 3. release object
        if (!err) {
                err = obj_release(hdl, blkobj);
        }

        return err;
}

//==============================================================================
/**
 * @brief  Function rename/move entry.
 *
 * @param  hdl          EEFS handle
 * @param  old_name     old path
 * @param  new_name     new path
 *
 * @return One of errno value.
 */
//==============================================================================
static int dir_rename(EEFS_t *hdl, const char *old_name, const char *new_name)
{
        size_t oldlen = 0;
        size_t newlen = 0;
        const char *oldbase = path_get_basename(old_name, &oldlen);
        const char *newbase = path_get_basename(new_name, &newlen);

        if ((oldlen == 0) || (newlen == 0)) {
                return EBUSY; // root directory
        }

        if ((oldlen >= NAME_LEN) || (newlen >= NAME_LEN)) {
                return ENAMETOOLONG;
        }

        char *dirname = NULL;
        u16_t blkold = 0;
        u16_t blknew = 0;

        int err = path_alloc_dirname(old_name, &dirname);
        if (!err) {
                err = block_load_by_type(hdl, dirname, BLOCK_MAGIC_DIR);
                blkold = hdl->block.num;
                sys_free(cast(void *, &dirname));
        }

        if (!err) {
                err = path_alloc_dirname(new_name, &dirname);
        }

        if (!err) {
                err = block_load_by_type(hdl, dirname, BLOCK_MAGIC_DIR);
                blknew = hdl->block.num;
                sys_free(cast(void *, &dirname));
        }

        if (err) {
                return err;
        }

        dir_entry_t *entry = NULL;
        u16_t blkprev = 0;
        u8_t used = 0;

        err = dir_find_entry(hdl, blkold, oldbase, oldlen, &entry, &blkprev, &used);
        if (err) {
                return err;
        }

        u16_t blkobj = entry->block_addr;
        u8_t type = entry->type;

        // the same name: nothing to do
        if ((blkold == blknew) && (oldlen == newlen) && (memcmp(oldbase, newbase, oldlen) == 0)) {
                return ESUCC;
        }

        // destination cannot exist
        err = dir_find_entry(hdl, blknew, newbase, newlen, &entry, &blkprev, &used);
        if (!err) {
                return EEXIST;
        } else if (err != ENOENT) {
                return err;
        }

        if (hdl->flag & FLAG_RDONLY) {
                return EROFS;
        }

        if (blkold == blknew) {
                // rename in place: single block write
                err = dir_find_entry(hdl, blkold, oldbase, oldlen, &entry, &blkprev, &used);
                if (!err) {
                        memset(entry->name, 0, sizeof(entry->name));
                        memcpy(entry->name, newbase, newlen);
                        err = block_write(hdl, &hdl->block);
                }

                return err;
        }

        // directory cannot be moved to itself or its subdirectory
        if (type == ENTRY_TYPE_DIR) {
                u16_t blk = blknew;
                u16_t guard = hdl->blocks;

                while (blk != hdl->root_dir_block) {
                        if ((blk == blkobj) || (--guard == 0)) {
                                return EINVAL;
                        }

                        hdl->tmpblock.num = blk;
                        err = block_read(hdl, &hdl->tmpblock);
                        if (err) {
                                return err;
                        }

                        if (!block_is_dir(hdl->tmpblock)) {
                                return EILSEQ;
                        }

                        blk = hdl->tmpblock.buf.dir.parent;
                        if ((blk == 0) || (blk >= hdl->blocks)) {
                                break; // broken parent link, root reached
                        }
                }
        }

        // 1. new reference (power loss: object referenced twice, fsck repairs)
        dir_entry_t *slot = NULL;
        err = dir_find_free_slot(hdl, blknew, &slot);
        if (!err) {
                slot->block_addr = blkobj;
                slot->type = type;
                memset(slot->name, 0, sizeof(slot->name));
                memcpy(slot->name, newbase, newlen);
                err = block_write(hdl, &hdl->block);
        }

        if (!err) {
                err = dir_update_items(hdl, blknew, 1);
        }

        // 2. remove old reference
        if (!err) {
                err = dir_find_entry(hdl, blkold, oldbase, oldlen, &entry, &blkprev, &used);
        }

        if (!err) {
                err = dir_detach_entry(hdl, entry, blkprev, used);
        }

        if (!err) {
                err = dir_update_items(hdl, blkold, -1);
        }

        // 3. parent link of moved directory
        if (!err && (type == ENTRY_TYPE_DIR)) {
                hdl->block.num = blkobj;
                err = block_read(hdl, &hdl->block);
                if (!err && block_is_dir(hdl->block)) {
                        hdl->block.buf.dir.parent = blknew;
                        err = block_write(hdl, &hdl->block);
                }
        }

        return err;
}

//==============================================================================
/**
 * @brief  Function get size of dir. Function uses temporary block buffer.
 *
 * @param  hdl          EEFS handle
 * @param  blkdir       directory block
 * @param  dirsize      directory size
 *
 * @return One of errno value.
 */
//==============================================================================
static int dir_get_size(EEFS_t *hdl, u16_t blkdir, u16_t *dirsize)
{
        int err;
        u16_t guard = hdl->blocks;

        *dirsize = 0;
        hdl->tmpblock.num = blkdir;

        do {
                err = block_read(hdl, &hdl->tmpblock);
                if (!err) {
                        *dirsize += sizeof(block_t);

                        u16_t next;

                        if (block_is_dir(hdl->tmpblock)) {
                                next = hdl->tmpblock.buf.dir.next;

                        } else if (block_is_dir_entry(hdl->tmpblock)) {
                                next = hdl->tmpblock.buf.dir_entry.next;

                        } else {
                                err = EILSEQ;
                                break;
                        }

                        if (CHAIN_END(next)) {
                                break;
                        }

                        if (--guard == 0) {
                                err = EILSEQ;
                                break;
                        }

                        hdl->tmpblock.num = next;
                }

        } while (!err);

        return err;
}

//==============================================================================
/**
 * @brief  Function read directory entry.
 *
 * @param  hdl          EEFS handle
 * @param  dd           directory descriptor
 * @param  entry        directory entry
 * @param  dirent       VFS directory entry
 *
 * @return One of errno value.
 */
//==============================================================================
static int dir_read_entry(EEFS_t *hdl, dir_desc_t *dd, dir_entry_t *eefs_entry, dirent_t *dirent)
{
        int err = EILSEQ;

        // name on medium does not have to be terminated
        memcpy(dd->name, eefs_entry->name, sizeof(dd->name) - 1);
        dd->name[sizeof(dd->name) - 1] = '\0';

        dirent->dev = 0;
        hdl->tmpblock.num = eefs_entry->block_addr;

        switch (eefs_entry->type) {
        case ENTRY_TYPE_DIR: {
                err = block_read(hdl, &hdl->tmpblock);
                if (!err && !block_is_dir(hdl->tmpblock)) {
                        err = EILSEQ;
                }

                if (!err) {
                        dirent->mode = S_IFDIR | hdl->tmpblock.buf.dir.mode;

                        u16_t size = 0;
                        err = dir_get_size(hdl, eefs_entry->block_addr, &size);
                        dirent->size = size;
                }
                break;
        }

        case ENTRY_TYPE_FILE:
                err = block_read(hdl, &hdl->tmpblock);
                if (!err && !block_is_file(hdl->tmpblock)) {
                        err = EILSEQ;
                }

                if (!err) {
                        dirent->size = hdl->tmpblock.buf.file.size;
                        dirent->mode = S_IFREG | hdl->tmpblock.buf.file.mode;
                }
                break;

        case ENTRY_TYPE_NODE:
                err = block_read(hdl, &hdl->tmpblock);
                if (!err && !block_is_node(hdl->tmpblock)) {
                        err = EILSEQ;
                }

                if (!err) {
                        dirent->dev = hdl->tmpblock.buf.node.dev;
                        dirent->mode = S_IFDEV | hdl->tmpblock.buf.node.mode;

                        struct vfs_dev_stat dev_stat;
                        err = sys_driver_stat(hdl->tmpblock.buf.node.dev, &dev_stat);
                        if (!err) {
                                dirent->size = dev_stat.st_size;
                        } else if (err == ENODEV) {
                                dirent->size = 0;
                                err = ESUCC;
                        }
                }
                break;
        }

        return err;
}

//==============================================================================
/**
 * @brief  Function truncate file block that is already loaded into block buffer.
 *
 * @param  hdl          EEFS handle
 *
 * @return One of errno value.
 */
//==============================================================================
static int file_truncate(EEFS_t *hdl)
{
        if (block_is_node(hdl->block)) {
                return ESUCC;
        }

        if (!block_is_file(hdl->block)) {
                return EILSEQ;
        }

        u16_t chain = hdl->block.buf.file.data_next;

        if ((hdl->block.buf.file.size == 0) && CHAIN_END(chain)) {
                return ESUCC;
        }

        time_t time = 0;
        sys_gettime(&time);

        // 1. file header does not reference data anymore
        hdl->block.buf.file.size = 0;
        hdl->block.buf.file.data_next = 0;
        hdl->block.buf.file.mtime = time;
        memset(hdl->block.buf.file.data, 0, sizeof(hdl->block.buf.file.data));

        int err = block_write(hdl, &hdl->block);

        // 2. release data blocks (errors only leak blocks, repaired by fsck)
        u16_t guard = hdl->blocks;
        while (!err && !CHAIN_END(chain) && (--guard > 0)) {
                hdl->tmpblock.num = chain;
                if (block_read(hdl, &hdl->tmpblock) != ESUCC) {
                        break;
                }

                if (!block_is_file_data(hdl->tmpblock)) {
                        break;
                }

                u16_t next = hdl->tmpblock.buf.file_data.data_next;

                if (bmp_block_free(hdl, chain) != ESUCC) {
                        break;
                }

                chain = next;
        }

        return err;
}

//==============================================================================
/**
 * @brief  Function add next chain to selected file/file_data block loaded in
 *         block buffer. New (zeroed) data block is loaded to block buffer.
 *
 * @param  hdl          EEFS handle
 *
 * @return One of errno value.
 */
//==============================================================================
static int file_add_chain(EEFS_t *hdl)
{
        u16_t blk = 0;
        int err = bmp_block_find_empty(hdl, &blk);
        if (!err) {
                err = bmp_block_alloc(hdl, blk);
        }

        if (err) {
                return err;
        }

        memset(&hdl->tmpblock, 0, sizeof(hdl->tmpblock));
        hdl->tmpblock.buf.file_data.magic = BLOCK_MAGIC_FILE_DATA;
        hdl->tmpblock.num = blk;

        err = block_write(hdl, &hdl->tmpblock);

        if (!err) {
                if (block_is_file(hdl->block)) {
                        hdl->block.buf.file.data_next = blk;

                } else if (block_is_file_data(hdl->block)) {
                        hdl->block.buf.file_data.data_next = blk;

                } else {
                        err = EILSEQ;
                }
        }

        if (!err) {
                err = block_write(hdl, &hdl->block);
        }

        if (!err) {
                memcpy(&hdl->block, &hdl->tmpblock, sizeof(hdl->block));
        } else {
                bmp_block_free(hdl, blk);
        }

        DBG("added file chain (%d)", err);

        return err;
}

//==============================================================================
/**
 * @brief  Function convert file position to chain index and offset.
 *
 * @param  pos          file position
 * @param  chainpos     chain index (0: file block)
 * @param  blkseek      offset in chain data
 */
//==============================================================================
static void file_locate(u32_t pos, u16_t *chainpos, u16_t *blkseek)
{
        if (pos < FILE_HDR_DATA) {
                *chainpos = 0;
                *blkseek = pos;
        } else {
                u32_t rel = pos - FILE_HDR_DATA;
                *chainpos = 1 + (rel / FILE_BLK_DATA);
                *blkseek = rel % FILE_BLK_DATA;
        }
}

//==============================================================================
/**
 * @brief  Function go to next block of file chain. Current block is loaded in
 *         block buffer. If chain ends and 'extend' is set then new data block
 *         is added.
 *
 * @param  hdl          EEFS handle
 * @param  extend       add new block if chain ends
 *
 * @return One of errno value. ENODATA when chain ends and 'extend' is false.
 */
//==============================================================================
static int file_next_block(EEFS_t *hdl, bool extend)
{
        u16_t next;

        if (block_is_file(hdl->block)) {
                next = hdl->block.buf.file.data_next;

        } else if (block_is_file_data(hdl->block)) {
                next = hdl->block.buf.file_data.data_next;

        } else {
                return EILSEQ;
        }

        if (CHAIN_END(next)) {
                return extend ? file_add_chain(hdl) : ENODATA;
        }

        hdl->block.num = next;
        int err = block_read(hdl, &hdl->block);
        if (!err && !block_is_file_data(hdl->block)) {
                err = EILSEQ;
        }

        return err;
}

//==============================================================================
/**
 * @brief  Function write data chunk to file. File block shall be loaded in
 *         block buffer. After write the file block is loaded in block buffer.
 *
 * @param  hdl          EEFS handle
 * @param  src          source buffer
 * @param  count        number of bytes to write
 * @param  pos          position in file
 * @param  wrcnt        number of wrote bytes
 *
 * @return One of errno value.
 */
//==============================================================================
static int file_write_chunk(EEFS_t *hdl, const u8_t *src, size_t count, u32_t pos, size_t *wrcnt)
{
        int err = ESUCC;
        u16_t baseblk = hdl->block.num;
        u16_t size = hdl->block.buf.file.size;
        u16_t chainpos;
        u16_t blkseek;
        u16_t cur = 0;
        u16_t guard = hdl->blocks;

        // small write to the first block: data and size in one block write
        bool hdr_only = (pos + count) <= FILE_HDR_DATA;

        *wrcnt = 0;
        file_locate(pos, &chainpos, &blkseek);

        while (!err && (count > 0)) {

                while (!err && (cur < chainpos)) {
                        err = file_next_block(hdl, true);
                        cur++;

                        if (--guard == 0) {
                                err = EILSEQ;
                        }
                }

                if (err) {
                        break;
                }

                u8_t *data = (cur == 0) ? hdl->block.buf.file.data : hdl->block.buf.file_data.data;
                u16_t cap = (cur == 0) ? FILE_HDR_DATA : FILE_BLK_DATA;
                u16_t sz = min(cast(size_t, cap - blkseek), count);

                memcpy(data + blkseek, src, sz);

                if (hdr_only) {
                        time_t time = 0;
                        sys_gettime(&time);
                        hdl->block.buf.file.mtime = time;
                        hdl->block.buf.file.size = max(cast(u32_t, size), pos + sz);
                }

                err = block_write(hdl, &hdl->block);

                if (!err) {
                        *wrcnt += sz;
                        src += sz;
                        count -= sz;
                        blkseek = 0;
                        chainpos++;
                }
        }

        if (!hdr_only && (*wrcnt > 0)) {
                hdl->block.num = baseblk;
                int e = block_read(hdl, &hdl->block);
                if (!e) {
                        time_t time = 0;
                        sys_gettime(&time);
                        hdl->block.buf.file.mtime = time;
                        hdl->block.buf.file.size = max(cast(u32_t, size), pos + *wrcnt);
                        e = block_write(hdl, &hdl->block);
                }

                if (!err) {
                        err = e;
                }
        }

        return err;
}

//==============================================================================
/**
 * @brief  Function write data to file loaded in current block.
 *
 * @param  hdl          EEFS handle
 * @param  src          source buffer
 * @param  count        number of bytes to write
 * @param  fpos         position in file
 * @param  wrcnt        number of wrote bytes
 *
 * @return One of errno value.
 */
//==============================================================================
static int file_write(EEFS_t *hdl, const u8_t *src, size_t count, fpos_t *fpos, size_t *wrcnt)
{
        *wrcnt = 0;

        if (count == 0) {
                return ESUCC;
        }

        if (*fpos >= FILE_SIZE_MAX) {
                return EFBIG;
        }

        if (count > cast(size_t, FILE_SIZE_MAX - *fpos)) {
                count = FILE_SIZE_MAX - *fpos;
        }

        u32_t pos = *fpos;
        int err = ESUCC;

        // hole after end of file reads as zeros
        while (!err && (hdl->block.buf.file.size < pos)) {
                u32_t size = hdl->block.buf.file.size;
                size_t n = min(cast(size_t, pos - size), sizeof(zero_block));
                size_t done = 0;

                err = file_write_chunk(hdl, zero_block, n, size, &done);
                if (!err && (done == 0)) {
                        err = EIO;
                }
        }

        if (!err) {
                err = file_write_chunk(hdl, src, count, pos, wrcnt);
        }

        // partial write is reported as success (VFS sets EOF)
        return (*wrcnt > 0) ? ESUCC : err;
}

//==============================================================================
/**
 * @brief  Function read data from selected file loaded in current block.
 *
 * @param  hdl          EEFS handle
 * @param  dst          destination buffer
 * @param  count        number of bytes to read
 * @param  fpos         file position
 * @param  rdcnt        number of read bytes
 *
 * @return One of errno value.
 */
//==============================================================================
static int file_read(EEFS_t *hdl, u8_t *dst, size_t count, fpos_t *fpos, size_t *rdcnt)
{
        int err = ESUCC;
        u16_t size = hdl->block.buf.file.size;
        u16_t chainpos;
        u16_t blkseek;
        u16_t cur = 0;
        u16_t guard = hdl->blocks;

        *rdcnt = 0;

        if (*fpos >= size) {
                return ESUCC;
        }

        if (count > cast(size_t, size - *fpos)) {
                count = size - *fpos;
        }

        file_locate(*fpos, &chainpos, &blkseek);

        while (!err && (count > 0)) {

                while (!err && (cur < chainpos)) {
                        err = file_next_block(hdl, false);
                        cur++;

                        if (--guard == 0) {
                                err = EILSEQ;
                        }
                }

                if (err == ENODATA) {
                        err = ESUCC; // chain shorter than size: EOF
                        break;
                }

                if (err) {
                        break;
                }

                u8_t *data = (cur == 0) ? hdl->block.buf.file.data : hdl->block.buf.file_data.data;
                u16_t cap = (cur == 0) ? FILE_HDR_DATA : FILE_BLK_DATA;
                u16_t sz = min(cast(size_t, cap - blkseek), count);

                memcpy(dst, data + blkseek, sz);

                *rdcnt += sz;
                dst += sz;
                count -= sz;
                blkseek = 0;
                chainpos++;
        }

        return (*rdcnt > 0) ? ESUCC : err;
}

#if __EEFS_CHECK_ENABLE__ > 0
/*==============================================================================
  File system check (mount option "check")
==============================================================================*/
/*
 * Check is performed at mount time when "check" option is given. Whole tree is
 * traversed from the root directory and following problems are repaired:
 *  - broken, looped or cross-linked directory chains (chain is cut),
 *  - empty additional directory chain blocks (unlinked),
 *  - directory entries out of range, referencing already used block, damaged
 *    or wrong type object (entry removed),
 *  - not terminated, empty and duplicated names (entry renamed to "~<block>"),
 *  - directory item counters and parent links,
 *  - file data chains (chain is cut, size limited to existing data),
 *  - bitmap (rebuilt from reachable blocks, damaged bitmap blocks rewritten,
 *    blocks behind the end of medium marked as used),
 *  - orphaned non-empty files and directories (reconnected to /lost+found),
 *  - damaged root directory (recreated empty, content goes to /lost+found).
 *
 * With "ro" option problems are only reported (nothing is written).
 *
 * Memory: 3 bitmaps of (blocks / 8) bytes and 2 block buffers, released after
 * check. Directories are processed iteratively (no recursion).
 */

/**
 * Check context.
 */
typedef struct {
        u8_t *used;   //!< referenced blocks
        u8_t *todo;   //!< directories to check
        u8_t *orphan; //!< orphans to reconnect
        u32_t problems;
        bool dry;     //!< report only (read-only mount)
        bool bmp_tmp; //!< bitmap block loaded in temporary buffer (orphan scan)
        block_buf_t prev;
        block_buf_t obj;
} fs_check_t;

#define CHK_MSG(chk, ...)                  \
        do {                               \
                (chk)->problems++;         \
                MSG("check: "__VA_ARGS__); \
        } while (0)

//==============================================================================
/**
 * @brief  Bitmap bit operations of check context.
 */
//==============================================================================
static bool chk_bit(const u8_t *map, u32_t n)
{
        return (map[n / 8] >> (n % 8)) & 1;
}

static void chk_set(u8_t *map, u32_t n, bool val)
{
        if (val) {
                map[n / 8] |= (1 << (n % 8));
        } else {
                map[n / 8] &= ~(1 << (n % 8));
        }
}

//==============================================================================
/**
 * @brief  Write block repaired by check (skipped in read-only mode).
 */
//==============================================================================
static int chk_write(EEFS_t *hdl, fs_check_t *chk, block_buf_t *blk)
{
        return chk->dry ? ESUCC : block_write(hdl, blk);
}

//==============================================================================
/**
 * @brief  Check if block can contain object or chain.
 */
//==============================================================================
static bool chk_is_data_block(EEFS_t *hdl, u32_t n)
{
        return (n > hdl->root_dir_block) && (n < hdl->blocks);
}

//==============================================================================
/**
 * @brief  Set name "~<block>" (used for invalid and duplicated names).
 */
//==============================================================================
static void chk_make_name(char name[NAME_LEN], u16_t blk)
{
        char digits[6];
        int n = 0;

        do {
                digits[n++] = '0' + (blk % 10);
                blk /= 10;
        } while (blk && (n < cast(int, sizeof(digits))));

        memset(name, 0, NAME_LEN);
        name[0] = '~';

        for (int i = 0; i < n; i++) {
                name[1 + i] = digits[n - 1 - i];
        }
}

//==============================================================================
/**
 * @brief  Check if name is used by entry placed in directory chain before
 *         selected entry. The chain block with entry is in block buffer.
 *         Function uses temporary block buffer.
 */
//==============================================================================
static bool chk_name_used_before(EEFS_t *hdl, u16_t blkdir, const dir_entry_t *entry)
{
        size_t len = strnlen(entry->name, NAME_LEN);
        u16_t guard = hdl->blocks;

        hdl->tmpblock.num = blkdir;

        while (hdl->tmpblock.num != hdl->block.num) {
                if (block_read(hdl, &hdl->tmpblock) != ESUCC) {
                        return false;
                }

                dir_entry_t *ent;
                u8_t items;
                u16_t next;

                if (block_is_dir(hdl->tmpblock)) {
                        ent = hdl->tmpblock.buf.dir.entry;
                        items = ARRAY_SIZE(hdl->tmpblock.buf.dir.entry);
                        next = hdl->tmpblock.buf.dir.next;
                } else if (block_is_dir_entry(hdl->tmpblock)) {
                        ent = hdl->tmpblock.buf.dir_entry.entry;
                        items = ARRAY_SIZE(hdl->tmpblock.buf.dir_entry.entry);
                        next = hdl->tmpblock.buf.dir_entry.next;
                } else {
                        return false;
                }

                for (u8_t i = 0; i < items; i++) {
                        if (is_entry_item_used(&ent[i]) && is_entry_name(&ent[i], entry->name, len)) {
                                return true;
                        }
                }

                if (CHAIN_END(next) || (--guard == 0)) {
                        return false;
                }

                hdl->tmpblock.num = next;
        }

        // entries of current chain block placed before entry
        dir_entry_t *ent;
        if (block_is_dir(hdl->block)) {
                ent = hdl->block.buf.dir.entry;
        } else {
                ent = hdl->block.buf.dir_entry.entry;
        }

        for (; ent < entry; ent++) {
                if (is_entry_item_used(ent) && is_entry_name(ent, entry->name, len)) {
                        return true;
                }
        }

        return false;
}

//==============================================================================
/**
 * @brief  Check file data chain. File block is loaded in chk->obj.
 *         Function uses temporary block buffer.
 */
//==============================================================================
static int chk_file(EEFS_t *hdl, fs_check_t *chk)
{
        u32_t capacity = FILE_HDR_DATA;
        u16_t prev = chk->obj.num;
        u16_t next = chk->obj.buf.file.data_next;
        int err = ESUCC;

        while (!CHAIN_END(next)) {
                bool ok = chk_is_data_block(hdl, next) && !chk_bit(chk->used, next);

                if (ok) {
                        hdl->tmpblock.num = next;
                        err = block_read(hdl, &hdl->tmpblock);
                        if (err && (err != EILSEQ)) {
                                return err;
                        }

                        ok = !err && block_is_file_data(hdl->tmpblock);
                        err = ESUCC;
                }

                if (!ok) {
                        CHK_MSG(chk, "file block %u: broken data chain at block %u (cut)", chk->obj.num, next);

                        if (prev == chk->obj.num) {
                                chk->obj.buf.file.data_next = 0;
                                err = chk_write(hdl, chk, &chk->obj);

                        } else {
                                hdl->tmpblock.num = prev;
                                err = block_read(hdl, &hdl->tmpblock);
                                if (!err) {
                                        hdl->tmpblock.buf.file_data.data_next = 0;
                                        err = chk_write(hdl, chk, &hdl->tmpblock);
                                }
                        }
                        break;
                }

                chk_set(chk->used, next, true);
                chk_set(chk->orphan, next, false);
                capacity += FILE_BLK_DATA;
                prev = next;
                next = hdl->tmpblock.buf.file_data.data_next;
        }

        if (!err && (chk->obj.buf.file.size > capacity)) {
                CHK_MSG(chk, "file block %u: size %u larger than data (%u)", chk->obj.num, chk->obj.buf.file.size,
                        cast(uint, capacity));
                chk->obj.buf.file.size = capacity;
                err = chk_write(hdl, chk, &chk->obj);
        }

        return err;
}

//==============================================================================
/**
 * @brief  Check directory entry. The chain block with entry is in block buffer.
 *
 * @return ESUCC: entry is correct, ENOENT: entry shall be removed, or errno.
 */
//==============================================================================
static int chk_entry(EEFS_t *hdl, fs_check_t *chk, u16_t blkdir, dir_entry_t *e, bool *dirty)
{
        u16_t addr = e->block_addr;

        if (memchr(e->name, '\0', NAME_LEN) == NULL) {
                e->name[NAME_LEN - 1] = '\0';
                CHK_MSG(chk, "dir %u: name not terminated, truncated to '%s'", blkdir, e->name);
                *dirty = true;
        }

        if ((e->name[0] == '\0') || strchr(e->name, '/')) {
                chk_make_name(e->name, addr);
                CHK_MSG(chk, "dir %u: invalid name, renamed to '%s'", blkdir, e->name);
                *dirty = true;
        }

        if (!chk_is_data_block(hdl, addr)) {
                CHK_MSG(chk, "dir %u: '%s' points out of range (%u), removed", blkdir, e->name, addr);
                return ENOENT;
        }

        if (chk_bit(chk->used, addr)) {
                CHK_MSG(chk, "dir %u: '%s' references already used block %u, removed", blkdir, e->name, addr);
                return ENOENT;
        }

        chk->obj.num = addr;
        int err = block_read(hdl, &chk->obj);
        if (err == EILSEQ) {
                CHK_MSG(chk, "dir %u: '%s' object block %u damaged, removed", blkdir, e->name, addr);
                return ENOENT;
        } else if (err) {
                return err;
        }

        u32_t expected = (e->type == ENTRY_TYPE_DIR) ? BLOCK_MAGIC_DIR
                : (e->type == ENTRY_TYPE_FILE)       ? BLOCK_MAGIC_FILE
                                                     : BLOCK_MAGIC_NODE;

        if (chk->obj.buf.dir.magic != expected) {
                CHK_MSG(chk, "dir %u: '%s' type does not match block %u, removed", blkdir, e->name, addr);
                return ENOENT;
        }

        if (chk_name_used_before(hdl, blkdir, e)) {
                char old[NAME_LEN];
                memcpy(old, e->name, NAME_LEN);
                chk_make_name(e->name, addr);
                CHK_MSG(chk, "dir %u: duplicated name '%s', renamed to '%s'", blkdir, old, e->name);
                *dirty = true;
        }

        chk_set(chk->used, addr, true);
        chk_set(chk->orphan, addr, false);

        if (e->type == ENTRY_TYPE_FILE) {
                err = chk_file(hdl, chk);

        } else if (e->type == ENTRY_TYPE_DIR) {
                chk_set(chk->todo, addr, true);

                if (chk->obj.buf.dir.parent != blkdir) {
                        CHK_MSG(chk, "dir %u: parent link %u, real %u", addr, chk->obj.buf.dir.parent, blkdir);
                        chk->obj.buf.dir.parent = blkdir;
                        err = chk_write(hdl, chk, &chk->obj);
                }
        }

        return err;
}

//==============================================================================
/**
 * @brief  Check directory: chain, entries and item counter.
 */
//==============================================================================
static int chk_dir(EEFS_t *hdl, fs_check_t *chk, u16_t blkdir)
{
        u16_t count = 0;
        u16_t guard = hdl->blocks;

        hdl->block.num = blkdir;
        int err = block_read(hdl, &hdl->block);
        if (err) {
                return err; // validated before
        }

        chk->prev.num = 0;

        while (!err) {
                dir_entry_t *ent = NULL;
                u8_t items = 0;
                u16_t next = 0;
                bool dirty = false;
                u8_t used_here = 0;

                err = dir_get_entry_params(hdl, &ent, &items, &next);
                if (err) {
                        break;
                }

                for (u8_t i = 0; !err && (i < items); i++) {
                        if (!is_entry_item_used(&ent[i])) {
                                continue;
                        }

                        err = chk_entry(hdl, chk, blkdir, &ent[i], &dirty);
                        if (err == ENOENT) {
                                memset(&ent[i], 0, sizeof(dir_entry_t));
                                dirty = true;
                                err = ESUCC;
                        } else if (!err) {
                                used_here++;
                                count++;
                        }
                }

                if (err) {
                        break;
                }

                if ((hdl->block.num != blkdir) && (used_here == 0)) {
                        // empty additional chain block: unlink (block released by bitmap rebuild)
                        CHK_MSG(chk, "dir %u: empty chain block %u removed", blkdir, hdl->block.num);
                        chk_set(chk->used, hdl->block.num, false);

                        memcpy(&hdl->tmpblock, &chk->prev, sizeof(block_buf_t));
                        memcpy(&chk->prev, &hdl->block, sizeof(block_buf_t));
                        memcpy(&hdl->block, &hdl->tmpblock, sizeof(block_buf_t));

                        err = dir_set_next(hdl, next);
                        if (!err) {
                                err = chk_write(hdl, chk, &hdl->block);
                        }

                        memcpy(&chk->prev, &hdl->block, sizeof(block_buf_t));

                } else {
                        if (dirty) {
                                err = chk_write(hdl, chk, &hdl->block);
                        }

                        memcpy(&chk->prev, &hdl->block, sizeof(block_buf_t));
                }

                if (err || CHAIN_END(next)) {
                        break;
                }

                bool ok = chk_is_data_block(hdl, next) && !chk_bit(chk->used, next) && (--guard > 0);
                if (ok) {
                        hdl->block.num = next;
                        err = block_read(hdl, &hdl->block);
                        if (err && (err != EILSEQ)) {
                                break;
                        }

                        ok = !err && block_is_dir_entry(hdl->block);
                        err = ESUCC;
                }

                if (!ok) {
                        CHK_MSG(chk, "dir %u: broken chain link to block %u (cut)", blkdir, next);
                        memcpy(&hdl->block, &chk->prev, sizeof(block_buf_t));
                        err = dir_set_next(hdl, 0);
                        if (!err) {
                                err = chk_write(hdl, chk, &hdl->block);
                        }
                        break;
                }

                chk_set(chk->used, next, true);
                chk_set(chk->orphan, next, false);
        }

        if (!err) {
                hdl->block.num = blkdir;
                err = block_read(hdl, &hdl->block);

                if (!err && (hdl->block.buf.dir.all_items != count)) {
                        CHK_MSG(chk, "dir %u: item counter %u, real %u", blkdir, hdl->block.buf.dir.all_items, count);
                        hdl->block.buf.dir.all_items = count;
                        err = chk_write(hdl, chk, &hdl->block);
                }
        }

        return err;
}

//==============================================================================
/**
 * @brief  Check all directories marked in todo bitmap.
 */
//==============================================================================
static int chk_tree(EEFS_t *hdl, fs_check_t *chk)
{
        int err = ESUCC;
        bool found = true;

        while (!err && found) {
                found = false;

                for (u32_t n = hdl->root_dir_block; !err && (n < hdl->blocks); n++) {
                        if (chk_bit(chk->todo, n)) {
                                chk_set(chk->todo, n, false);
                                err = chk_dir(hdl, chk, n);
                                found = true;
                        }
                }
        }

        return err;
}

//==============================================================================
/**
 * @brief  Check if block is allocated in medium bitmap. Function uses
 *         temporary block buffer.
 */
//==============================================================================
static bool chk_is_allocated(EEFS_t *hdl, fs_check_t *chk, u32_t n)
{
        u16_t bmpblk;
        u16_t byte;

        if (n < BMP_MAIN_BITS) {
                bmpblk = MAIN_BLOCK_ADDR;
                byte = n / BLOCKS_IN_BYTE;
        } else {
                u32_t rel = n - BMP_MAIN_BITS;
                bmpblk = 1 + (rel / BMP_BLK_BITS);
                byte = (rel % BMP_BLK_BITS) / BLOCKS_IN_BYTE;
        }

        if (bmpblk > hdl->bitmap_blocks) {
                return false;
        }

        u8_t *map = NULL;
        u16_t mapsz = 0;

        if (chk->bmp_tmp && (hdl->tmpblock.num == bmpblk)) {
                map = (bmpblk == MAIN_BLOCK_ADDR) ? hdl->tmpblock.buf.main.bitmap : hdl->tmpblock.buf.bitmap.map;
        } else {
                chk->bmp_tmp = (bmp_load(hdl, bmpblk, &map, &mapsz) == ESUCC);
                if (!chk->bmp_tmp) {
                        return false; // unknown: no orphan recovery
                }
        }

        return !(map[byte] & (1 << (n % BLOCKS_IN_BYTE)));
}

//==============================================================================
/**
 * @brief  Find orphaned non-empty objects (allocated in bitmap, not
 *         referenced). Directories are checked as subtrees. Orphans are
 *         marked in orphan bitmap.
 */
//==============================================================================
static int chk_find_orphans(EEFS_t *hdl, fs_check_t *chk)
{
        int err = ESUCC;
        u32_t candidates = 0;

        chk->bmp_tmp = false; // temporary buffer is used only by chk_is_allocated() in this loop

        for (u32_t n = hdl->root_dir_block + 1; !err && (n < hdl->blocks); n++) {
                if (chk_bit(chk->used, n) || !chk_is_allocated(hdl, chk, n)) {
                        continue;
                }

                chk->obj.num = n;
                err = block_read(hdl, &chk->obj);
                if (err) {
                        err = (err == EILSEQ) ? ESUCC : err;
                        continue;
                }

                bool content = false;

                if (block_is_file(chk->obj)) {
                        content = (chk->obj.buf.file.size > 0) || !CHAIN_END(chk->obj.buf.file.data_next);

                } else if (block_is_dir(chk->obj)) {
                        content = !CHAIN_END(chk->obj.buf.dir.next);
                        for (u8_t i = 0; i < ARRAY_SIZE(chk->obj.buf.dir.entry); i++) {
                                content |= is_entry_item_used(&chk->obj.buf.dir.entry[i]);
                        }
                }

                if (content) {
                        chk_set(chk->orphan, n, true);
                        candidates++;
                }
        }

        if (err || (candidates == 0)) {
                return err;
        }

        // directories first, top-level orphan directories before their children
        for (int pass = 0; !err && (pass < 2); pass++) {
                bool progress = true;

                while (!err && progress) {
                        progress = false;

                        for (u32_t n = hdl->root_dir_block + 1; !err && (n < hdl->blocks); n++) {
                                if (!chk_bit(chk->orphan, n) || chk_bit(chk->used, n)) {
                                        continue;
                                }

                                chk->obj.num = n;
                                if ((block_read(hdl, &chk->obj) != ESUCC) || !block_is_dir(chk->obj)) {
                                        continue;
                                }

                                u16_t p = chk->obj.buf.dir.parent;
                                bool parent_orphan = (p < hdl->blocks) && (p != n) && chk_bit(chk->orphan, p) &&
                                        !chk_bit(chk->used, p);

                                if ((pass == 1) || !parent_orphan) {
                                        chk_set(chk->used, n, true); // orphan bit stays: to reconnect
                                        chk_set(chk->todo, n, true);
                                        err = chk_tree(hdl, chk);
                                        progress = true;
                                }
                        }
                }
        }

        // files (data chains are checked)
        for (u32_t n = hdl->root_dir_block + 1; !err && (n < hdl->blocks); n++) {
                if (!chk_bit(chk->orphan, n) || chk_bit(chk->used, n)) {
                        continue;
                }

                chk->obj.num = n;
                if ((block_read(hdl, &chk->obj) == ESUCC) && block_is_file(chk->obj)) {
                        chk_set(chk->used, n, true);
                        err = chk_file(hdl, chk);
                } else {
                        chk_set(chk->orphan, n, false);
                }
        }

        return err;
}

//==============================================================================
/**
 * @brief  Rebuild bitmap from referenced blocks.
 */
//==============================================================================
static int chk_bitmap(EEFS_t *hdl, fs_check_t *chk, bool damaged)
{
        u32_t leaked = 0;
        u32_t unsafe = 0;
        u32_t beyond = 0;
        int err = ESUCC;

        for (u16_t b = 0; !err && (b <= hdl->bitmap_blocks); b++) {
                u8_t *map = NULL;
                u16_t mapsz = 0;
                bool rebuild = false;

                err = bmp_load(hdl, b, &map, &mapsz);
                if (err == EILSEQ) {
                        if (b == MAIN_BLOCK_ADDR) {
                                break; // main block is validated at mount
                        }

                        CHK_MSG(chk, "bitmap block %u damaged, rebuilt", b);
                        memset(&hdl->tmpblock.buf, 0xFF, sizeof(hdl->tmpblock.buf));
                        hdl->tmpblock.num = b;
                        hdl->tmpblock.buf.bitmap.magic = BLOCK_MAGIC_BITMAP;
                        map = hdl->tmpblock.buf.bitmap.map;
                        mapsz = sizeof(hdl->tmpblock.buf.bitmap.map);
                        rebuild = true;
                        err = ESUCC;
                }

                if (err) {
                        break;
                }

                bool dirty = rebuild;
                u32_t base = bmp_base(b);

                for (u32_t i = 0; i < (cast(u32_t, mapsz) * BLOCKS_IN_BYTE); i++) {
                        u32_t n = base + i;
                        u8_t mask = 1 << (i % BLOCKS_IN_BYTE);
                        bool disk_free = map[i / BLOCKS_IN_BYTE] & mask;
                        bool real_free = (n < hdl->blocks) && (n > hdl->root_dir_block) && !chk_bit(chk->used, n);

                        if (disk_free == real_free) {
                                continue;
                        }

                        if (!rebuild) {
                                if (n >= hdl->blocks) {
                                        beyond++;
                                } else if (real_free) {
                                        leaked++;
                                } else {
                                        unsafe++;
                                }
                        }

                        if (real_free) {
                                map[i / BLOCKS_IN_BYTE] |= mask;
                        } else {
                                map[i / BLOCKS_IN_BYTE] &= ~mask;
                        }

                        dirty = true;
                }

                if (dirty) {
                        err = chk_write(hdl, chk, &hdl->tmpblock);
                }
        }

        UNUSED_ARG1(damaged);

        if (leaked) {
                CHK_MSG(chk, "bitmap: %u not referenced blocks marked as used", cast(uint, leaked));
        }

        if (unsafe) {
                CHK_MSG(chk, "bitmap: %u blocks in use marked as free", cast(uint, unsafe));
        }

        if (beyond) {
                CHK_MSG(chk, "bitmap: %u not existing blocks behind the end marked as free", cast(uint, beyond));
        }

        return err;
}

//==============================================================================
/**
 * @brief  Reconnect orphans to /lost+found (bitmap shall be consistent).
 */
//==============================================================================
static int chk_reconnect(EEFS_t *hdl, fs_check_t *chk)
{
        int err = ESUCC;
        u16_t blklf = 0;

        for (u32_t n = hdl->root_dir_block + 1; !err && (n < hdl->blocks); n++) {
                if (!chk_bit(chk->orphan, n)) {
                        continue;
                }

                if (chk->dry) {
                        CHK_MSG(chk, "orphan in block %u (not reconnected, read-only)", cast(uint, n));
                        continue;
                }

                if (blklf == 0) {
                        err = block_load(hdl, "/" LOST_FOUND_NAME);
                        if (err == ENOENT) {
                                err = dir_add_entry(hdl, "/" LOST_FOUND_NAME, BLOCK_MAGIC_DIR, DEFAULT_MODE, 0);
                        }

                        if (!err && !block_is_dir(hdl->block)) {
                                err = ENOTDIR;
                        }

                        if (err) {
                                MSG("check: unable to create /" LOST_FOUND_NAME " (%d)", err);
                                break;
                        }

                        blklf = hdl->block.num;
                }

                chk->obj.num = n;
                err = block_read(hdl, &chk->obj);
                if (err) {
                        break;
                }

                u8_t type = block_is_dir(chk->obj) ? ENTRY_TYPE_DIR : ENTRY_TYPE_FILE;

                dir_entry_t *slot = NULL;
                err = dir_find_free_slot(hdl, blklf, &slot);
                if (!err) {
                        slot->block_addr = n;
                        slot->type = type;
                        chk_make_name(slot->name, n);
                        slot->name[0] = '#';
                        err = block_write(hdl, &hdl->block);
                }

                if (!err) {
                        err = dir_update_items(hdl, blklf, 1);
                }

                if (!err && (type == ENTRY_TYPE_DIR)) {
                        chk->obj.buf.dir.parent = blklf;
                        err = block_write(hdl, &chk->obj);
                }

                if (!err) {
                        CHK_MSG(chk, "orphan %s in block %u reconnected as /" LOST_FOUND_NAME "/#%u",
                                (type == ENTRY_TYPE_DIR) ? "directory" : "file", cast(uint, n), cast(uint, n));
                }
        }

        return err;
}

//==============================================================================
/**
 * @brief  Check and repair file system (mount option "check").
 *
 * @param  hdl              EEFS handle
 * @param  bitmap_damaged   damaged bitmap block(s) found at mount
 *
 * @return One of errno value.
 */
//==============================================================================
static int fs_check(EEFS_t *hdl, bool bitmap_damaged)
{
        fs_check_t *chk = NULL;
        size_t mapsz = CEILING(cast(size_t, hdl->blocks), BLOCKS_IN_BYTE);

        int err = sys_zalloc(sizeof(fs_check_t) + (3 * mapsz), cast(void *, &chk));
        if (err) {
                MSG("check: not enough memory");
                return err;
        }

        chk->used = cast(u8_t *, &chk[1]);
        chk->todo = chk->used + mapsz;
        chk->orphan = chk->todo + mapsz;
        chk->dry = (hdl->flag & FLAG_RDONLY);

        for (u32_t n = 0; n <= hdl->root_dir_block; n++) {
                chk_set(chk->used, n, true);
        }

        // root directory
        hdl->block.num = hdl->root_dir_block;
        err = block_read(hdl, &hdl->block);

        if ((err == EILSEQ) || (!err && !block_is_dir(hdl->block))) {
                if (chk->dry) {
                        MSG("check: root directory damaged, cannot be repaired in read-only mode");
                        err = EMEDIUMTYPE;
                        goto finish;
                }

                CHK_MSG(chk, "root directory damaged, empty one created (content in /" LOST_FOUND_NAME ")");

                time_t time = 0;
                sys_gettime(&time);

                memset(&hdl->block.buf, 0xFF, sizeof(hdl->block.buf));
                hdl->block.num = hdl->root_dir_block;
                hdl->block.buf.dir.magic = BLOCK_MAGIC_DIR;
                hdl->block.buf.dir.ctime = time;
                hdl->block.buf.dir.mtime = time;
                hdl->block.buf.dir.mode = DEFAULT_MODE;
                hdl->block.buf.dir.gid = 0;
                hdl->block.buf.dir.uid = 0;
                hdl->block.buf.dir.parent = 0;
                hdl->block.buf.dir.next = 0;
                hdl->block.buf.dir.all_items = 0;

                err = block_write(hdl, &hdl->block);
        }

        if (err) {
                goto finish;
        }

        // tree
        chk_set(chk->todo, hdl->root_dir_block, true);
        err = chk_tree(hdl, chk);

        // orphans and bitmap
        if (!err) {
                err = chk_find_orphans(hdl, chk);
        }

        if (!err) {
                err = chk_bitmap(hdl, chk, bitmap_damaged);
        }

        if (!err) {
                err = chk_reconnect(hdl, chk);
        }

finish:
        if (!err) {
                if (chk->problems) {
                        MSG("check: %u problem(s) %s", cast(uint, chk->problems), chk->dry ? "found" : "fixed");
                } else {
                        MSG("check: file system clean");
                }
        } else {
                MSG("check: error %d", err);
        }

        sys_free(cast(void *, &chk));

        return err;
}
#endif /* __EEFS_CHECK_ENABLE__ > 0 */

/*==============================================================================
  End of file
==============================================================================*/
