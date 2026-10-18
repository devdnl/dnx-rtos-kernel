/*==============================================================================
File    flatfs.c

Author  daniel.zorychta@gmail.com

Brief   Daniel Zorychta

        Copyright (C) 2021 Daniel Zorychta <daniel.zorychta@gmail.com>

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
#include "fs/fs.h"
#include <assert.h>

/*==============================================================================
  Local macros
==============================================================================*/
#if __FLATFS_LOG_ENABLE__ > 0
#define DBG(...) MSG(__VA_ARGS__)
#else
#define DBG(...)
#endif

#define MSG(...)              printk("FLATFS: "__VA_ARGS__)

#define MTX_TIMEOUT           60000

#define MAGIC_BLOCK_FS        0x54414C46
#define MAGIC_BLOCK_FILE      0x454C4946
#define MAGIC_BLOCK_FILE_DATA 0x41544144

#define BLOCK_SIZE            512
#define CRC32_INIT            0xFFFFFFFF

/* Number of non-file blocks: FS descriptor + 2x bitmap */
#define SYSTEM_BLOCKS         3

/* Maximum length of stored name (with NUL), equal to blk_file_t::name size. */
#define MAX_FILE_NAME_LEN     sizeof(((blk_file_t *)0)->name)

#define BIT(_n)               (1u << (_n))
#define BITMAP_WORD_BITS      (ARRAY_ITEM_SIZE(((blk_bitmap_t *)0)->word) * 8)
#define BITMAP_BITS           (sizeof(((blk_bitmap_t *)0)->word) * 8)

/*
 * Iterate over file nodes starting from _start_node up to last node of volume.
 *
 * NOTE: macro is built from nested loops - `break` inside loop body works like
 *       `continue`! Use `goto` to leave the loop. Macro declares local
 *       variables, so use it only once in the scope (put it in a block `{}`).
 */
#define foreach_file_node(_hdl, _start_node)                                                    \
        u32_t _start = (_start_node);                                                           \
        u32_t _max_files = (_start < (_hdl)->total_files) ? ((_hdl)->total_files - _start) : 0; \
        size_t i = _start / BITMAP_WORD_BITS;                                                   \
        size_t bit = _start % BITMAP_WORD_BITS;                                                 \
        for (; (i < ARRAY_SIZE((_hdl)->bitmap.word)) and (_max_files > 0); i++, bit = 0)        \
                for (; (bit < BITMAP_WORD_BITS) and (_max_files > 0); bit++, _max_files--)      \
                        for (u32_t node = (BITMAP_WORD_BITS * i) + bit; node < BITMAP_BITS;     \
                                node = UINT32_MAX)

#define foreach_used_file_node(hdl, start_node) \
        foreach_file_node(hdl, start_node) if ((hdl)->bitmap.word[i] & BIT(bit))

#define FIND_ANY_NODE     UINT32_MAX

#define INDEX_HIT_MAX     (100 * __OS_SYSTEM_CACHE_SYNC_PERIOD__)
#define INDEX_HIT_MIN     (-INDEX_HIT_MAX)

/*==============================================================================
  Local object types
==============================================================================*/
/*
 * File system descriptor block.
 */
typedef union {
        struct {
                u32_t magic;                    // Block magic number: MAGIC_BLOCK_FS
                u32_t crc;                      // CRC32 calculated starting from field below up to block end
                u32_t total_volume_blocks;      // Total number of block with FS and bitmaps
                u32_t file_node_blocks;         // Number of blocks per file
                u32_t last_file_node_blocks;    // Number of blocks of last file
                u32_t bitmap_address_primary;   // Address of primary bitmap
                u32_t bitmap_address_secondary; // Address of secondary bitmap
                u32_t first_file_node_address;  // Address of first file entry block
                u32_t total_files;              // Total number of files
                u32_t version;                  // FS version
        };

        u8_t block[BLOCK_SIZE];
} blk_fs_t;

/*
 * File descriptor block.
 */
typedef union {
        struct {
                u32_t magic;          // Block magic number: MAGIC_BLOCK_FILE
                u32_t crc;            // CRC32 calculated starting from field below up to block end
                u64_t ctime;          // time of creation
                u64_t mtime;          // time of last modification
                mode_t mode;          // file mode
                uid_t uid;            // user ID of owner
                gid_t gid;            // group ID of owner
                u32_t size;           // File size in bytes
                char name[312];       // file name
                u32_t next_data_node; // next data node
                u32_t reserved;       // reserved
        };

        u8_t block[BLOCK_SIZE];
} blk_file_t;

static_assert(sizeof(blk_file_t) == BLOCK_SIZE, "blk_file_t is bigger than BLOCK_SIZE!");
static_assert(sizeof(mode_t) >= sizeof(u32_t), "FIND_ANY_NODE requires at least 32-bit mode_t!");

/*
 * File data descriptor block.
 *
 * Reserved for future use: chaining of data nodes when file size exceeds
 * capacity of a single file node (see blk_file_t::next_data_node).
 */
typedef union {
        struct {
                u32_t magic;          // Block magic number: MAGIC_BLOCK_FILE_DATA
                u32_t crc;            // CRC32 calculated starting from field below up to block end
                u32_t next_data_node; // next data node
        };

        u8_t block[BLOCK_SIZE];
} blk_file_data_t;

/*
 * Bitmap block.
 */
typedef struct {
        u32_t word[BLOCK_SIZE / sizeof(u32_t)];
} blk_bitmap_t;

/*
 * File handle.
 */
typedef struct {
        u32_t node_num;
} file_t;

/*
 * Directory handle.
 */
typedef struct {
        char *dir_root;
        char *item_name;
        llist_t *last_dirs;
        uint32_t dir_root_len;
        uint32_t seek;
} dir_t;

/*
 * Type contains block cache buffer.
 */
typedef struct {
        u32_t node;
        i32_t hit;
        blk_file_t blk;
} path_index_t;

/*
 * File system handle.
 */
typedef struct {
        kmtx_t *mtx;
        kfile_t *disc;
        llist_t *open_files;
        size_t open_dirs_ctr;
        bool read_only;
        bool force_check;
        bool noatime;
        u32_t total_files;
        u32_t total_volume_blocks;
        u32_t file_node_blocks;
        u32_t last_file_node_blocks;
        u32_t bitmap_address_primary;
        u32_t bitmap_address_secondary;
        u32_t first_file_node_address;
        blk_bitmap_t bitmap;
        char tmp_path[MAX_FILE_NAME_LEN];
        u8_t block[2][BLOCK_SIZE] __attribute__((aligned(8))); // casted to blk_file_t (u64_t fields)

#if __FLATFS_INDEX_SIZE__ > 0
        path_index_t path_index[__FLATFS_INDEX_SIZE__];
#endif
} flatfs_t;

/*==============================================================================
  Local function prototypes
==============================================================================*/
static u32_t calc_crc32(const void *buf, size_t buflen, u32_t init);
static int block_read(flatfs_t *hdl, u32_t address, void *dst, size_t blocks);
static int block_write(flatfs_t *hdl, u32_t address, const void *src, size_t blocks);
static int mount(flatfs_t *hdl);
static int check_first_sector_coherency(flatfs_t *hdl);
static int check_bitmaps_coherency_and_repair(flatfs_t *hdl);
static int check_file_block_coherency(flatfs_t *hdl, u32_t node_num, const blk_file_t *blk);
static u32_t node_get_blkaddr(flatfs_t *hdl, u32_t node_num);
static u64_t node_max_file_size(flatfs_t *hdl, u32_t node_num);
static int node_find(flatfs_t *hdl, const char *name, mode_t mode, blk_file_t *blk, u32_t *node_num);
static int node_find_any(flatfs_t *hdl, const char *path, blk_file_t *blk, u32_t *node_num);
static int node_create(flatfs_t *hdl, const char *name, mode_t mode, blk_file_t *blk, u32_t *node_num,
        time_t time);
static int node_remove(flatfs_t *hdl, u32_t node_num);
static bool node_is_open(flatfs_t *hdl, u32_t node_num);
static int check_path(const char *path);
static const char *make_dir_path(flatfs_t *hdl, const char *path);
static int parent_dir_check(flatfs_t *hdl, const char *path, blk_file_t *blk);
static void set_bitmap_bit(flatfs_t *hdl, u32_t node_num, bool set);
static u32_t calc_blk_crc(const void *blk);
static int file_block_read(flatfs_t *hdl, u32_t node_num, blk_file_t *blk);
static int file_block_write(flatfs_t *hdl, u32_t node_num, blk_file_t *blk);
static void index_remove(flatfs_t *hdl, u32_t node_num);
static bool index_find(flatfs_t *hdl, const char *name, mode_t mode, blk_file_t *blk, u32_t *node_num);
static bool index_get(flatfs_t *hdl, u32_t node_num, blk_file_t *blk);
static void index_add(flatfs_t *hdl, u32_t node_num, const blk_file_t *blk);

/*==============================================================================
  Local objects
==============================================================================*/
/*
 * Table for polynomial: 0x04c11db7
 */
