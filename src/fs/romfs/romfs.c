/*==============================================================================
File    romfs.c

Author  Daniel Zorychta

Brief   ROM File System (FPKG container)

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
  FPKG container layout (all values little endian):

        0x00            "FPKG\n" + padding to 64 bytes
        0x40            u32 total file size
        0x44            file_descriptor_t + name (no NUL) + content  (4 B aligned)
        ...             next descriptors
        entries_offset  file_entry_t[number_of_files]                (4 B aligned)
        EOF-24          container_header_t (last field = CRC32 of entire file)

  Names are absolute paths ("/dir/file"). When FLAG_SORTED is set, entries are
  sorted by name (byte order, as strcmp()), which allows binary search and
  early termination of directory listing. Images without the flag (created by
  older mkfpkg.py) are handled by linear search.

  NOTE: "encryption" is an obfuscation only - all seeds are stored in the
        container, no key is required to decode it.
==============================================================================*/

#pragma GCC optimize("Os")

/*==============================================================================
  Include files
==============================================================================*/
#include "fs/fs.h"
#include <build/vars.h>

/*==============================================================================
  Local macros
==============================================================================*/
#define FPKG_CRC32_INIT 0xFFFFFFFF

#define FILE_DESCRIPTOR_MAGIC  0x43534446
#define FILE_ENTRY_MAGIC       0x544E4546
#define CONTAINER_HEADER_MAGIC 0x474B5046

#define FLAG_ENCRYPTED (1 << 0)
#define FLAG_SORTED    (1 << 1)

#define is_encrypted(hdl) ((hdl)->flags & FLAG_ENCRYPTED)
#define is_sorted(hdl)    ((hdl)->flags & FLAG_SORTED)

#define NAME_BUF_LEN 512
#define MAX_PATH_LEN (NAME_BUF_LEN - 2)

/*==============================================================================
  Local object types
==============================================================================*/
typedef struct {
        uint32_t magic;
        uint32_t crc32;
        uint32_t file_name_len;
        uint32_t file_name_crc32;
        uint32_t file_size;
        uint32_t file_crc32;
        uint32_t content_prng_seed;
        uint32_t flags;
        uint8_t file_name_file_data[];
} file_descriptor_t;

typedef struct {
        uint32_t magic;
        uint32_t crc32;
        uint32_t file_descriptor_address;
        uint32_t reserved;
} file_entry_t;

typedef struct {
        uint32_t magic;
        uint32_t crc32;
        uint32_t prng_seed;
        uint32_t number_of_files;
        uint32_t flags;
        uint32_t file_crc32;
} container_header_t;

typedef struct {
        kfile_t *file;
        kmtx_t *mtx;
        uint32_t file_size;
        uint32_t entries_offset;
        uint32_t prng_seed;
        uint32_t number_of_files;
        uint32_t flags;
        uint32_t open_files_ctr;
        uint32_t open_dirs_ctr;
        char tmp[NAME_BUF_LEN];
        bool exec_reg_files;
} romfs_t;

typedef struct {
        uint32_t index;
        file_entry_t entry;
        file_descriptor_t desc;
        bool is_dir;
} romfs_item_t;

struct fpkg_file {
        uint32_t size;
        uint32_t seek;
        uint32_t content_prng_seed;
        uint32_t current_seed;
        uint32_t content_offset;
};

struct fpkg_dir {
        char *root;      /* directory path with trailing '/' */
        char *item_name; /* last returned item */
        uint32_t root_len;
        uint32_t start; /* first entry that can belong to directory */
        uint32_t seek;
};

/*==============================================================================
  Local function prototypes
==============================================================================*/
static int romfs_destroy(void **fs_handle);

static int read_image_info(romfs_t *hdl);

static int normalize_path(const char *path, size_t *key_len);

static int lookup(romfs_t *hdl, const char *key, size_t key_len, romfs_item_t *item);

static int dir_next(romfs_t *hdl, struct fpkg_dir *dir, bool *is_dir, uint32_t *size);

static int dir_reset_item_name(struct fpkg_dir *dir);

static void fpkg__fseek(romfs_t *hdl, struct fpkg_file *file, uint32_t seek);

static int read_file(void *buf, size_t len, kfile_t *file);

static uint32_t prng(uint32_t *seed);

static void xcrypt(void *buf, uint32_t buflen, uint32_t *seed);

static int file_validity_check(romfs_t *hdl);

static bool is_container_header_valid(const container_header_t *hdr);

static bool is_file_entry_valid(romfs_t *hdl, const file_entry_t *file_entry);

static bool is_file_descriptor_valid(romfs_t *hdl, const file_entry_t *file_entry, const file_descriptor_t *file_desc);

static int read_container_header(romfs_t *hdl, container_header_t *hdr);

static int read_file_entry(romfs_t *hdl, uint32_t n, file_entry_t *entry);

static int read_file_descriptor(romfs_t *hdl, file_entry_t *entry, file_descriptor_t *desc);

static int read_file_descriptor_name(romfs_t *hdl, file_entry_t *entry, file_descriptor_t *desc, char *buf,
        size_t buf_len, bool *truncated);

static int read_item(romfs_t *hdl, uint32_t n, file_entry_t *entry, file_descriptor_t *desc, bool *truncated);

static int region_crc32(romfs_t *hdl, uint32_t offset, uint32_t len, uint32_t seed, bool decrypt, uint32_t *crc_out);

static uint32_t calc_CRC32(const void *buf, uint32_t buflen, uint32_t init);