static const u32_t CRC32_TAB[] = {0x00000000, 0x04c11db7, 0x09823b6e, 0x0d4326d9, 0x130476dc, 0x17c56b6b,
        0x1a864db2, 0x1e475005, 0x2608edb8, 0x22c9f00f, 0x2f8ad6d6, 0x2b4bcb61, 0x350c9b64, 0x31cd86d3,
        0x3c8ea00a, 0x384fbdbd, 0x4c11db70, 0x48d0c6c7, 0x4593e01e, 0x4152fda9, 0x5f15adac, 0x5bd4b01b,
        0x569796c2, 0x52568b75, 0x6a1936c8, 0x6ed82b7f, 0x639b0da6, 0x675a1011, 0x791d4014, 0x7ddc5da3,
        0x709f7b7a, 0x745e66cd, 0x9823b6e0, 0x9ce2ab57, 0x91a18d8e, 0x95609039, 0x8b27c03c, 0x8fe6dd8b,
        0x82a5fb52, 0x8664e6e5, 0xbe2b5b58, 0xbaea46ef, 0xb7a96036, 0xb3687d81, 0xad2f2d84, 0xa9ee3033,
        0xa4ad16ea, 0xa06c0b5d, 0xd4326d90, 0xd0f37027, 0xddb056fe, 0xd9714b49, 0xc7361b4c, 0xc3f706fb,
        0xceb42022, 0xca753d95, 0xf23a8028, 0xf6fb9d9f, 0xfbb8bb46, 0xff79a6f1, 0xe13ef6f4, 0xe5ffeb43,
        0xe8bccd9a, 0xec7dd02d, 0x34867077, 0x30476dc0, 0x3d044b19, 0x39c556ae, 0x278206ab, 0x23431b1c,
        0x2e003dc5, 0x2ac12072, 0x128e9dcf, 0x164f8078, 0x1b0ca6a1, 0x1fcdbb16, 0x018aeb13, 0x054bf6a4,
        0x0808d07d, 0x0cc9cdca, 0x7897ab07, 0x7c56b6b0, 0x71159069, 0x75d48dde, 0x6b93dddb, 0x6f52c06c,
        0x6211e6b5, 0x66d0fb02, 0x5e9f46bf, 0x5a5e5b08, 0x571d7dd1, 0x53dc6066, 0x4d9b3063, 0x495a2dd4,
        0x44190b0d, 0x40d816ba, 0xaca5c697, 0xa864db20, 0xa527fdf9, 0xa1e6e04e, 0xbfa1b04b, 0xbb60adfc,
        0xb6238b25, 0xb2e29692, 0x8aad2b2f, 0x8e6c3698, 0x832f1041, 0x87ee0df6, 0x99a95df3, 0x9d684044,
        0x902b669d, 0x94ea7b2a, 0xe0b41de7, 0xe4750050, 0xe9362689, 0xedf73b3e, 0xf3b06b3b, 0xf771768c,
        0xfa325055, 0xfef34de2, 0xc6bcf05f, 0xc27dede8, 0xcf3ecb31, 0xcbffd686, 0xd5b88683, 0xd1799b34,
        0xdc3abded, 0xd8fba05a, 0x690ce0ee, 0x6dcdfd59, 0x608edb80, 0x644fc637, 0x7a089632, 0x7ec98b85,
        0x738aad5c, 0x774bb0eb, 0x4f040d56, 0x4bc510e1, 0x46863638, 0x42472b8f, 0x5c007b8a, 0x58c1663d,
        0x558240e4, 0x51435d53, 0x251d3b9e, 0x21dc2629, 0x2c9f00f0, 0x285e1d47, 0x36194d42, 0x32d850f5,
        0x3f9b762c, 0x3b5a6b9b, 0x0315d626, 0x07d4cb91, 0x0a97ed48, 0x0e56f0ff, 0x1011a0fa, 0x14d0bd4d,
        0x19939b94, 0x1d528623, 0xf12f560e, 0xf5ee4bb9, 0xf8ad6d60, 0xfc6c70d7, 0xe22b20d2, 0xe6ea3d65,
        0xeba91bbc, 0xef68060b, 0xd727bbb6, 0xd3e6a601, 0xdea580d8, 0xda649d6f, 0xc423cd6a, 0xc0e2d0dd,
        0xcda1f604, 0xc960ebb3, 0xbd3e8d7e, 0xb9ff90c9, 0xb4bcb610, 0xb07daba7, 0xae3afba2, 0xaafbe615,
        0xa7b8c0cc, 0xa379dd7b, 0x9b3660c6, 0x9ff77d71, 0x92b45ba8, 0x9675461f, 0x8832161a, 0x8cf30bad,
        0x81b02d74, 0x857130c3, 0x5d8a9099, 0x594b8d2e, 0x5408abf7, 0x50c9b640, 0x4e8ee645, 0x4a4ffbf2,
        0x470cdd2b, 0x43cdc09c, 0x7b827d21, 0x7f436096, 0x7200464f, 0x76c15bf8, 0x68860bfd, 0x6c47164a,
        0x61043093, 0x65c52d24, 0x119b4be9, 0x155a565e, 0x18197087, 0x1cd86d30, 0x029f3d35, 0x065e2082,
        0x0b1d065b, 0x0fdc1bec, 0x3793a651, 0x3352bbe6, 0x3e119d3f, 0x3ad08088, 0x2497d08d, 0x2056cd3a,
        0x2d15ebe3, 0x29d4f654, 0xc5a92679, 0xc1683bce, 0xcc2b1d17, 0xc8ea00a0, 0xd6ad50a5, 0xd26c4d12,
        0xdf2f6bcb, 0xdbee767c, 0xe3a1cbc1, 0xe760d676, 0xea23f0af, 0xeee2ed18, 0xf0a5bd1d, 0xf464a0aa,
        0xf9278673, 0xfde69bc4, 0x89b8fd09, 0x8d79e0be, 0x803ac667, 0x84fbdbd0, 0x9abc8bd5, 0x9e7d9662,
        0x933eb0bb, 0x97ffad0c, 0xafb010b1, 0xab710d06, 0xa6322bdf, 0xa2f33668, 0xbcb4666d, 0xb8757bda,
        0xb5365d03, 0xb1f740b4};

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
 * @brief Initialize file system.
 *
 * @param[out]          **fs_handle             file system allocated memory
 * @param[in ]           *src_path              file source path
 * @param[in ]           *opts                  file system options (can be NULL)
 *
 * @return One of errno value (errno.h).
 */
//==============================================================================
API_FS_INIT(flatfs, void **fs_handle, const char *src_path, const char *opts)
{
        int err = sys_zalloc(sizeof(flatfs_t), fs_handle);
        if (!err) {
                flatfs_t *hdl = *fs_handle;

                hdl->read_only = sys_stropt_is_flag(opts, "ro");
                hdl->force_check = sys_stropt_is_flag(opts, "check");
                hdl->noatime = sys_stropt_is_flag(opts, "noatime");

                if (hdl->read_only) {
                        MSG("mounted read-only");
                }

                if (hdl->force_check) {
                        MSG("enabled file system force check");
                }

#if __FLATFS_INDEX_SIZE__ > 0
                MSG("indexing enabled");

                for (size_t i = 0; i < ARRAY_SIZE(hdl->path_index); i++) {
                        hdl->path_index[i].node = UINT32_MAX;
                }
#endif

                err = sys_mutex_create(KMTX_TYPE_RECURSIVE, &hdl->mtx);
                if (!err) {

                        err = sys_llist_create(sys_llist_functor_cmp_pointers, NULL, &hdl->open_files);
                        if (!err) {

                                err = sys_fopen(src_path, hdl->read_only ? O_RDONLY : O_RDWR, 0, &hdl->disc);
                                if (!err) {

                                        err = mount(hdl);
                                        if (!err) {
                                                return err;
                                        }

                                        sys_fclose(hdl->disc);
                                }

                                sys_llist_destroy(hdl->open_files);
                        }

                        sys_mutex_destroy(hdl->mtx);
                }

                sys_free(fs_handle);
        }

        return err;
}

//==============================================================================
/**
 * @brief Release file system.
 *
 * @param[in ]          *fs_handle              file system allocated memory
 *
 * @return One of errno value (errno.h).
 */
//==============================================================================
API_FS_RELEASE(flatfs, void *fs_handle)
{
        flatfs_t *hdl = fs_handle;

        kmtx_t *mtx = hdl->mtx;

        int err = sys_mutex_lock(mtx, MTX_TIMEOUT);
        if (!err) {
                if ((sys_llist_size(hdl->open_files) == 0) and (hdl->open_dirs_ctr == 0)) {

                        hdl->mtx = NULL;
                        sys_llist_destroy(hdl->open_files);

                        sys_fflush(hdl->disc);
                        sys_fclose(hdl->disc);

                        sys_mutex_unlock(mtx);
                        sys_mutex_destroy(mtx);

                        sys_free(&fs_handle);
                } else {
                        err = EBUSY;
                        sys_mutex_unlock(mtx);
                }
        }

        return err;
}

//==============================================================================
/**
 * @brief Open file.
 *
 * @param[in ]          *fs_handle              file system allocated memory
 * @param[out]          *fhdl                   file extra data
 * @param[out]          *fpos                   file position
 * @param[in]           *path                   file path
 * @param[in]            flags                  file open flags
 *
 * @return One of errno value (errno.h).
 */
//==============================================================================
API_FS_OPEN(flatfs, void *fs_handle, void **fhdl, fpos_t *fpos, const char *path, u32_t flags)
{
        flatfs_t *hdl = fs_handle;

        int err = check_path(path);
        if (err) {
                return err;
        }

        file_t *file;
        err = sys_zalloc(sizeof(file_t), cast(void **, &file));
        if (!err) {
                *fhdl = file;
                *fpos = 0;

                time_t mtime = 0;
                sys_gettime(&mtime);

                err = sys_mutex_lock(hdl->mtx, MTX_TIMEOUT);
                if (!err) {
                        blk_file_t *blk = cast(blk_file_t *, hdl->block[0]);
                        u32_t node_num;

                        /*
                         * Directory that should contain selected file must exist
                         */
                        err = parent_dir_check(hdl, path, blk);
                        if (err) {
                                goto finish;
                        }

                        /*
                         * Check if path is a directory
                         */
                        err = node_find(hdl, make_dir_path(hdl, path), S_IFDIR, blk, &node_num);
                        if (err == ESUCC) {
                                err = EISDIR;
                                goto finish;
                        } else if (err != ENOENT) {
                                goto finish;
                        }

                        /*
                         * Try to open/create file in selected directory
                         */
                        err = node_find(hdl, path, S_IFREG, blk, &node_num);

                        if (err == ESUCC) {
#ifdef O_EXCL
                                if ((flags & O_CREAT) and (flags & O_EXCL)) {
                                        err = EEXIST;
                                }
#endif
                        } else if ((err == ENOENT) and (flags & O_CREAT)) {
                                err = node_create(hdl, path, S_IFREG | 0666, blk, &node_num, mtime);
                        }

                        if (err) {
                                goto finish;
                        }

                        /*
                         * Opened block should be a regular file
                         */
                        if (S_ISREG(blk->mode)) {
                                if ((flags & O_TRUNC) and (blk->size > 0)) {

                                        blk->size = 0;
                                        blk->mtime = mtime;

                                        err = file_block_write(hdl, node_num, blk);
                                        if (err) {
                                                goto finish;
                                        }
                                }

                                if (flags & O_APPEND) {
                                        *fpos = blk->size;
                                }

                                file->node_num = node_num;

                                if (sys_llist_push_back(hdl->open_files, file) == file) {
                                        err = ESUCC;
                                } else {
                                        err = ENOMEM;
                                }
                        } else {
                                err = EISDIR;
                        }

                finish:
                        sys_mutex_unlock(hdl->mtx);
                }

                if (err) {
                        sys_free(cast(void **, &file));
                }
        }

        return err;
}

//==============================================================================
/**
 * @brief Close file.
 *
 * Note: the file should always be closed (resources should be released) even
 *       function return error.
 *
 * @param[in ]          *fs_handle              file system allocated memory
 * @param[in ]          *fhdl                   file handle
 *
 * @return One of errno value (errno.h).
 */
//==============================================================================
API_FS_CLOSE(flatfs, void *fs_handle, void *fhdl)
{
        flatfs_t *hdl = fs_handle;

        int err = sys_mutex_lock(hdl->mtx, MTX_TIMEOUT);
        if (!err) {
                file_t *file = fhdl;

                int pos = sys_llist_find_begin(hdl->open_files, file);
                if (pos >= 0) {
                        // NOTE: list created with NULL destructor - erase releases object by default
                        sys_llist_erase(hdl->open_files, pos);
                        err = ESUCC;

                } else {
                        MSG("no such file descriptor!");
                        err = ENOENT;
                }

                sys_mutex_unlock(hdl->mtx);
        }

        return err;
}

//==============================================================================
/**
 * @brief Write data to the file.
 *
 * @param[in ]          *fs_handle              file system allocated memory
 * @param[in ]          *fhdl                   file handle
 * @param[in ]          *src                    data source
 * @param[in ]           count                  number of bytes to write
 * @param[in ]          *fpos                   position in file
 * @param[out]          *wrctr                  number of written bytes
 * @param[in ]           fattr                  file attributes
 *
 * @return One of errno value (errno.h).
 */
//==============================================================================
API_FS_WRITE(flatfs, void *fs_handle, void *fhdl, const u8_t *src, size_t count, fpos_t *fpos, size_t *wrctr,
        struct vfs_fattr fattr)
{
        UNUSED_ARG1(fattr);

        flatfs_t *hdl = fs_handle;
        file_t *file = fhdl;

        *wrctr = 0;

        if (hdl->read_only) {
                return EROFS;
        }

        time_t now = 0;
        sys_gettime(&now);

        int err = sys_mutex_lock(hdl->mtx, MTX_TIMEOUT);
        if (err) {
                return err;
        }

        blk_file_t *blk = cast(blk_file_t *, hdl->block[0]);
        err = file_block_read(hdl, file->node_num, blk);
        if (err) {
                goto finish;
        }

        /*
         * Seek must be inside of file node area, otherwise data of next
         * node would be overwritten.
         */
        u64_t max_file_size = node_max_file_size(hdl, file->node_num);
        if (cast(u64_t, *fpos) >= max_file_size) {
                err = (count > 0) ? EFBIG : ESUCC;
                goto finish;
        }

        u32_t seek = *fpos;
        u32_t data_address = node_get_blkaddr(hdl, file->node_num) + 1;

        /*
         * Fill by zeros void between file size and seek.
         */
        if (seek > blk->size) {
                u32_t wrpos = blk->size;

                while (!err and (wrpos < seek)) {
                        u32_t offset = wrpos % BLOCK_SIZE;
                        u32_t bytes = min(seek - wrpos, BLOCK_SIZE - offset);
                        u32_t address = data_address + (wrpos / BLOCK_SIZE);

                        if (offset != 0) {
                                err = block_read(hdl, address, hdl->block[1], 1);
                        } else {
                                memset(hdl->block[1], 0, BLOCK_SIZE);
                        }

                        if (!err) {
                                memset(&hdl->block[1][offset], 0, bytes);
                                err = block_write(hdl, address, hdl->block[1], 1);
                        }

                        if (!err) {
                                wrpos += bytes;
                        }
                }
        }

        /*
         * Write data buffer.
         */
        u32_t bytes_to_write = min(max_file_size - seek, cast(u64_t, count));
        u32_t block_address = data_address + (seek / BLOCK_SIZE);

        while (!err and (bytes_to_write > 0)) {

                u32_t blocks = 0;
                u32_t bytes = 0;
                const u8_t *buf = NULL;

                if ((seek % BLOCK_SIZE == 0) and (bytes_to_write >= BLOCK_SIZE)) {
                        blocks = bytes_to_write / BLOCK_SIZE;
                        bytes = blocks * BLOCK_SIZE;
                        buf = src;

                } else {
                        blocks = 1;
                        bytes = BLOCK_SIZE - (seek % BLOCK_SIZE);
                        bytes = min(bytes, bytes_to_write);
                        buf = hdl->block[1];

                        if ((seek % BLOCK_SIZE == 0) and (seek >= blk->size)) {
                                // block beyond end of file: nothing to preserve
                                memset(hdl->block[1], 0, BLOCK_SIZE);
                        } else {
                                err = block_read(hdl, block_address, hdl->block[1], blocks);
                        }

                        if (!err) {
                                memcpy(&hdl->block[1][(seek % BLOCK_SIZE)], src, bytes);
                        }
                }

                if (!err) {
                        err = block_write(hdl, block_address, buf, blocks);
                }

                if (!err) {
                        block_address += blocks;
                        bytes_to_write -= bytes;
                        seek += bytes;
                        src += bytes;
                        *wrctr += bytes;
                }
        }

        /*
         * Update file descriptor. Option noatime is used here to reduce
         * medium wear: descriptor is not updated (mtime) when data was
         * overwritten in place without file size change.
         */
        if (*wrctr > 0) {
                if ((blk->size < seek) or (not hdl->noatime)) {
                        blk->mtime = now;
                        blk->size = max(blk->size, seek);

                        int wrerr = file_block_write(hdl, file->node_num, blk);
                        if (!err) {
                                err = wrerr;
                        }
                }
        }

finish:
        sys_mutex_unlock(hdl->mtx);

        return err;
}

//==============================================================================
/**
 * @brief Read data from file.
 *
 * @param[in ]          *fs_handle              file system allocated memory
 * @param[in ]          *fhdl                   file handle
 * @param[out]          *dst                    data destination
 * @param[in ]           count                  number of bytes to read
 * @param[in ]          *fpos                   position in file
 * @param[out]          *rdctr                  number of read bytes
 * @param[in ]           fattr                  file attributes
 *
 * @return One of errno value (errno.h).
 */
//==============================================================================
API_FS_READ(flatfs, void *fs_handle, void *fhdl, u8_t *dst, size_t count, fpos_t *fpos, size_t *rdctr,
        struct vfs_fattr fattr)
{
        UNUSED_ARG1(fattr);

        flatfs_t *hdl = fs_handle;
        file_t *file = fhdl;

        *rdctr = 0;

        int err = sys_mutex_lock(hdl->mtx, MTX_TIMEOUT);
        if (!err) {

                blk_file_t *blk = cast(blk_file_t *, hdl->block[0]);
                err = file_block_read(hdl, file->node_num, blk);
                if (!err and (cast(u64_t, *fpos) < blk->size)) {

                        u32_t seek = *fpos;
                        u32_t bytes_to_read = min(blk->size - seek, count);
                        u32_t block_address = (node_get_blkaddr(hdl, file->node_num) + 1) +
                                (seek / BLOCK_SIZE);

                        while (!err and (bytes_to_read > 0)) {

                                u32_t blocks = 0;
                                u32_t bytes = 0;

                                if ((seek % BLOCK_SIZE == 0) and (bytes_to_read >= BLOCK_SIZE)) {

                                        blocks = bytes_to_read / BLOCK_SIZE;
                                        bytes = blocks * BLOCK_SIZE;

                                        err = block_read(hdl, block_address, dst, blocks);

                                } else {
                                        blocks = 1;
                                        bytes = BLOCK_SIZE - (seek % BLOCK_SIZE);
                                        bytes = min(bytes, bytes_to_read);

                                        err = block_read(hdl, block_address, hdl->block[1], blocks);
                                        if (!err) {
                                                memcpy(dst, &hdl->block[1][(seek % BLOCK_SIZE)], bytes);
                                        }
                                }

                                if (!err) {
                                        block_address += blocks;
                                        bytes_to_read -= bytes;
                                        seek += bytes;
                                        dst += bytes;
                                        *rdctr += bytes;
                                }
                        }
                }

                sys_mutex_unlock(hdl->mtx);
        }

        return err;
}

//==============================================================================
/**
 * @brief IO operations on files.
 *
 * @param[in ]          *fs_handle              file system allocated memory
 * @param[in ]          *fhdl                   file handle
 * @param[in ]           request                request
 * @param[in ][out]     *arg                    request's argument
 *
 * @return One of errno value (errno.h).
 */
//==============================================================================
API_FS_IOCTL(flatfs, void *fs_handle, void *fhdl, int request, void *arg)
{
        UNUSED_ARG4(fs_handle, fhdl, request, arg);
        return EBADRQC;
}

//==============================================================================
/**
 * @brief Flush file data.
 *
 * @param[in ]          *fs_handle              file system allocated memory
 * @param[in ]          *fhdl                   file handle
 *
 * @return One of errno value (errno.h).
 */
//==============================================================================
API_FS_FLUSH(flatfs, void *fs_handle, void *fhdl)
{
        UNUSED_ARG2(fs_handle, fhdl);
        return ESUCC;
}

//==============================================================================
/**
 * @brief Return file status.
 *
 * @param[in ]          *fs_handle              file system allocated memory
 * @param[in ]          *fhdl                   file handle
 * @param[out]          *stat                   file status
 *
 * @return One of errno value (errno.h).
 */