/*==============================================================================
  Local objects
==============================================================================*/

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
 * Options:
 *      exec=<bool>     regular files are executable
 *      validate        full image check (CRC of image, entries, names and content)
 *
 * @param[out]          **fs_handle             file system allocated memory
 * @param[in ]           *src_path              file source path
 * @param[in ]           *opts                  file system options (can be NULL)
 *
 * @return One of errno value (errno.h).
 */
//==============================================================================
API_FS_INIT(romfs, void **fs_handle, const char *src_path, const char *opts)
{
        int err = sys_zalloc(sizeof(romfs_t), fs_handle);
        if (err) {
                return err;
        }

        romfs_t *hdl = *fs_handle;

        hdl->exec_reg_files = sys_stropt_get_bool(opts, "exec", __ROMFS_CFG_EXEC_FILES__);

        err = sys_mutex_create(KMTX_TYPE_RECURSIVE, &hdl->mtx);

        if (not err) {
                err = sys_fopen(src_path, O_RDONLY, 0, &hdl->file);
        }

        if (not err) {
                err = read_image_info(hdl);
        }

        if (not err and sys_stropt_is_flag(opts, "validate")) {
                err = file_validity_check(hdl);
        }

        if (err) {
                romfs_destroy(fs_handle);
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
API_FS_RELEASE(romfs, void *fs_handle)
{
        romfs_t *hdl = fs_handle;

        int err = sys_mutex_lock(hdl->mtx, _WAIT_FOREVER_MS);
        if (err) {
                return err;
        }

        bool busy = hdl->open_files_ctr or hdl->open_dirs_ctr;

        sys_mutex_unlock(hdl->mtx);

        if (busy) {
                return EBUSY;
        }

        return romfs_destroy(&fs_handle);
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
API_FS_OPEN(romfs, void *fs_handle, void **fhdl, fpos_t *fpos, const char *path, u32_t flags)
{
        romfs_t *hdl = fs_handle;

        if (flags & (O_WRONLY | O_RDWR | O_CREAT | O_TRUNC | O_APPEND)) {
                return EROFS;
        }

        size_t key_len;
        int err = normalize_path(path, &key_len);
        if (err) {
                return err;
        }

        *fpos = 0;

        err = sys_mutex_lock(hdl->mtx, _WAIT_FOREVER_MS);
        if (err) {
                return err;
        }

        romfs_item_t item;
        err = lookup(hdl, path, key_len, &item);

        if (not err and item.is_dir) {
                err = EISDIR;
        }

        if (not err) {
                struct fpkg_file *file;
                err = sys_malloc(sizeof(*file), cast(void **, &file));
                if (not err) {
                        file->content_offset = item.entry.file_descriptor_address + sizeof(file_descriptor_t) +
                                item.desc.file_name_len;

                        file->content_prng_seed = item.desc.content_prng_seed;
                        file->current_seed = item.desc.content_prng_seed;
                        file->size = item.desc.file_size;
                        file->seek = 0;

                        hdl->open_files_ctr++;

                        *fhdl = file;
                }
        }

        sys_mutex_unlock(hdl->mtx);

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
API_FS_CLOSE(romfs, void *fs_handle, void *fhdl)
{
        romfs_t *hdl = fs_handle;

        sys_free(&fhdl);

        // resources are released even if mutex cannot be taken
        int err = sys_mutex_lock(hdl->mtx, _WAIT_FOREVER_MS);

        hdl->open_files_ctr--;

        if (not err) {
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
 * @param[out]          *wrcnt                  number of written bytes
 * @param[in ]           fattr                  file attributes
 *
 * @return One of errno value (errno.h).
 */
//==============================================================================
API_FS_WRITE(romfs, void *fs_handle, void *fhdl, const u8_t *src, size_t count, fpos_t *fpos, size_t *wrcnt,
        struct vfs_fattr fattr)
{
        UNUSED_ARG7(fs_handle, fhdl, src, count, fpos, wrcnt, fattr);
        return EROFS;
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
 * @param[out]          *rdcnt                  number of read bytes
 * @param[in ]           fattr                  file attributes
 *
 * @return One of errno value (errno.h).
 */
//==============================================================================
API_FS_READ(romfs, void *fs_handle, void *fhdl, u8_t *dst, size_t count, fpos_t *fpos, size_t *rdcnt,
        struct vfs_fattr fattr)
{
        UNUSED_ARG1(fattr);

        romfs_t *hdl = fs_handle;
        struct fpkg_file *file = fhdl;

        *rdcnt = 0;

        int err = sys_mutex_lock(hdl->mtx, _WAIT_FOREVER_MS);
        if (err) {
                return err;
        }

        uint64_t pos = *fpos;
        fpkg__fseek(hdl, file, (pos > file->size) ? file->size : cast(uint32_t, pos));

        size_t len = min(cast(size_t, file->size - file->seek), count);
        if (len > 0) {
                err = sys_fseek(hdl->file, file->content_offset + file->seek, VFS_SEEK_SET);

                if (not err) {
                        err = read_file(dst, len, hdl->file);
                }

                if (not err) {
                        file->seek += len;
                        *rdcnt = len;

                        if (is_encrypted(hdl)) {
                                xcrypt(dst, len, &file->current_seed);
                        }
                }
        }

        sys_mutex_unlock(hdl->mtx);

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
API_FS_IOCTL(romfs, void *fs_handle, void *fhdl, int request, void *arg)
{
        UNUSED_ARG4(fs_handle, fhdl, request, arg);
        return ENOTSUP;
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
API_FS_FLUSH(romfs, void *fs_handle, void *fhdl)
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
API_FS_FSTAT(romfs, void *fs_handle, void *fhdl, struct stat *stat)
{
        romfs_t *hdl = fs_handle;
        struct fpkg_file *file = fhdl;

        stat->st_ctime = COMPILE_EPOCH_TIME;
        stat->st_mtime = COMPILE_EPOCH_TIME;
        stat->st_dev = 0;
        stat->st_size = file->size;
        stat->st_gid = 0;
        stat->st_uid = 0;
        stat->st_mode = S_IRUSR | S_IRGRP | S_IROTH | (S_IXUSR * hdl->exec_reg_files) | S_IFREG;

        return ESUCC;
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
API_FS_STAT(romfs, void *fs_handle, const char *path, struct stat *stat)
{
        romfs_t *hdl = fs_handle;

        size_t key_len;
        int err = normalize_path(path, &key_len);
        if (err) {
                return err;
        }

        err = sys_mutex_lock(hdl->mtx, _WAIT_FOREVER_MS);
        if (err) {
                return err;
        }

        romfs_item_t item;
        err = lookup(hdl, path, key_len, &item);
        if (not err) {
                stat->st_ctime = COMPILE_EPOCH_TIME;
                stat->st_mtime = COMPILE_EPOCH_TIME;
                stat->st_dev = 0;
                stat->st_gid = 0;
                stat->st_uid = 0;
                stat->st_mode = S_IRUSR | S_IRGRP | S_IROTH;

                if (item.is_dir) {
                        stat->st_size = 0;
                        stat->st_mode |= S_IFDIR;
                } else {
                        stat->st_size = item.desc.file_size;
                        stat->st_mode |= S_IFREG | (S_IXUSR * hdl->exec_reg_files);
                }
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
API_FS_STATFS(romfs, void *fs_handle, struct statfs *statfs)
{
        romfs_t *hdl = fs_handle;

        statfs->f_bsize = 1;
        statfs->f_blocks = hdl->file_size;
        statfs->f_bfree = 0;
        statfs->f_ffree = 0;
        statfs->f_files = hdl->number_of_files;
        statfs->f_type = SYS_FS_TYPE__SOLID;
        statfs->f_fsname = "romfs";

        return ESUCC;
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
API_FS_MKDIR(romfs, void *fs_handle, const char *path, mode_t mode)
{
        UNUSED_ARG3(fs_handle, path, mode);
        return EROFS;
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
API_FS_MKFIFO(romfs, void *fs_handle, const char *path, mode_t mode)
{
        UNUSED_ARG3(fs_handle, path, mode);
        return EROFS;
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
API_FS_MKNOD(romfs, void *fs_handle, const char *path, const dev_t dev)
{
        UNUSED_ARG3(fs_handle, path, dev);
        return EROFS;
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
API_FS_OPENDIR(romfs, void *fs_handle, const char *path, kdir_t *kdir)
{
        romfs_t *hdl = fs_handle;

        size_t key_len;
        int err = normalize_path(path, &key_len);
        if (err) {
                return err;
        }

        struct fpkg_dir *dir;
        err = sys_zalloc(sizeof(*dir), cast(void **, &dir));
        if (err) {
                return err;
        }

        err = sys_malloc(key_len + 2, cast(void **, &dir->root));
        if (err) {
                goto error;
        }

        memcpy(dir->root, path, key_len);
        dir->root[key_len] = '/';
        dir->root[key_len + 1] = '\0';
        dir->root_len = key_len + 1;

        err = sys_strdup("", &dir->item_name);
        if (err) {
                goto error;
        }

        err = sys_mutex_lock(hdl->mtx, _WAIT_FOREVER_MS);
        if (err) {
                goto error;
        }

        romfs_item_t item;
        err = lookup(hdl, path, key_len, &item);

        if (not err and not item.is_dir) {
                err = ENOTDIR;
        }

        // count directory items
        uint32_t items = 0;

        if (not err) {
                dir->start = item.index;
                dir->seek = item.index;

                bool is_dir;
                uint32_t size;
                while ((err = dir_next(hdl, dir, &is_dir, &size)) == ESUCC) {
                        items++;
                }

                if (err == ENOENT) {
                        err = dir_reset_item_name(dir);
                }
        }

        if (not err) {
                dir->seek = 0;

                kdir->d_hdl = dir;
                kdir->d_items = items;
                kdir->d_seek = 0;

                hdl->open_dirs_ctr++;
        }

        sys_mutex_unlock(hdl->mtx);

error:
        if (err) {
                if (dir->root) {
                        sys_free(cast(void *, &dir->root));
                }

                if (dir->item_name) {
                        sys_free(cast(void *, &dir->item_name));
                }

                sys_free(cast(void *, &dir));
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
API_FS_CLOSEDIR(romfs, void *fs_handle, kdir_t *kdir)
{
        romfs_t *hdl = fs_handle;
        struct fpkg_dir *dir = kdir->d_hdl;

        sys_free(cast(void *, &dir->root));
        sys_free(cast(void *, &dir->item_name));
        sys_free(cast(void *, &dir));

        // resources are released even if mutex cannot be taken
        int err = sys_mutex_lock(hdl->mtx, _WAIT_FOREVER_MS);

        hdl->open_dirs_ctr--;

        if (not err) {
                sys_mutex_unlock(hdl->mtx);
        }

        return err;
}

//==============================================================================
/**
 * @brief Read directory.
 *
 * @param[in ]          *fs_handle              file system allocated memory
 * @param[in,out]       *kdir                   directory object
 *
 * @return One of errno value (errno.h).
 */
//==============================================================================
API_FS_READDIR(romfs, void *fs_handle, kdir_t *kdir)
{
        romfs_t *hdl = fs_handle;
        struct fpkg_dir *dir = kdir->d_hdl;

        if (dir->seek != kdir->d_seek) {
                int err = dir_reset_item_name(dir);
                if (err) {
                        return err;
                }

                dir->seek = kdir->d_seek;
        }

        int err = sys_mutex_lock(hdl->mtx, _WAIT_FOREVER_MS);
        if (err) {
                return err;
        }

        bool is_dir;
        uint32_t size;
        err = dir_next(hdl, dir, &is_dir, &size);

        kdir->d_seek = dir->seek;

        if (not err) {
                if (is_dir) {
                        kdir->dirent.mode = S_IFDIR;
                } else {
                        kdir->dirent.mode = S_IFREG | (S_IXUSR * hdl->exec_reg_files);
                }

                kdir->dirent.mode |= S_IRUSR | S_IRGRP | S_IROTH;
                kdir->dirent.size = size;
                kdir->dirent.dev = 0;
                kdir->dirent.d_name = dir->item_name;
        }

        sys_mutex_unlock(hdl->mtx);

        return err;
}

//==============================================================================
/**
 * @brief Remove file/directory.
 *
 * @param[in ]          *fs_handle              file system allocated memory
 * @param[in ]          *path                   name of removed file/directory
 *
 * @return One of errno value (errno.h).
 */
//==============================================================================
API_FS_REMOVE(romfs, void *fs_handle, const char *path)
{
        UNUSED_ARG2(fs_handle, path);
        return EROFS;
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
API_FS_RENAME(romfs, void *fs_handle, const char *old_name, const char *new_name)
{
        UNUSED_ARG3(fs_handle, old_name, new_name);
        return EROFS;
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
API_FS_CHMOD(romfs, void *fs_handle, const char *path, mode_t mode)
{
        UNUSED_ARG3(fs_handle, path, mode);
        return EROFS;
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
API_FS_CHOWN(romfs, void *fs_handle, const char *path, uid_t owner, gid_t group)
{
        UNUSED_ARG4(fs_handle, path, owner, group);
        return EROFS;
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
API_FS_SYNC(romfs, void *fs_handle)
{
        UNUSED_ARG1(fs_handle);
        return ESUCC;
}

//==============================================================================
/**
 * @brief  Release all file system resources. Safe for partially initialized
 *         object.
 *
 * @param  fs_handle    pointer to file system handle
 *
 * @return One of errno value.
 */
//==============================================================================
static int romfs_destroy(void **fs_handle)
{
        romfs_t *hdl = *fs_handle;

        if (hdl->file) {
                sys_fclose(hdl->file);
        }

        if (hdl->mtx) {
                sys_mutex_destroy(hdl->mtx);
        }

        return sys_free(fs_handle);
}

//==============================================================================
/**
 * @brief  Read and check container header, compute image geometry.
 *
 * @param  hdl          FS handler
 *
 * @return One of errno value.
 */
//==============================================================================
static int read_image_info(romfs_t *hdl)
{
        int err = sys_fseek(hdl->file, 0, VFS_SEEK_END);
        if (err) {
                return err;
        }

        i64_t size;
        err = sys_ftell(hdl->file, &size);
        if (err) {
                return err;
        }

        if ((size < cast(i64_t, sizeof(container_header_t))) or (size > cast(i64_t, UINT32_MAX))) {
                return EILSEQ;
        }

        hdl->file_size = size;

        container_header_t header;
        err = read_container_header(hdl, &header);
        if (err) {
                return err;
        }

        if (not is_container_header_valid(&header)) {
                return EILSEQ;
        }

        uint64_t tables_size = sizeof(container_header_t) +
                (cast(uint64_t, header.number_of_files) * sizeof(file_entry_t));

        if (tables_size > hdl->file_size) {
                return EILSEQ;
        }

        hdl->entries_offset = hdl->file_size - tables_size;
        hdl->prng_seed = header.prng_seed;
        hdl->number_of_files = header.number_of_files;
        hdl->flags = header.flags;

        return ESUCC;
}

//==============================================================================
/**
 * @brief  Compute search key length: trailing slashes are ignored, so "/dir/"
 *         and "/dir" are the same object, and "/" is the root (length 0).
 *
 * @param  path         path
 * @param  key_len      key length
 *
 * @return One of errno value.
 */
//==============================================================================
static int normalize_path(const char *path, size_t *key_len)
{
        size_t len = strlen(path);

        while ((len > 0) and (path[len - 1] == '/')) {
                len--;
        }

        if (len > MAX_PATH_LEN) {
                return ENAMETOOLONG;
        }

        *key_len = len;

        return ESUCC;
}

//==============================================================================
/**
 * @brief  Compare entry name with key (+ optional trailing '/'). Bytes are
 *         compared as unsigned (as strcmp()).
 *
 * @param  name         entry name (may be truncated)
 * @param  truncated    name was truncated (real name is longer)
 * @param  key          key
 * @param  key_len      key length
 * @param  slash        append '/' to key
 *
 * @return <0, 0, >0 as strcmp().
 */
//==============================================================================
static int name_cmp(const char *name, bool truncated, const char *key, size_t key_len, bool slash)
{
        size_t total = key_len + (slash ? 1 : 0);

        for (size_t i = 0; i < total; i++) {
                uint8_t k = (i < key_len) ? cast(uint8_t, key[i]) : '/';
                uint8_t n = cast(uint8_t, name[i]);

                if (n != k) {
                        return (n < k) ? -1 : 1;
                }
        }

        return (name[total] or truncated) ? 1 : 0;
}

//==============================================================================
/**
 * @brief  Check if name is located in directory key ("key/...").
 *
 * @param  name         entry name
 * @param  key          directory key
 * @param  key_len      key length
 *
 * @return True if name is in directory (or subdirectory).
 */
//==============================================================================
static bool is_in_dir(const char *name, const char *key, size_t key_len)
{
        return (strncmp(name, key, key_len) == 0) and (name[key_len] == '/');
}

//==============================================================================
/**
 * @brief  Find first entry that is not less than key (lower bound).
 *
 * @param  hdl          FS handler
 * @param  lo           search start index
 * @param  key          key
 * @param  key_len      key length
 * @param  slash        append '/' to key
 * @param  idx          found index (number_of_files if not found)
 *
 * @return One of errno value.
 */
//==============================================================================
static int lower_bound(romfs_t *hdl, uint32_t lo, const char *key, size_t key_len, bool slash, uint32_t *idx)
{
        uint32_t hi = hdl->number_of_files;

        while (lo < hi) {
                uint32_t mid = lo + (hi - lo) / 2;

                file_entry_t entry;
                file_descriptor_t desc;
                bool trunc;
                int err = read_item(hdl, mid, &entry, &desc, &trunc);
                if (err) {
                        return err;
                }

                if (name_cmp(hdl->tmp, trunc, key, key_len, slash) < 0) {
                        lo = mid + 1;
                } else {
                        hi = mid;
                }
        }

        *idx = lo;

        return ESUCC;
}

//==============================================================================
/**
 * @brief  Binary search of file or directory (sorted images only).
 *
 * @param  hdl          FS handler
 * @param  key          key
 * @param  key_len      key length
 * @param  item         found item
 *
 * @return One of errno value.
 */
//==============================================================================
static int lookup_sorted(romfs_t *hdl, const char *key, size_t key_len, romfs_item_t *item)
{
        bool trunc;
        uint32_t i;

        int err = lower_bound(hdl, 0, key, key_len, false, &i);
        if (err) {
                return err;
        }

        if (i < hdl->number_of_files) {
                err = read_item(hdl, i, &item->entry, &item->desc, &trunc);
                if (err) {
                        return err;
                }

                if (name_cmp(hdl->tmp, trunc, key, key_len, false) == 0) {
                        item->index = i;
                        item->is_dir = false;
                        return ESUCC;
                }
        }

        // "key/" > "key", so directory content cannot be located before i
        err = lower_bound(hdl, i, key, key_len, true, &i);
        if (err) {
                return err;
        }

        if (i < hdl->number_of_files) {
                err = read_item(hdl, i, &item->entry, &item->desc, &trunc);
                if (err) {
                        return err;
                }

                if (is_in_dir(hdl->tmp, key, key_len)) {
                        item->index = i;
                        item->is_dir = true;
                        return ESUCC;
                }
        }

        return ENOENT;
}

//==============================================================================
/**
 * @brief  Linear search of file or directory (unsorted images). Name is read
 *         only if length (and CRC for files) match.
 *
 * @param  hdl          FS handler
 * @param  key          key
 * @param  key_len      key length
 * @param  item         found item
 *
 * @return One of errno value.
 */
//==============================================================================
static int lookup_linear(romfs_t *hdl, const char *key, size_t key_len, romfs_item_t *item)
{
        uint32_t key_crc = calc_CRC32(key, key_len, FPKG_CRC32_INIT);

        for (uint32_t i = 0; i < hdl->number_of_files; i++) {

                int err = read_file_entry(hdl, i, &item->entry);
                if (err) {
                        return err;
                }

                err = read_file_descriptor(hdl, &item->entry, &item->desc);
                if (err) {
                        return err;
                }

                bool exact = (item->desc.file_name_len == key_len);
                bool longer = (item->desc.file_name_len > (key_len + 1));

                if (exact and (item->desc.file_name_crc32 != key_crc)) {
                        continue;
                }

                if (not exact and not longer) {
                        continue;
                }

                bool trunc;
                err = read_file_descriptor_name(hdl, &item->entry, &item->desc, hdl->tmp, sizeof(hdl->tmp), &trunc);
                if (err) {
                        return err;
                }

                if (exact and (memcmp(hdl->tmp, key, key_len) == 0)) {
                        item->index = i;
                        item->is_dir = false;
                        return ESUCC;
                }

                if (longer and is_in_dir(hdl->tmp, key, key_len)) {
                        item->index = i;
                        item->is_dir = true;
                        return ESUCC;
                }
        }

        return ENOENT;
}

//==============================================================================
/**
 * @brief  Find file or directory. Root (key_len == 0) always exists.
 *
 * @param  hdl          FS handler
 * @param  key          path (first key_len characters are used)
 * @param  key_len      key length
 * @param  item         found item (for directory: index of first entry)
 *
 * @return One of errno value.
 */
//==============================================================================
static int lookup(romfs_t *hdl, const char *key, size_t key_len, romfs_item_t *item)
{
        if (key_len == 0) {
                item->index = 0;
                item->is_dir = true;
                return ESUCC;
        }

        if (is_sorted(hdl)) {
                return lookup_sorted(hdl, key, key_len, item);
        } else {
                return lookup_linear(hdl, key, key_len, item);
        }
}

//==============================================================================
/**
 * @brief  Set last directory item name to empty string.
 *
 * @param  dir          directory object
 *
 * @return One of errno value.
 */
//==============================================================================
static int dir_reset_item_name(struct fpkg_dir *dir)
{
        char *empty;
        int err = sys_strdup("", &empty);
        if (not err) {
                sys_free(cast(void *, &dir->item_name));
                dir->item_name = empty;
        }

        return err;
}

//==============================================================================
/**
 * @brief  Find next directory item. Duplicated subdirectory names are skipped
 *         only if they are adjacent (always true for sorted images).
 *
 * @param  hdl          FS handler (mutex must be locked)
 * @param  dir          directory object
 * @param  is_dir       item is a directory
 * @param  size         item size
 *
 * @return ESUCC if found (name in dir->item_name), ENOENT at end of directory.
 */
//==============================================================================
static int dir_next(romfs_t *hdl, struct fpkg_dir *dir, bool *is_dir, uint32_t *size)
{
        if (dir->seek < dir->start) {
                dir->seek = dir->start;
        }

        while (dir->seek < hdl->number_of_files) {

                file_entry_t entry;
                file_descriptor_t desc;
                bool trunc;
                int err = read_item(hdl, dir->seek, &entry, &desc, &trunc);
                if (err) {
                        return err;
                }

                dir->seek++;

                if (strncmp(hdl->tmp, dir->root, dir->root_len) != 0) {
                        if (is_sorted(hdl)) {
                                // sorted: directory content is contiguous
                                dir->seek = hdl->number_of_files;
                                break;
                        }
                        continue;
                }

                char *item_name = hdl->tmp + dir->root_len;

                char *slash = strchr(item_name, '/');
                if (slash) {
                        *slash = '\0';
                } else if (trunc) {
                        continue; // name cannot be represented
                }

                if ((item_name[0] == '\0') or (strcmp(item_name, dir->item_name) == 0)) {
                        continue;
                }

                char *name;
                err = sys_strdup(item_name, &name);
                if (err) {
                        dir->seek--;
                        return err;
                }

                sys_free(cast(void *, &dir->item_name));
                dir->item_name = name;

                *is_dir = (slash != NULL);
                *size = slash ? 0 : desc.file_size;

                return ESUCC;
        }

        return ENOENT;
}

//==============================================================================
/**
 * @brief  Set file position.
 *
 * Keystream byte depends only on 8 LSB of LCG state (modulus 2^32), and these
 * bits have period 256, so seed is advanced by (distance mod 256) steps.
 *
 * @param  hdl          FS handler
 * @param  file         File representation object.
 * @param  seek         position
 */
//==============================================================================
static void fpkg__fseek(romfs_t *hdl, struct fpkg_file *file, uint32_t seek)
{
        if (seek > file->size) {
                seek = file->size;
        }

        if (is_encrypted(hdl)) {
                uint32_t n;

                if (seek >= file->seek) {
                        n = seek - file->seek;
                } else {
                        file->current_seed = file->content_prng_seed;
                        n = seek;
                }

                n &= 0xFF;

                while (n > 0) {
                        prng(&file->current_seed);
                        n--;
                }
        }

        file->seek = seek;
}

//==============================================================================
/**
 * @brief  Calculate CRC32.
 *
 * @param  buf          buffer
 * @param  buflen       buffer length
 * @param  init         initial value
 *
 * @return CRC32 result.
 */
//==============================================================================
static uint32_t calc_CRC32(const void *buf, uint32_t buflen, uint32_t init)
{
        const uint8_t *b = buf;

        uint32_t crc = init;

        while (buflen--) {

                crc = crc ^ *b++;

                for (int shift = 7; shift >= 0; shift--) {
                        uint32_t mask = -(crc & 1);
                        crc = (crc >> 1) ^ (0xEDB88320 & mask);
                }
        }

        return ~crc;
}

//==============================================================================
/**
 * @brief Function returns a pseudo-random integer.
 *
 * @param  seed         seed value
 *
 * @return The function return a pseudo-random value.
 */
//==============================================================================
static uint32_t prng(uint32_t *seed)
{
        *seed = (1103515245UL * *seed) + 12345UL;

        return *seed;
}

//==============================================================================
/**
 * @brief  Function encode/decode selected block.
 *
 * @param  buf          buffer
 * @param  buflen       buffer length
 * @param  seed         seed value
 */
//==============================================================================
static void xcrypt(void *buf, uint32_t buflen, uint32_t *seed)
{
        uint8_t *blk = buf;

        while (buflen > 0) {
                *blk = *blk ^ prng(seed);
                blk++;
                buflen--;
        }
}

//==============================================================================
/**
 * @brief  Read file by checking all possible errors.
 *
 * @param  buf          destination buffer
 * @param  len          read length
 * @param  file         file
 *
 * @return One of errno value.
 */
//==============================================================================
static int read_file(void *buf, size_t len, kfile_t *file)
{
        sys_clearerr(file);

        size_t rdcnt;
        int err = sys_fread(buf, len, &rdcnt, file);
        if (err) {
                return err;
        }

        int file_error;
        err = sys_ferror(file, &file_error);
        if (err) {
                return err;
        }

        if (file_error) {
                return EIO;
        }

        if (rdcnt != len) {
                return EIO;
        }

        return ESUCC;
}

//==============================================================================
/**
 * @brief  Read container header.
 *
 * @param  hdl          FS handler
 * @param  hdr          header destination buffer
 *
 * @return On success 0 is returned.
 */
//==============================================================================
static int read_container_header(romfs_t *hdl, container_header_t *hdr)
{
        int err = sys_fseek(hdl->file, hdl->file_size - sizeof(container_header_t), VFS_SEEK_SET);
        if (err) {
                return err;
        }

        err = read_file(hdr, sizeof(*hdr), hdl->file);
        if (err) {
                return err;
        }

        if (hdr->prng_seed != 0) {
                uint32_t seed = hdr->prng_seed + 1;
                hdr->number_of_files ^= prng(&seed);

                seed = hdr->prng_seed + 2;
                hdr->flags ^= prng(&seed);
        }

        return err;
}

//==============================================================================
/**
 * @brief  Read file entry.
 *
 * @param  hdl          FS handler
 * @param  n            n-th entry
 * @param  entry        file entry buffer
 *
 * @return On success 0 is returned.
 */
//==============================================================================
static int read_file_entry(romfs_t *hdl, uint32_t n, file_entry_t *entry)
{
        int err = sys_fseek(hdl->file, hdl->entries_offset + (sizeof(file_entry_t) * n), VFS_SEEK_SET);
        if (err) {
                return err;
        }

        err = read_file(entry, sizeof(*entry), hdl->file);
        if (err) {
                return err;
        }

        if (is_encrypted(hdl)) {
                uint32_t seed = hdl->prng_seed + n;
                xcrypt(entry, sizeof(file_entry_t), &seed);
        }

        return err;
}

//==============================================================================
/**
 * @brief  Read file descriptor.
 *
 * @param  hdl          FS handler
 * @param  entry        file entry
 * @param  desc         descriptor buffer
 *
 * @return On success 0 is returned.
 */
//==============================================================================
static int read_file_descriptor(romfs_t *hdl, file_entry_t *entry, file_descriptor_t *desc)
{
        int err = sys_fseek(hdl->file, entry->file_descriptor_address, VFS_SEEK_SET);
        if (err) {
                return err;
        }

        err = read_file(desc, sizeof(*desc), hdl->file);
        if (err) {
                return err;
        }

        if (is_encrypted(hdl)) {
                uint32_t seed = hdl->prng_seed;
                xcrypt(desc, sizeof(file_descriptor_t), &seed);
        }

        return err;
}

//==============================================================================
/**
 * @brief  Read file name. Name longer than buffer is truncated.
 *
 * @param  hdl          FS handler
 * @param  entry        file entry
 * @param  desc         descriptor
 * @param  buf          destination buffer
 * @param  buf_len      destination buffer size
 * @param  truncated    name was truncated
 *
 * @return On success 0 is returned.
 */
//==============================================================================
static int read_file_descriptor_name(romfs_t *hdl, file_entry_t *entry, file_descriptor_t *desc, char *buf,
        size_t buf_len, bool *truncated)
{
        if (buf_len < 2) {
                return EINVAL;
        }

        size_t name_len = desc->file_name_len;
        *truncated = false;

        if (name_len > (buf_len - 1)) {
                name_len = buf_len - 1;
                *truncated = true;
        }

        int err = sys_fseek(hdl->file, entry->file_descriptor_address + sizeof(file_descriptor_t), VFS_SEEK_SET);
        if (err) {
                return err;
        }

        err = read_file(buf, name_len, hdl->file);
        if (err) {
                return err;
        }

        if (is_encrypted(hdl)) {
                uint32_t seed = desc->content_prng_seed;
                xcrypt(buf, name_len, &seed);
        }

        buf[name_len] = '\0';

        return err;
}

//==============================================================================
/**
 * @brief  Read entry, descriptor and name (to hdl->tmp) of n-th item.
 *
 * @param  hdl          FS handler
 * @param  n            item index
 * @param  entry        entry buffer
 * @param  desc         descriptor buffer
 * @param  truncated    name was truncated
 *
 * @return One of errno value.
 */
//==============================================================================
static int read_item(romfs_t *hdl, uint32_t n, file_entry_t *entry, file_descriptor_t *desc, bool *truncated)
{
        int err = read_file_entry(hdl, n, entry);

        if (not err) {
                err = read_file_descriptor(hdl, entry, desc);
        }

        if (not err) {
                err = read_file_descriptor_name(hdl, entry, desc, hdl->tmp, sizeof(hdl->tmp), truncated);
        }

        return err;
}

//==============================================================================
/**
 * @brief  Calculate CRC32 of image region (optionally decoded). Uses hdl->tmp.
 *
 * @param  hdl          FS handler
 * @param  offset       region offset
 * @param  len          region length
 * @param  seed         decode seed
 * @param  decrypt      decode region
 * @param  crc_out      CRC32 result
 *
 * @return One of errno value.
 */
//==============================================================================
static int region_crc32(romfs_t *hdl, uint32_t offset, uint32_t len, uint32_t seed, bool decrypt, uint32_t *crc_out)
{
        uint8_t *buf = cast(uint8_t *, hdl->tmp);
        uint32_t crc = ~FPKG_CRC32_INIT;

        int err = sys_fseek(hdl->file, offset, VFS_SEEK_SET);

        while (not err and (len > 0)) {
                uint32_t n = min(len, cast(uint32_t, sizeof(hdl->tmp)));

                err = read_file(buf, n, hdl->file);
                if (not err) {
                        if (decrypt) {
                                xcrypt(buf, n, &seed);
                        }

                        crc = calc_CRC32(buf, n, ~crc);
                        len -= n;
                }
        }

        *crc_out = crc;

        return err;
}

//==============================================================================
/**
 * @brief  Full image check: CRC of entire image, entries, descriptors, names,
 *         content and (for sorted images) order of names.
 *
 * @param  hdl          FS handler (geometry already read)
 *
 * @return On success 0 is returned.
 */
//==============================================================================
static int file_validity_check(romfs_t *hdl)
{
        uint32_t crc;
        int err = region_crc32(hdl, 0, hdl->file_size - sizeof(uint32_t), 0, false, &crc);
        if (err) {
                return err;
        }

        uint32_t file_crc;
        err = read_file(&file_crc, sizeof(file_crc), hdl->file);
        if (err) {
                return err;
        }

        if (file_crc != crc) {
                return EILSEQ;
        }

        char *prev = NULL;
        bool prev_trunc = false;

        if (is_sorted(hdl)) {
                err = sys_malloc(NAME_BUF_LEN, cast(void **, &prev));
                if (err) {
                        return err;
                }
        }

        for (uint32_t i = 0; (i < hdl->number_of_files) and not err; i++) {

                file_entry_t entry;
                err = read_file_entry(hdl, i, &entry);
                if (err) {
                        break;
                }

                if (not is_file_entry_valid(hdl, &entry)) {
                        err = EILSEQ;
                        break;
                }

                file_descriptor_t desc;
                err = read_file_descriptor(hdl, &entry, &desc);
                if (err) {
                        break;
                }

                if (not is_file_descriptor_valid(hdl, &entry, &desc)) {
                        err = EILSEQ;
                        break;
                }

                uint32_t name_offset = entry.file_descriptor_address + sizeof(file_descriptor_t);

                err = region_crc32(hdl, name_offset, desc.file_name_len, desc.content_prng_seed, is_encrypted(hdl),
                        &crc);
                if (err) {
                        break;
                }

                if (crc != desc.file_name_crc32) {
                        err = EILSEQ;
                        break;
                }

                err = region_crc32(hdl, name_offset + desc.file_name_len, desc.file_size, desc.content_prng_seed,
                        is_encrypted(hdl), &crc);
                if (err) {
                        break;
                }

                if (crc != desc.file_crc32) {
                        err = EILSEQ;
                        break;
                }

                if (prev) {
                        bool trunc;
                        err = read_file_descriptor_name(hdl, &entry, &desc, hdl->tmp, sizeof(hdl->tmp), &trunc);
                        if (err) {
                                break;
                        }

                        if (i > 0) {
                                int cmp = strcmp(prev, hdl->tmp);

                                if ((cmp > 0) or ((cmp == 0) and not prev_trunc and not trunc)) {
                                        err = EILSEQ; // wrong order or duplicated name
                                        break;
                                }
                        }

                        strcpy(prev, hdl->tmp);
                        prev_trunc = trunc;
                }
        }

        if (prev) {
                sys_free(cast(void **, &prev));
        }

        return err;
}

//==============================================================================
/**
 * @brief  Function check if container header is coherent.
 *
 * @param  header               header to check
 *
 * @return True if valid, otherwise false.
 */
//==============================================================================
static bool is_container_header_valid(const container_header_t *hdr)
{
        bool valid = false;

        uint32_t crc = calc_CRC32(&hdr->prng_seed,
                sizeof(*hdr) - offsetof(container_header_t, prng_seed) - sizeof(hdr->file_crc32), FPKG_CRC32_INIT);

        if ((hdr->magic == CONTAINER_HEADER_MAGIC) && (hdr->crc32 == crc)) {

                if (hdr->prng_seed != 0) {
                        valid = (hdr->flags & FLAG_ENCRYPTED);
                } else {
                        valid = not(hdr->flags & FLAG_ENCRYPTED);
                }
        }

        return valid;
}

//==============================================================================
/**
 * @brief  Check file entry coherency.
 *
 * @param  hdl                  FS handler
 * @param  file_entry           file entry
 *
 * @return True if valid, otherwise false.
 */
//==============================================================================
static bool is_file_entry_valid(romfs_t *hdl, const file_entry_t *file_entry)
{
        uint32_t crc = calc_CRC32(&file_entry->file_descriptor_address,
                sizeof(*file_entry) - offsetof(file_entry_t, file_descriptor_address), FPKG_CRC32_INIT);

        uint64_t desc_end = cast(uint64_t, file_entry->file_descriptor_address) + sizeof(file_descriptor_t);

        return ((file_entry->magic == FILE_ENTRY_MAGIC) and (file_entry->crc32 == crc) and
                (desc_end <= hdl->entries_offset));
}

//==============================================================================
/**
 * @brief  Check file descriptor coherency (magic, CRC, bounds of name and
 *         content).
 *
 * @param  hdl                  FS handler
 * @param  file_entry           file entry
 * @param  file_desc            file descriptor
 *
 * @return True if valid, otherwise false.
 */
//==============================================================================
static bool is_file_descriptor_valid(romfs_t *hdl, const file_entry_t *file_entry, const file_descriptor_t *file_desc)
{
        uint32_t crc = calc_CRC32(&file_desc->file_name_len,
                sizeof(*file_desc) - offsetof(file_descriptor_t, file_name_len), FPKG_CRC32_INIT);

        uint64_t end = cast(uint64_t, file_entry->file_descriptor_address) + sizeof(file_descriptor_t) +
                file_desc->file_name_len + file_desc->file_size;

        return ((file_desc->magic == FILE_DESCRIPTOR_MAGIC) and (file_desc->crc32 == crc) and
                (file_desc->file_name_len > 0) and (end <= hdl->entries_offset));
}

/*==============================================================================
  End of file
==============================================================================*/