//==============================================================================
API_FS_FSTAT(flatfs, void *fs_handle, void *fhdl, struct stat *stat)
{
        flatfs_t *hdl = fs_handle;

        int err = sys_mutex_lock(hdl->mtx, MTX_TIMEOUT);
        if (!err) {
                file_t *file = fhdl;

                blk_file_t *blk = cast(blk_file_t *, hdl->block[0]);
                err = file_block_read(hdl, file->node_num, blk);
                if (!err) {
                        stat->st_ctime = blk->ctime;
                        stat->st_mtime = blk->mtime;
                        stat->st_dev = 0;
                        stat->st_gid = blk->gid;
                        stat->st_uid = blk->uid;
                        stat->st_mode = blk->mode;
                        stat->st_size = blk->size;
                }

                sys_mutex_unlock(hdl->mtx);
        }

        return err;
}

//==============================================================================
/**
 * @brief Return file/dir status.
 *
 * @param[in ]          *fs_handle              file system allocated memory
 * @param[in ]          *path                   file path
 * @param[out]          *stat                   file status
 *
 * @return One of errno value (errno.h).
 */
//==============================================================================
API_FS_STAT(flatfs, void *fs_handle, const char *path, struct stat *stat)
{
        flatfs_t *hdl = fs_handle;

        /*
         * Root directory always exists (without any node).
         */
        if (isstreq(path, "/")) {
                memset(stat, 0, sizeof(*stat));
                stat->st_mode = S_IFDIR | 0777;
                return ESUCC;
        }

        int err = check_path(path);
        if (err) {
                return err;
        }

        err = sys_mutex_lock(hdl->mtx, MTX_TIMEOUT);
        if (err) {
                return err;
        }

        blk_file_t *blk = cast(blk_file_t *, hdl->block[0]);
        u32_t node_num;

        err = node_find_any(hdl, path, blk, &node_num);
        if (not err) {
                stat->st_ctime = blk->ctime;
                stat->st_mtime = blk->mtime;
                stat->st_dev = 0;
                stat->st_gid = blk->gid;
                stat->st_uid = blk->uid;
                stat->st_mode = blk->mode;
                stat->st_size = blk->size;
        }

        sys_mutex_unlock(hdl->mtx);

        return err;
}

//==============================================================================
/**
 * @brief Return file system status.
 *
 * @param[in ]          *fs_handle              file system allocated memory
 * @param[out]          *statfs                 file system status
 *
 * @return One of errno value (errno.h).
 */
//==============================================================================
API_FS_STATFS(flatfs, void *fs_handle, struct statfs *statfs)
{
        flatfs_t *hdl = fs_handle;

        statfs->f_bsize = BLOCK_SIZE;
        statfs->f_blocks = hdl->total_volume_blocks;
        statfs->f_bfree = hdl->total_volume_blocks - hdl->total_files - SYSTEM_BLOCKS;
        statfs->f_ffree = hdl->total_files;
        statfs->f_files = hdl->total_files;
        statfs->f_type = SYS_FS_TYPE__SOLID;
        statfs->f_fsname = "flatfs";

        int err = sys_mutex_lock(hdl->mtx, MTX_TIMEOUT);
        if (!err) {

                blk_file_t *blk = cast(blk_file_t *, hdl->block[0]);

                foreach_used_file_node(hdl, 0)
                {
                        statfs->f_ffree--;

                        int rderr = file_block_read(hdl, node, blk);
                        if (!rderr) {
                                statfs->f_bfree -= CEILING(blk->size, BLOCK_SIZE);

                        } else if (rderr == EILSEQ) {
                                DBG("incoherent node %lu in block %lu", cast(unsigned long, node),
                                        cast(unsigned long, node_get_blkaddr(hdl, node)));
                        } else {
                                err = rderr;
                                goto finish;
                        }
                }

        finish:
                sys_mutex_unlock(hdl->mtx);
        }

        return err;
}

//==============================================================================
/**
 * @brief Create directory.
 *
 * @param[in ]          *fs_handle              file system allocated memory
 * @param[in ]          *path                   name of created directory
 * @param[in ]           mode                   dir mode
 *
 * @return One of errno value (errno.h).
 */
//==============================================================================
API_FS_MKDIR(flatfs, void *fs_handle, const char *path, mode_t mode)
{
        flatfs_t *hdl = fs_handle;

        if (isstreq(path, "/")) {
                return EEXIST;
        }

        int err = check_path(path);
        if (err) {
                return err;
        }

        time_t now = 0;
        sys_gettime(&now);

        err = sys_mutex_lock(hdl->mtx, MTX_TIMEOUT);
        if (!err) {
                blk_file_t *blk = cast(blk_file_t *, hdl->block[0]);
                u32_t node_num;

                /*
                 * Directory that should contain created directory must exist
                 */
                err = parent_dir_check(hdl, path, blk);
                if (err) {
                        goto finish;
                }

                /*
                 * Regular file with the same name cannot exist
                 */
                sys_strlcpy(hdl->tmp_path, path, sizeof(hdl->tmp_path));
                size_t len = strlen(hdl->tmp_path);
                if (hdl->tmp_path[len - 1] == '/') {
                        hdl->tmp_path[--len] = '\0';
                }

                err = node_find(hdl, hdl->tmp_path, S_IFREG, blk, &node_num);
                if (err == ESUCC) {
                        err = EEXIST;
                        goto finish;
                } else if (err != ENOENT) {
                        goto finish;
                }

                /*
                 * Directory cannot exist
                 */
                hdl->tmp_path[len++] = '/';
                hdl->tmp_path[len] = '\0';

                err = node_find(hdl, hdl->tmp_path, FIND_ANY_NODE, blk, &node_num);
                if (err == ENOENT) {
                        err = node_create(hdl, hdl->tmp_path, (mode & S_IPMT) | S_IFDIR, blk, &node_num, now);

                } else if (err == ESUCC) {
                        err = EEXIST;
                }

        finish:
                sys_mutex_unlock(hdl->mtx);
        }

        return err;
}

//==============================================================================
/**
 * @brief Create pipe.
 *
 * @param[in ]          *fs_handle              file system allocated memory
 * @param[in ]          *path                   name of created pipe
 * @param[in ]           mode                   pipe mode
 *
 * @return One of errno value (errno.h).
 */
//==============================================================================
API_FS_MKFIFO(flatfs, void *fs_handle, const char *path, mode_t mode)
{
        UNUSED_ARG3(fs_handle, path, mode);
        return ENOTSUP;
}

//==============================================================================
/**
 * @brief Create node for driver file.
 *
 * @param[in ]          *fs_handle              file system allocated memory
 * @param[in ]          *path                   name of created node
 * @param[in ]           dev                    driver number
 *
 * @return One of errno value (errno.h).
 */
//==============================================================================
API_FS_MKNOD(flatfs, void *fs_handle, const char *path, const dev_t dev)
{
        UNUSED_ARG3(fs_handle, path, dev);
        return ENOTSUP;
}

//==============================================================================
/**
 * @brief Open directory.
 *
 * @param[in ]          *fs_handle              file system allocated memory
 * @param[in ]          *path                   name of opened directory
 * @param[in ]          *kdir                   directory object
 *
 * @return One of errno value (errno.h).
 */
//==============================================================================
API_FS_OPENDIR(flatfs, void *fs_handle, const char *path, kdir_t *kdir)
{
        flatfs_t *hdl = fs_handle;

        kdir->d_hdl = NULL;
        kdir->d_items = 0;
        kdir->d_seek = 0;

        int err = check_path(path);
        if (err) {
                return err;
        }

        size_t path_len = strlen(path);

        dir_t *dir;
        err = sys_zalloc(sizeof(*dir), cast(void **, &dir));
        if (err) {
                return err;
        }

        /*
         * Directory root always ends with '/' (place for '/' and '\0').
         */
        err = sys_zalloc(path_len + 2, cast(void **, &dir->dir_root));
        if (err) {
                goto error;
        }

        memcpy(dir->dir_root, path, path_len);

        if (dir->dir_root[path_len - 1] != '/') {
                dir->dir_root[path_len] = '/';
        }

        dir->dir_root_len = strlen(dir->dir_root);

        err = sys_strdup("", &dir->item_name);
        if (err) {
                goto error;
        }

        err = sys_llist_create(sys_llist_functor_cmp_strings, NULL, &dir->last_dirs);
        if (err) {
                goto error;
        }

        err = sys_mutex_lock(hdl->mtx, MTX_TIMEOUT);
        if (err) {
                goto error;
        }

        if (isstreq(dir->dir_root, "/")) {
                err = ESUCC;

        } else {
                err = ENOENT;

                blk_file_t *blk = cast(blk_file_t *, hdl->block[0]);

                foreach_used_file_node(hdl, 0)
                {
                        int rderr = file_block_read(hdl, node, blk);
                        if (rderr == EILSEQ) {
                                continue;
                        } else if (rderr) {
                                err = rderr;
                                goto finish;
                        }

                        if (strncmp(blk->name, dir->dir_root, dir->dir_root_len) == 0) {
                                err = ESUCC;
                                goto finish;
                        }
                }
        }

finish:
        if (not err) {
                kdir->d_hdl = dir;
                kdir->d_items = 0; // FIXME count number of files
                kdir->d_seek = 0;

                hdl->open_dirs_ctr++;
        }

        sys_mutex_unlock(hdl->mtx);

error:
        if (err) {
                if (dir->dir_root) {
                        sys_free(cast(void **, &dir->dir_root));
                }

                if (dir->item_name) {
                        sys_free(cast(void **, &dir->item_name));
                }

                if (dir->last_dirs) {
                        sys_llist_destroy(dir->last_dirs);
                }

                sys_free(cast(void **, &dir));
        }

        return err;
}

//==============================================================================
/**
 * @brief Close directory.
 *
 * @param[in ]          *fs_handle              file system allocated memory
 * @param[in ]          *kdir                   directory object
 *
 * @return One of errno value (errno.h).
 */
//==============================================================================
API_FS_CLOSEDIR(flatfs, void *fs_handle, kdir_t *kdir)
{
        flatfs_t *hdl = fs_handle;

        dir_t *dir = kdir->d_hdl;

        sys_free(cast(void **, &dir->dir_root));
        sys_free(cast(void **, &dir->item_name));
        sys_llist_destroy(dir->last_dirs);
        sys_free(cast(void **, &dir));

        kdir->d_hdl = NULL;

        int err = sys_mutex_lock(hdl->mtx, MTX_TIMEOUT);
        if (not err) {
                if (hdl->open_dirs_ctr > 0) {
                        hdl->open_dirs_ctr--;
                }
                sys_mutex_unlock(hdl->mtx);
        }

        return err;
}

//==============================================================================
/**
 * @brief Read directory.
 *
 * @param[in ]          *fs_handle              file system allocated memory
 * @param[in,out]       *dir                    directory object
 *
 * @return One of errno value (errno.h).
 */
//==============================================================================
API_FS_READDIR(flatfs, void *fs_handle, kdir_t *kdir)
{
        flatfs_t *hdl = fs_handle;
        dir_t *dir = kdir->d_hdl;

        if (dir->seek != kdir->d_seek) {
                dir->seek = kdir->d_seek;
        }

        int err = sys_mutex_lock(hdl->mtx, MTX_TIMEOUT);
        if (err) {
                return err;
        }

        err = ENOENT;

        blk_file_t *blk = cast(blk_file_t *, hdl->block[0]);

        foreach_used_file_node(hdl, dir->seek)
        {
                dir->seek = node + 1;
                kdir->d_seek = dir->seek;

                int rderr = file_block_read(hdl, node, blk);
                if (rderr == EILSEQ) {
                        continue;
                } else if (rderr) {
                        err = rderr;
                        goto finish;
                }

                index_add(hdl, node, blk);

                DBG("readdir() node: %lu: '%s'", cast(unsigned long, node), blk->name);

                /*
                 * Node must be located in selected directory
                 */
                if (strncmp(blk->name, dir->dir_root, dir->dir_root_len) != 0) {
                        continue;
                }

                char *item_name = blk->name + dir->dir_root_len;
                if (isstrempty(item_name)) {
                        continue; // directory node itself
                }

                mode_t mode;
                u64_t size;

                char *slash = strchr(item_name, '/');
                if (slash) {
                        bool own_node = (slash[1] == '\0'); // node of subdirectory itself
                        *slash = '\0';
                        mode = S_IFDIR | (own_node ? (blk->mode & S_IPMT) : 0777);
                        size = 0;
                } else {
                        mode = blk->mode;
                        size = blk->size;
                }

                if (sys_llist_find_begin(dir->last_dirs, item_name) != -1) {
                        continue; // directory already reported
                }

                if (S_ISDIR(mode)) {
                        if (not sys_llist_push_emplace_front(dir->last_dirs, strsize(item_name), item_name)) {
                                err = ENOMEM;
                                goto finish;
                        }
                }

                err = sys_free(cast(void **, &dir->item_name));
                if (err) {
                        goto finish;
                }

                err = sys_strdup(item_name, &dir->item_name);
                if (err) {
                        goto finish;
                }

                kdir->dirent.dev = 0;
                kdir->dirent.mode = mode;
                kdir->dirent.size = size;
                kdir->dirent.d_name = dir->item_name;

                err = ESUCC;
                goto finish;
        }

finish:
        sys_mutex_unlock(hdl->mtx);

        return err;
}

//==============================================================================
/**
 * @brief Remove file/directory.
 *
 * Note: only empty directory can be removed (ENOTEMPTY otherwise).
 *
 * @param[in ]          *fs_handle              file system allocated memory
 * @param[in ]          *path                   name of removed file/directory
 *
 * @return One of errno value (errno.h).
 */
//==============================================================================
API_FS_REMOVE(flatfs, void *fs_handle, const char *path)
{
        flatfs_t *hdl = fs_handle;

        if (hdl->read_only) {
                return EROFS;
        }

        if (isstreq(path, "/")) {
                return EBUSY;
        }

        int err = check_path(path);
        if (err) {
                return err;
        }

        err = sys_mutex_lock(hdl->mtx, MTX_TIMEOUT);
        if (!err) {

                blk_file_t *blk = cast(blk_file_t *, hdl->block[0]);
                blk_file_t *child = cast(blk_file_t *, hdl->block[1]);
                u32_t df_node_num = 0;

                err = node_find_any(hdl, path, blk, &df_node_num);
                if (err) {
                        goto finish;
                }

                if (node_is_open(hdl, df_node_num)) {
                        err = EBUSY;
                        goto finish;
                }

                /*
                 * Directory can be removed only if it is empty (POSIX: ENOTEMPTY).
                 */
                if (S_ISDIR(blk->mode)) {

                        sys_strlcpy(hdl->tmp_path, blk->name, sizeof(hdl->tmp_path));
                        size_t dir_name_len = strlen(hdl->tmp_path);

                        foreach_used_file_node(hdl, 0)
                        {
                                if (node == df_node_num) {
                                        continue;
                                }

                                int rderr = file_block_read(hdl, node, child);
                                if (rderr == EILSEQ) {
                                        continue;
                                } else if (rderr) {
                                        err = rderr;
                                        goto finish;
                                }

                                if (strncmp(child->name, hdl->tmp_path, dir_name_len) == 0) {
                                        err = ENOTEMPTY;
                                        goto finish;
                                }
                        }
                }

                err = node_remove(hdl, df_node_num);

        finish:
                sys_mutex_unlock(hdl->mtx);
        }

        return err;
}

//==============================================================================
/**
 * @brief Rename file/directory.
 *
 * @param[in ]          *fs_handle              file system allocated memory
 * @param[in ]          *old_name               old object name
 * @param[in ]          *new_name               new object name
 *
 * @return One of errno value (errno.h).
 */
//==============================================================================
API_FS_RENAME(flatfs, void *fs_handle, const char *old_name, const char *new_name)
{
        flatfs_t *hdl = fs_handle;

        if (hdl->read_only) {
                return EROFS;
        }

        if (isstreq(old_name, "/") or isstreq(new_name, "/")) {
                return EBUSY;
        }

        int err = check_path(old_name);
        if (!err) {
                err = check_path(new_name);
        }

        if (err) {
                return err;
        }

        /* length of new name without trailing slash */
        size_t new_len = strlen(new_name);
        bool new_is_dir_path = (new_name[new_len - 1] == '/');
        if (new_is_dir_path) {
                new_len--;
        }

        err = sys_mutex_lock(hdl->mtx, MTX_TIMEOUT);
        if (err) {
                return err;
        }

        blk_file_t *blk = cast(blk_file_t *, hdl->block[0]);
        blk_file_t *child = cast(blk_file_t *, hdl->block[1]);
        u32_t obj_node;

        /*
         * Destination cannot exist (neither as file nor as directory).
         */
        err = node_find_any(hdl, new_name, blk, &obj_node);
        if (err == ESUCC) {
                err = EEXIST;
                goto finish;
        } else if (err != ENOENT) {
                goto finish;
        }

        /*
         * Destination directory must exist.
         */
        err = parent_dir_check(hdl, new_name, blk);
        if (err) {
                goto finish;
        }

        /*
         * Find source object.
         */
        err = node_find_any(hdl, old_name, blk, &obj_node);
        if (err) {
                goto finish;
        }

        if (S_ISDIR(blk->mode)) {
                sys_strlcpy(hdl->tmp_path, blk->name, sizeof(hdl->tmp_path));
                size_t old_len = strlen(hdl->tmp_path); // with trailing slash

                /*
                 * Directory cannot be moved into itself.
                 */
                if ((new_len >= old_len) and (strncmp(new_name, hdl->tmp_path, old_len) == 0)) {
                        err = EINVAL;
                        goto finish;
                }

                /*
                 * Pass 1: check if all new names fit in name field.
                 */
                {
                        foreach_used_file_node(hdl, 0)
                        {
                                if (node == obj_node) {
                                        continue;
                                }

                                int rderr = file_block_read(hdl, node, child);
                                if (rderr == EILSEQ) {
                                        continue;
                                } else if (rderr) {
                                        err = rderr;
                                        goto finish;
                                }

                                if (strncmp(child->name, hdl->tmp_path, old_len) == 0) {
                                        size_t tail_len = strlen(&child->name[old_len]);
                                        if ((new_len + 1 + tail_len) >= sizeof(child->name)) {
                                                err = ENAMETOOLONG;
                                                goto finish;
                                        }
                                }
                        }
                }

                /*
                 * Pass 2: rename directory content.
                 */
                {
                        foreach_used_file_node(hdl, 0)
                        {
                                if (node == obj_node) {
                                        continue;
                                }

                                int rderr = file_block_read(hdl, node, child);
                                if (rderr == EILSEQ) {
                                        continue;
                                } else if (rderr) {
                                        err = rderr;
                                        goto finish;
                                }

                                if (strncmp(child->name, hdl->tmp_path, old_len) == 0) {
                                        size_t tail_len = strlen(&child->name[old_len]);

                                        memmove(&child->name[new_len + 1], &child->name[old_len], tail_len + 1);
                                        memcpy(child->name, new_name, new_len);
                                        child->name[new_len] = '/';

                                        size_t name_len = new_len + 1 + tail_len;
                                        memset(&child->name[name_len], 0, sizeof(child->name) - name_len);

                                        err = file_block_write(hdl, node, child);
                                        if (err) {
                                                goto finish;
                                        }
                                }
                        }
                }

                memset(blk->name, 0, sizeof(blk->name));
                memcpy(blk->name, new_name, new_len);
                blk->name[new_len] = '/';

        } else {
                if (new_is_dir_path) {
                        err = ENOTDIR;
                        goto finish;
                }

                memset(blk->name, 0, sizeof(blk->name));
                memcpy(blk->name, new_name, new_len);
        }

        err = file_block_write(hdl, obj_node, blk);

finish:
        sys_mutex_unlock(hdl->mtx);

        return err;
}

//==============================================================================
/**
 * @brief Change file's mode.
 *
 * @param[in ]          *fs_handle              file system allocated memory
 * @param[in ]          *path                   file path
 * @param[in ]           mode                   new file mode
 *
 * @return One of errno value (errno.h).
 */
//==============================================================================
API_FS_CHMOD(flatfs, void *fs_handle, const char *path, mode_t mode)
{
        flatfs_t *hdl = fs_handle;

        if (hdl->read_only) {
                return EROFS;
        }

        int err = check_path(path);
        if (err) {
                return err;
        }

        err = sys_mutex_lock(hdl->mtx, MTX_TIMEOUT);
        if (!err) {
                blk_file_t *file = cast(blk_file_t *, hdl->block[0]);
                u32_t node;

                err = node_find_any(hdl, path, file, &node);
                if (!err) {
                        file->mode = (file->mode & S_IFMT) | (mode & S_IPMT);
                        err = file_block_write(hdl, node, file);
                }

                sys_mutex_unlock(hdl->mtx);
        }

        return err;
}

//==============================================================================
/**
 * @brief Change file's owner and group.
 *
 * @param[in ]          *fs_handle              file system allocated memory
 * @param[in ]          *path                   file path
 * @param[in ]           owner                  new file owner
 * @param[in ]           group                  new file group
 *
 * @return One of errno value (errno.h).
 */
//==============================================================================
API_FS_CHOWN(flatfs, void *fs_handle, const char *path, uid_t owner, gid_t group)
{
        flatfs_t *hdl = fs_handle;

        if (hdl->read_only) {
                return EROFS;
        }

        int err = check_path(path);
        if (err) {
                return err;
        }

        err = sys_mutex_lock(hdl->mtx, MTX_TIMEOUT);
        if (!err) {
                blk_file_t *file = cast(blk_file_t *, hdl->block[0]);
                u32_t node;

                err = node_find_any(hdl, path, file, &node);
                if (!err) {
                        file->uid = owner;
                        file->gid = group;
                        err = file_block_write(hdl, node, file);
                }

                sys_mutex_unlock(hdl->mtx);
        }

        return err;
}

//==============================================================================
/**
 * @brief Synchronize all buffers to a medium.
 *
 * @param[in ]          *fs_handle              file system allocated memory
 *
 * @return One of errno value (errno.h).
 */
//==============================================================================
API_FS_SYNC(flatfs, void *fs_handle)
{
#if __FLATFS_INDEX_SIZE__ > 0
        /*
         * Decrease index cache usage points.
         */
        flatfs_t *hdl = fs_handle;

        int err = sys_mutex_lock(hdl->mtx, MTX_TIMEOUT);
        if (!err) {
                for (size_t i = 0; i < ARRAY_SIZE(hdl->path_index); i++) {

                        path_index_t *index = &hdl->path_index[i];

                        if (index->node != UINT32_MAX) {
                                if (index->hit > (INDEX_HIT_MIN + __OS_SYSTEM_CACHE_SYNC_PERIOD__)) {
                                        index->hit -= __OS_SYSTEM_CACHE_SYNC_PERIOD__;
                                }
                        }
                }

                sys_mutex_unlock(hdl->mtx);
        }
#else
        UNUSED_ARG1(fs_handle);
#endif

        return ESUCC;
}

//==============================================================================
/**
 * @brief  Calculate CRC32.
 *
 * @param  buf          buffer
 * @param  buflen       buffer length
 * @param  init         initial value ()
 *
 * @return CRC32 result.
 */
//==============================================================================
static u32_t calc_crc32(const void *buf, size_t buflen, u32_t init)
{
        const u8_t *b = buf;

        u32_t crc = init;
        while (buflen--) {
                crc = (crc << 8) ^ CRC32_TAB[((crc >> 24) ^ *b) & 255];
                b++;
        }

        return crc;
}

//==============================================================================
/**
 * @brief  Function read selected amount of blocks from disc.
 *
 * @param  hdl          file system handle
 * @param  address      block start address
 * @param  dst          destination buffer
 * @param  blocks       number of blocks to read
 *
 * @return One of errno value (errno.h).
 */
//==============================================================================
static int block_read(flatfs_t *hdl, u32_t address, void *dst, size_t blocks)
{
        int err = sys_fseek(hdl->disc, cast(i64_t, address) * BLOCK_SIZE, VFS_SEEK_SET);
        if (!err) {
                size_t rdctr = 0;
                err = sys_fread(dst, BLOCK_SIZE * blocks, &rdctr, hdl->disc);
                if (!err and (rdctr != BLOCK_SIZE * blocks)) {
                        err = EIO;
                }

                if (err) {
                        DBG("block_read: disc read error %d @ %lu", err, cast(unsigned long, address));
                }
        } else {
                DBG("block_read: fseek error %d", err);
        }

        return err;
}

//==============================================================================
/**
 * @brief  Function write selected amount of blocks to disc.
 *
 * @param  hdl          file system handle
 * @param  address      block start address
 * @param  src          source buffer
 * @param  blocks       number of blocks to write
 *
 * @return One of errno value (errno.h).
 */
//==============================================================================
static int block_write(flatfs_t *hdl, u32_t address, const void *src, size_t blocks)
{
        if (hdl->read_only) {
                return EROFS;
        }

        int err = sys_fseek(hdl->disc, cast(i64_t, address) * BLOCK_SIZE, VFS_SEEK_SET);
        if (!err) {
                size_t wrctr = 0;
                err = sys_fwrite(src, BLOCK_SIZE * blocks, &wrctr, hdl->disc);
                if (!err and (wrctr != BLOCK_SIZE * blocks)) {
                        err = EIO;
                }

                if (err) {
                        DBG("block_write: disc write error %d @ %lu", err, cast(unsigned long, address));
                }
        } else {
                DBG("block_write: fseek error %d", err);
        }

        return err;
}

//==============================================================================
/**
 * @brief  Function mount file system.
 *
 * @param  hdl          file system handle
 *
 * @return One of errno value (errno.h).
 */
//==============================================================================
static int mount(flatfs_t *hdl)
{
        int err = check_first_sector_coherency(hdl);
        if (!err) {
                err = check_bitmaps_coherency_and_repair(hdl);
                if (err) {
                        DBG("fs bitmap not coherent");
                }
        } else {
                DBG("fs descriptor not coherent");
        }

        return err;
}

//==============================================================================
/**
 * @brief  Check coherency of first FS sector.
 *
 * @param  hdl          file system handle
 *
 * @return One of errno value (errno.h).
 */
//==============================================================================
static int check_first_sector_coherency(flatfs_t *hdl)
{
        int err = block_read(hdl, 0, hdl->block[0], 1);
        if (err) {
                return err;
        }

        blk_fs_t *blk = cast(blk_fs_t *, hdl->block[0]);

        if (blk->magic != MAGIC_BLOCK_FS) {
                DBG("unknown file system signature");
                return EMEDIUMTYPE;
        }

        if (blk->crc != calc_blk_crc(blk)) {
                DBG("file system descriptor CRC error");
                return EILSEQ;
        }

        /*
         * Basic parameters must be checked before any calculation
         * (division by zero, underflow).
         */
        if ((blk->total_volume_blocks < SYSTEM_BLOCKS) or (blk->file_node_blocks <= 1) or
                (blk->file_node_blocks >= (blk->total_volume_blocks - SYSTEM_BLOCKS))) {
                DBG("file system parameters not coherent");
                return EILSEQ;
        }

        u32_t max_files = CEILING(blk->total_volume_blocks - SYSTEM_BLOCKS, blk->file_node_blocks);

        u64_t node_area_end = cast(u64_t, blk->first_file_node_address) +
                (cast(u64_t, blk->total_files - 1) * blk->file_node_blocks) + blk->last_file_node_blocks;

        bool bmp_pri_in_node_area = (blk->bitmap_address_primary >= blk->first_file_node_address) and
                (blk->bitmap_address_primary < node_area_end);

        bool bmp_sec_in_node_area = (blk->bitmap_address_secondary >= blk->first_file_node_address) and
                (blk->bitmap_address_secondary < node_area_end);

        if ((blk->total_files == max_files) and (blk->total_files > 0) and (blk->total_files <= BITMAP_BITS) and
                (blk->last_file_node_blocks >= 1) and (blk->last_file_node_blocks <= blk->file_node_blocks) and
                (blk->first_file_node_address > 0) and (node_area_end <= blk->total_volume_blocks) and
                (blk->bitmap_address_primary > 0) and (blk->bitmap_address_secondary > 0) and
                (blk->bitmap_address_primary < blk->total_volume_blocks) and
                (blk->bitmap_address_secondary < blk->total_volume_blocks) and
                (blk->bitmap_address_primary != blk->bitmap_address_secondary) and
                (not bmp_pri_in_node_area) and (not bmp_sec_in_node_area)) {

                hdl->total_files = blk->total_files;
                hdl->total_volume_blocks = blk->total_volume_blocks;
                hdl->file_node_blocks = blk->file_node_blocks;
                hdl->last_file_node_blocks = blk->last_file_node_blocks;
                hdl->first_file_node_address = blk->first_file_node_address;
                hdl->bitmap_address_primary = blk->bitmap_address_primary;
                hdl->bitmap_address_secondary = blk->bitmap_address_secondary;

                return ESUCC;

        } else {
                DBG("file system parameters not coherent");
                return EILSEQ;
        }
}

//==============================================================================
/**
 * @brief  Function try to repair file system. Function load bitmap to handle.
 *
 * @param  hdl          file system handle
 *
 * @return One of errno value (errno.h).
 */
//==============================================================================
static int check_bitmaps_coherency_and_repair(flatfs_t *hdl)
{
        blk_bitmap_t *bmp_pri = &hdl->bitmap;
        blk_bitmap_t *bmp_sec = cast(blk_bitmap_t *, hdl->block[0]);
        blk_file_t *file = cast(blk_file_t *, hdl->block[1]);

        int err = block_read(hdl, hdl->bitmap_address_primary, bmp_pri, 1);
        if (!err) {
                err = block_read(hdl, hdl->bitmap_address_secondary, bmp_sec, 1);
        }

        if (err) {
                return err;
        }

        bool bmp_pri_dirty = false;
        bool bmp_sec_dirty = false;

        for (u32_t node = 0; node < BITMAP_BITS; node++) {

                size_t i = node / BITMAP_WORD_BITS;
                u32_t mask = BIT(node % BITMAP_WORD_BITS);

                bool pri = bmp_pri->word[i] & mask;
                bool sec = bmp_sec->word[i] & mask;

                /*
                 * Nodes outside of volume cannot be used.
                 */
                if (node >= hdl->total_files) {
                        if (pri) {
                                bmp_pri->word[i] &= ~mask;
                                bmp_pri_dirty = true;
                        }

                        if (sec) {
                                bmp_sec->word[i] &= ~mask;
                                bmp_sec_dirty = true;
                        }

                        continue;
                }

                /*
                 * Node block is checked only if bitmaps are different or
                 * force check is enabled for used node.
                 */
                if ((pri == sec) and not(hdl->force_check and pri)) {
                        continue;
                }

                u32_t address = node_get_blkaddr(hdl, node);

                err = block_read(hdl, address, file, 1);
                if (err) {
                        goto finish;
                }

                bool coherent = (check_file_block_coherency(hdl, node, file) == ESUCC);

                if (coherent) {
                        if (not pri) {
                                bmp_pri->word[i] |= mask;
                                bmp_pri_dirty = true;
                        }

                        if (not sec) {
                                bmp_sec->word[i] |= mask;
                                bmp_sec_dirty = true;
                        }

                        if (pri != sec) {
                                MSG("fixed incoherent bitmap of node %lu in block %lu", cast(unsigned long, node),
                                        cast(unsigned long, address));
                        }

                        /*
                         * Directory name must end with slash.
                         */
                        if (S_ISDIR(file->mode) and (LAST_CHARACTER(file->name) != '/')) {
                                size_t len = strlen(file->name);

                                if ((len + 1) < sizeof(file->name)) {
                                        file->name[len] = '/';
                                        file->name[len + 1] = '\0';

                                        if (not hdl->read_only) {
                                                file->crc = calc_blk_crc(file);

                                                err = block_write(hdl, address, file, 1);
                                                if (err) {
                                                        goto finish;
                                                }

                                                MSG("fixed incorrect directory name in node %lu in block %lu",
                                                        cast(unsigned long, node), cast(unsigned long, address));
                                        }
                                }
                        }

                } else {
                        if (pri) {
                                bmp_pri->word[i] &= ~mask;
                                bmp_pri_dirty = true;
                        }

                        if (sec) {
                                bmp_sec->word[i] &= ~mask;
                                bmp_sec_dirty = true;
                        }

                        MSG("removed incoherent node %lu in block %lu", cast(unsigned long, node),
                                cast(unsigned long, address));
                }
        }

        /*
         * In read-only mode bitmap is repaired only in memory.
         */
        if (not hdl->read_only) {
                if (!err and bmp_pri_dirty) {
                        err = block_write(hdl, hdl->bitmap_address_primary, bmp_pri, 1);
                }

                if (!err and bmp_sec_dirty) {
                        err = block_write(hdl, hdl->bitmap_address_secondary, bmp_sec, 1);
                }
        }

finish:
        return err;
}

//==============================================================================
/**
 * @brief  Function check if file block is consistent.
 *
 * @param  hdl          file system handle
 * @param  node_num     node number
 * @param  blk          file block
 *
 * @return One of errno value (errno.h).
 */
//==============================================================================
static int check_file_block_coherency(flatfs_t *hdl, u32_t node_num, const blk_file_t *blk)
{
        const char *errmsg = NULL;

        if (blk->magic != MAGIC_BLOCK_FILE) {
                errmsg = "incorrect magic number of file block";

        } else if (blk->crc != calc_blk_crc(blk)) {
                errmsg = "incorrect file block CRC";

        } else if ((blk->name[0] == '\0') or (blk->name[sizeof(blk->name) - 1] != '\0')) {
                errmsg = "incorrect file name";

        } else if (blk->size > node_max_file_size(hdl, node_num)) {
                errmsg = "file size not coherent with number of used blocks";
        }

        if (errmsg) {
                MSG("file block error: '%.*s'", cast(int, strnlen(blk->name, sizeof(blk->name))), blk->name);
                MSG("%s", errmsg);
                return EILSEQ;

        } else {
                return ESUCC;
        }
}

//==============================================================================
/**
 * @brief  Calculate block address according selected node number.
 *
 * @param  hdl          file system handle
 * @param  node_num     node number
 *
 * @return Block address.
 */
//==============================================================================
static u32_t node_get_blkaddr(flatfs_t *hdl, u32_t node_num)
{
        return (node_num * hdl->file_node_blocks) + hdl->first_file_node_address;
}

//==============================================================================
/**
 * @brief  Return maximum file size of selected node (last node can be smaller).
 *
 * @param  hdl          file system handle
 * @param  node_num     node number
 *
 * @return Maximum file size in bytes.
 */
//==============================================================================
static u64_t node_max_file_size(flatfs_t *hdl, u32_t node_num)
{
        u32_t blocks = (node_num == (hdl->total_files - 1)) ? hdl->last_file_node_blocks : hdl->file_node_blocks;

        u64_t size = (blocks > 0) ? cast(u64_t, blocks - 1) * BLOCK_SIZE : 0;

        return min(size, cast(u64_t, UINT32_MAX)); // blk_file_t::size is 32-bit
}

//==============================================================================
/**
 * @brief  Find node by name.
 *
 * @param  hdl          file system handle
 * @param  name         node name
 * @param  mode         node type (S_IFMT part) or FIND_ANY_NODE
 * @param  blk          block buffer
 * @param  node_num     node number
 *
 * @return One of errno value (errno.h).
 */
//==============================================================================
static int node_find(flatfs_t *hdl, const char *name, mode_t mode, blk_file_t *blk, u32_t *node_num)
{
        /*
         * Try find node in index
         */
        bool found = index_find(hdl, name, mode, blk, node_num);
        if (found) {
                return ESUCC;
        }

        /*
         * Try to find path in storage
         */
        foreach_used_file_node(hdl, 0)
        {
                int err = block_read(hdl, node_get_blkaddr(hdl, node), blk, 1);
                if (err) {
                        return err;
                }

                if (check_file_block_coherency(hdl, node, blk) == ESUCC) {
                        if (isstreq(name, blk->name) and
                                (((blk->mode & S_IFMT) == mode) or (mode == FIND_ANY_NODE))) {

                                DBG("found file '%s' in node %lu in block %lu", name, cast(unsigned long, node),
                                        cast(unsigned long, node_get_blkaddr(hdl, node)));

                                index_add(hdl, node, blk);

                                *node_num = node;

                                return ESUCC;
                        }

                } else {
                        DBG("incoherent node %lu in block %lu", cast(unsigned long, node),
                                cast(unsigned long, node_get_blkaddr(hdl, node)));
                }
        }

        return ENOENT;
}

//==============================================================================
/**
 * @brief  Find file or directory node (path with or without trailing slash).
 *         Single storage scan is performed.
 *
 * @note   Function uses hdl->tmp_path.
 *
 * @param  hdl          file system handle
 * @param  path         object path
 * @param  blk          block buffer
 * @param  node_num     node number
 *
 * @return One of errno value (errno.h).
 */
//==============================================================================
static int node_find_any(flatfs_t *hdl, const char *path, blk_file_t *blk, u32_t *node_num)
{
        const char *dir_path = make_dir_path(hdl, path);

        if (index_find(hdl, path, FIND_ANY_NODE, blk, node_num) or
                index_find(hdl, dir_path, S_IFDIR, blk, node_num)) {
                return ESUCC;
        }

        foreach_used_file_node(hdl, 0)
        {
                int err = block_read(hdl, node_get_blkaddr(hdl, node), blk, 1);
                if (err) {
                        return err;
                }

                if (check_file_block_coherency(hdl, node, blk) == ESUCC) {
                        if (isstreq(blk->name, path) or (S_ISDIR(blk->mode) and isstreq(blk->name, dir_path))) {
                                index_add(hdl, node, blk);
                                *node_num = node;
                                return ESUCC;
                        }
                }
        }

        return ENOENT;
}

//==============================================================================
/**
 * @brief  Create new node.
 *
 * @param  hdl          file system handle
 * @param  name         node name
 * @param  mode         file mode
 * @param  blk          block buffer
 * @param  node_num     node number
 * @param  time         create time
 *
 * @return One of errno value (errno.h).
 */
//==============================================================================
static int node_create(flatfs_t *hdl, const char *name, mode_t mode, blk_file_t *blk, u32_t *node_num,
        time_t time)
{
        if (hdl->read_only) {
                return EROFS;
        }

        if ((strlen(name) + 1) > sizeof(blk->name)) {
                return ENAMETOOLONG;
        }

        foreach_file_node(hdl, 0)
        {
                u32_t mask = BIT(bit);

                if ((hdl->bitmap.word[i] & mask) == 0) {

                        *node_num = node;

                        memset(blk, 0, sizeof(blk_file_t));
                        blk->magic = MAGIC_BLOCK_FILE;
                        blk->ctime = time;
                        blk->mtime = time;
                        blk->mode = mode;
                        blk->uid = 0;
                        blk->gid = 0;
                        blk->size = 0;
                        strcpy(blk->name, name);

                        /*
                         * Write order: primary bitmap, file block, secondary
                         * bitmap. Bitmap repair at mount relies on it.
                         */
                        hdl->bitmap.word[i] |= mask;
                        int err = block_write(hdl, hdl->bitmap_address_primary, &hdl->bitmap, 1);
                        if (!err) {
                                err = file_block_write(hdl, node, blk);
                                if (!err) {
                                        err = block_write(hdl, hdl->bitmap_address_secondary, &hdl->bitmap,
                                                1);
                                }
                        }

                        if (!err) {
                                DBG("created new file '%s' in node %lu in block %lu", name,
                                        cast(unsigned long, node), cast(unsigned long, node_get_blkaddr(hdl, node)));
                        } else {
                                DBG("node '%s' create error %d", name, err);
                        }

                        return err;
                }
        }

        return ENOSPC;
}

//==============================================================================
/**
 * @brief  Remove selected node number.
 *
 * @note   Function uses hdl->block[0] buffer.
 *
 * @param  hdl          file system handle
 * @param  node_num     node number
 *
 * @return One of errno value (errno.h).
 */
//==============================================================================
static int node_remove(flatfs_t *hdl, u32_t node_num)
{
        index_remove(hdl, node_num);

        memset(hdl->block[0], 0xFF, sizeof(hdl->block[0]));

        set_bitmap_bit(hdl, node_num, false);

        int err = block_write(hdl, hdl->bitmap_address_primary, &hdl->bitmap, 1);
        if (!err) {
                err = block_write(hdl, node_get_blkaddr(hdl, node_num), hdl->block[0], 1);
                if (!err) {
                        err = block_write(hdl, hdl->bitmap_address_secondary, &hdl->bitmap, 1);
                }
        }

        return err;
}

//==============================================================================
/**
 * @brief  Check if selected node is opened.
 *
 * @param  hdl          file system handle
 * @param  node_num     node number
 *
 * @return True if node is open, otherwise false.
 */
//==============================================================================
static bool node_is_open(flatfs_t *hdl, u32_t node_num)
{
        bool open = false;

        sys_llist_foreach (file_t *, file, hdl->open_files) {
                if (file->node_num == node_num) {
                        open = true;
                        break;
                }
        }

        return open;
}

//==============================================================================
/**
 * @brief  Check if path is correct (not empty, fits in name field with
 *         additional trailing slash).
 *
 * @param  path         path
 *
 * @return One of errno value (errno.h).
 */
//==============================================================================
static int check_path(const char *path)
{
        if ((path == NULL) or isstrempty(path)) {
                return ENOENT;
        }

        /* place for additional '/' and '\0' */
        if (strnlen(path, MAX_FILE_NAME_LEN) > (MAX_FILE_NAME_LEN - 2)) {
                return ENAMETOOLONG;
        }

        return ESUCC;
}

//==============================================================================
/**
 * @brief  Create directory path (with trailing slash) in hdl->tmp_path.
 *
 * @param  hdl          file system handle
 * @param  path         path (checked by check_path())
 *
 * @return Pointer to hdl->tmp_path.
 */
//==============================================================================
static const char *make_dir_path(flatfs_t *hdl, const char *path)
{
        sys_strlcpy(hdl->tmp_path, path, sizeof(hdl->tmp_path));

        size_t len = strlen(hdl->tmp_path);
        if ((len == 0) or (hdl->tmp_path[len - 1] != '/')) {
                sys_strlcat(hdl->tmp_path, "/", sizeof(hdl->tmp_path));
        }

        return hdl->tmp_path;
}

//==============================================================================
/**
 * @brief  Check if directory that contains selected path exists.
 *
 * @note   Function uses hdl->tmp_path and overwrites blk.
 *
 * @param  hdl          file system handle
 * @param  path         path (checked by check_path())
 * @param  blk          block buffer
 *
 * @return One of errno value (errno.h).
 */
//==============================================================================
static int parent_dir_check(flatfs_t *hdl, const char *path, blk_file_t *blk)
{
        sys_strlcpy(hdl->tmp_path, path, sizeof(hdl->tmp_path));

        size_t len = strlen(hdl->tmp_path);
        if ((len > 1) and (hdl->tmp_path[len - 1] == '/')) {
                hdl->tmp_path[len - 1] = '\0';
        }

        char *slash = strrchr(hdl->tmp_path, '/');
        if (slash == NULL) {
                return ENOENT;
        }

        slash[1] = '\0';

        if (isstreq(hdl->tmp_path, "/")) {
                return ESUCC; // root dir always exists (without any node)
        }

        u32_t node_num;
        return node_find(hdl, hdl->tmp_path, S_IFDIR, blk, &node_num);
}

//==============================================================================
/**
 * @brief  Set selected bit in bitmap.
 *
 * @param  hdl          file system handle
 * @param  node_num     node number
 * @param  set          true: set, false: clear
 */
//==============================================================================
static void set_bitmap_bit(flatfs_t *hdl, u32_t node_num, bool set)
{
        u32_t mask = BIT(node_num % BITMAP_WORD_BITS);

        if (set) {
                hdl->bitmap.word[node_num / BITMAP_WORD_BITS] |= mask;
        } else {
                hdl->bitmap.word[node_num / BITMAP_WORD_BITS] &= ~mask;
        }
}

//==============================================================================
/**
 * @brief  Calculate CRC32 of fs and file blocks.
 *
 * @param  blk          fs or file block
 *
 * @return CRC32.
 */
//==============================================================================
static u32_t calc_blk_crc(const void *blk)
{
        static_assert(offsetof(blk_fs_t, total_volume_blocks) == offsetof(blk_file_t, ctime),
                "CRC area of FS and file block must start at the same offset!");

        const blk_fs_t *fsblk = blk;

        return calc_crc32(&fsblk->total_volume_blocks, BLOCK_SIZE - offsetof(blk_fs_t, total_volume_blocks),
                CRC32_INIT);
}

//==============================================================================
/**
 * @brief  Read file block.
 *
 * @param  hdl          file system handle
 * @param  node_num     node number
 * @param  blk          destination block buffer
 *
 * @return One of errno value (errno.h).
 */
//==============================================================================
static int file_block_read(flatfs_t *hdl, u32_t node_num, blk_file_t *blk)
{
        bool found = index_get(hdl, node_num, blk);
        if (found) {
                return ESUCC;
        }

        int err = block_read(hdl, node_get_blkaddr(hdl, node_num), blk, 1);
        if (!err) {
                err = check_file_block_coherency(hdl, node_num, blk);
        }

        return err;
}

//==============================================================================
/**
 * @brief  Write file block.
 *
 * @param  hdl          file system handle
 * @param  node_num     node number
 * @param  blk          source block buffer
 *
 * @return One of errno value (errno.h).
 */
//==============================================================================
static int file_block_write(flatfs_t *hdl, u32_t node_num, blk_file_t *blk)
{
        blk->crc = calc_blk_crc(blk);

        int err = block_write(hdl, node_get_blkaddr(hdl, node_num), blk, 1);
        if (!err) {
                index_add(hdl, node_num, blk);
        } else {
                index_remove(hdl, node_num); // medium state unknown
        }

        return err;
}

//==============================================================================
/**
 * @brief  Find selected node (file) in index cache.
 *
 * @param  hdl          file system handle
 * @param  name         node name
 * @param  mode         node type (S_IFMT part) or FIND_ANY_NODE
 * @param  blk          block buffer
 * @param  node_num     node number
 *
 * @return True if node found, otherwise false.
 */
//==============================================================================
static bool index_find(flatfs_t *hdl, const char *name, mode_t mode, blk_file_t *blk, u32_t *node_num)
{
#if __FLATFS_INDEX_SIZE__ > 0
        for (size_t i = 0; i < ARRAY_SIZE(hdl->path_index); i++) {

                path_index_t *index = &hdl->path_index[i];

                if (index->node != UINT32_MAX) {

                        if (isstreq(name, index->blk.name) and
                                (((index->blk.mode & S_IFMT) == mode) or (mode == FIND_ANY_NODE))) {

                                DBG("index hit [%li]: '%s' @ %lu", cast(long, index->hit), name,
                                        cast(unsigned long, index->node));

                                if (index->hit < INDEX_HIT_MAX) {
                                        index->hit++;
                                }

                                memcpy(blk, &index->blk, sizeof(*blk));

                                *node_num = index->node;

                                return true;
                        }
                }
        }

#else
        UNUSED_ARG5(hdl, name, mode, blk, node_num);
#endif

        return false;
}

//==============================================================================
/**
 * @brief  Get selected node (file) from index cache.
 *
 * @param  hdl          file system handle
 * @param  node_num     node number
 * @param  blk          block buffer
 *
 * @return True if node found, otherwise false.
 */
//==============================================================================
static bool index_get(flatfs_t *hdl, u32_t node_num, blk_file_t *blk)
{
#if __FLATFS_INDEX_SIZE__ > 0
        for (size_t i = 0; i < ARRAY_SIZE(hdl->path_index); i++) {

                path_index_t *index = &hdl->path_index[i];

                if (index->node == node_num) {

                        DBG("index hit [%li]: '%s' @ %lu", cast(long, index->hit), index->blk.name,
                                cast(unsigned long, index->node));

                        if (index->hit < INDEX_HIT_MAX) {
                                index->hit++;
                        }

                        memcpy(blk, &index->blk, sizeof(*blk));

                        return true;
                }
        }

#else
        UNUSED_ARG3(hdl, blk, node_num);
#endif

        return false;
}

//==============================================================================
/**
 * @brief  Create new index.
 *
 * @param  hdl          file system handle
 * @param  node_num     node number
 * @param  blk          block data
 */
//==============================================================================
static void index_add(flatfs_t *hdl, u32_t node_num, const blk_file_t *blk)
{
#if __FLATFS_INDEX_SIZE__ > 0
        /*
         * Find the less used or empty index.
         */
        i32_t min_hit = INT32_MAX;
        size_t entry_num = 0;

        for (size_t i = 0; i < ARRAY_SIZE(hdl->path_index); i++) {

                path_index_t *index = &hdl->path_index[i];

                if (index->node == node_num) {
                        index->blk = *blk;
                        if (index->hit < INDEX_HIT_MAX) {
                                index->hit++;
                        }
                        return;

                } else if (index->node == UINT32_MAX) {
                        // empty slot: use it, but continue looking for existing entry of node
                        if (min_hit != INT32_MIN) {
                                min_hit = INT32_MIN;
                                entry_num = i;
                        }

                } else if (index->hit < min_hit) {
                        min_hit = index->hit;
                        entry_num = i;
                }
        }

        /*
         * Use index slot for new node.
         */
        path_index_t *index = &hdl->path_index[entry_num];
        index->blk = *blk;
        index->hit = 0;
        index->node = node_num;

#else
        UNUSED_ARG3(hdl, node_num, blk);
#endif
}

//==============================================================================
/**
 * @brief  Clear address in index.
 *
 * @param  hdl          file system handle
 * @param  node_num     node number
 */
//==============================================================================
static void index_remove(flatfs_t *hdl, u32_t node_num)
{
#if __FLATFS_INDEX_SIZE__ > 0
        for (size_t i = 0; i < ARRAY_SIZE(hdl->path_index); i++) {
                path_index_t *index = &hdl->path_index[i];
                if (index->node == node_num) {
                        memset(index, 0, sizeof(*index));
                        index->node = UINT32_MAX;
                        break;
                }
        }
#else
        UNUSED_ARG2(hdl, node_num);
#endif
}

/*==============================================================================
  End of file
==============================================================================*/
