/*==============================================================================
File    romfs.c

Author  Daniel Zorychta

Brief   ROM File System (FPKG container)

        Copyright (C) 2023 Daniel Zorychta <daniel.zorychta@gmail.com>

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

#pragma GCC optimize("Os")

/*==============================================================================
  Include files
==============================================================================*/
#include "fs/fs.h"
#include <build/vars.h>

/*==============================================================================
  Local macros
==============================================================================*/
#define FPKG_CRC32_INIT        0xFFFFFFFF

#define FILE_DESCRIPTOR_MAGIC  0x43534446
#define FILE_ENTRY_MAGIC       0x544E4546
#define CONTAINER_HEADER_MAGIC 0x474B5046

#define FLAG_ENCRYPTED         (1 << 0)

#define is_encrypted(hdl)      (hdl->flags & FLAG_ENCRYPTED)

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
        uint32_t prng_seed;
        uint32_t number_of_files;
        uint32_t flags;
        uint32_t open_files_ctr;
        uint32_t open_dirs_ctr;
        char tmp[512];
        bool exec_reg_files;
} romfs_t;

struct fpkg_file {
        uint32_t size;
        uint32_t seek;
        uint32_t content_prng_seed;
        uint32_t current_seed;
        uint32_t content_offset;
};

struct fpkg_dir {
        char *dir_root;
        char *item_name;
        uint32_t dir_root_len;
        uint32_t seek;
};

/*==============================================================================
  Local function prototypes
==============================================================================*/
int _romfs_release(void *fs_handle);

static void fpkg__fseek(romfs_t *hdl, struct fpkg_file *file, uint32_t seek);

static int read_file(void *buf, size_t len, kfile_t *file);

static uint32_t prng(uint32_t *seed);

static void xcrypt(void *buf, uint32_t buflen, uint32_t *seed);

static int file_validity_check(romfs_t *hdl, bool full_check);

static bool is_container_header_valid(const container_header_t *hdr);

static bool is_file_entry_valid(const file_entry_t *file_entry);

static bool is_file_descriptor_valid(const file_descriptor_t *file_desc);

static bool is_file_descriptor_name_valid(const file_descriptor_t *file_desc, const char *name);

static int read_container_header(romfs_t *hdl, container_header_t *hdr);

static int read_file_entry(romfs_t *hdl, uint16_t n, file_entry_t *entry);

static int read_file_descriptor(romfs_t *hdl, file_entry_t *entry, file_descriptor_t *desc);

static int read_file_descriptor_name(romfs_t *hdl, file_entry_t *entry, file_descriptor_t *desc, char *buf,
        size_t buf_len);

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
        if (err) {
                _romfs_release(*fs_handle);
                return err;
        }

        err = sys_fopen(src_path, O_RDONLY, 0, &hdl->file);
        if (err) {
                _romfs_release(*fs_handle);
                return err;
        }

        if (sys_stropt_is_flag(opts, "validate")) {
                err = file_validity_check(hdl, true);
                if (err) {
                        _romfs_release(*fs_handle);
                        return err;
                }
        }

        container_header_t header;
        err = read_container_header(hdl, &header);
        if (err) {
                _romfs_release(*fs_handle);
                return err;
        }

        hdl->prng_seed = header.prng_seed;
        hdl->number_of_files = header.number_of_files;
        hdl->flags = header.flags;

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

        if (hdl->open_files_ctr or hdl->open_dirs_ctr) {
                return EBUSY;
        }

        sys_mutex_destroy(hdl->mtx);
        sys_fclose(hdl->file);
        return sys_free(&fs_handle);
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

        if (flags != O_RDONLY) {
                return EROFS;
        }

        *fpos = 0;

        int err = sys_mutex_lock(hdl->mtx, _WAIT_FOREVER_MS);
        if (err) {
                return err;
        }

        for (size_t i = 0; i < hdl->number_of_files; i++) {

                file_entry_t entry;
                err = read_file_entry(hdl, i, &entry);
                if (err) {
                        break;
                }

                file_descriptor_t desc;
                err = read_file_descriptor(hdl, &entry, &desc);
                if (err) {
                        break;
                }

                err = read_file_descriptor_name(hdl, &entry, &desc, hdl->tmp, sizeof(hdl->tmp));
                if (err) {
                        break;
                }

                if (strcmp(hdl->tmp, path) == 0) {

                        struct fpkg_file *file;
                        err = sys_malloc(sizeof(*file), cast(void **, &file));
                        if (err) {
                                break;
                        }

                        file->content_offset = entry.file_descriptor_address + sizeof(desc) +
                                desc.file_name_len;

                        file->content_prng_seed = desc.content_prng_seed;
                        file->current_seed = desc.content_prng_seed;
                        file->size = desc.file_size;
                        file->seek = 0;

                        hdl->open_files_ctr++;

                        *fhdl = file;

                        break;

                } else {
                        err = ENOENT;
                }
        }

        sys_mutex_unlock(hdl->mtx);

        return err;
}

//==============================================================================
/**
 * @brief Close file.
 *
 * @param[in ]          *fs_handle              file system allocated memory
 * @param[in ]          *fhdl                   file handle
 * @param[in ]           force                  force close
 *
 * @return One of errno value (errno.h).
 */
//==============================================================================
API_FS_CLOSE(romfs, void *fs_handle, void *fhdl, bool force)
{
        UNUSED_ARG2(fhdl, force);

        romfs_t *hdl = fs_handle;

        int err = sys_mutex_lock(hdl->mtx, _WAIT_FOREVER_MS);
        if (not err) {
                sys_free(&fhdl);

                hdl->open_files_ctr--;

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

        int err = sys_mutex_lock(hdl->mtx, _WAIT_FOREVER_MS);
        if (not err) {

                fpkg__fseek(hdl, file, *fpos);

                err = sys_fseek(hdl->file, file->content_offset + file->seek, VFS_SEEK_SET);

                if (not err) {
                        size_t len = min((file->size - file->seek), count);
                        if (len > 0) {
                                err = read_file(dst, len, hdl->file);

                                if (not err) {
                                        file->seek += len;
                                        *rdcnt = len;

                                        if (is_encrypted(hdl)) {
                                                xcrypt(dst, len, &file->current_seed);
                                        }
                                }
                        } else {
                                *rdcnt = 0;
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

        int err = sys_mutex_lock(hdl->mtx, _WAIT_FOREVER_MS);
        if (not err) {

                stat->st_ctime = COMPILE_EPOCH_TIME;
                stat->st_mtime = COMPILE_EPOCH_TIME;
                stat->st_dev = 0;
                stat->st_size = file->size;
                stat->st_gid = 0;
                stat->st_uid = 0;
                stat->st_mode = S_IRUSR | S_IRGRP | S_IROTH | (S_IXUSR * hdl->exec_reg_files) | S_IFREG;

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
API_FS_STAT(romfs, void *fs_handle, const char *path, struct stat *stat)
{
        romfs_t *hdl = fs_handle;

        int err = sys_mutex_lock(hdl->mtx, _WAIT_FOREVER_MS);
        if (err) {
                return err;
        }

        stat->st_ctime = COMPILE_EPOCH_TIME;
        stat->st_mtime = COMPILE_EPOCH_TIME;
        stat->st_dev = 0;
        stat->st_gid = 0;
        stat->st_uid = 0;
        stat->st_size = 0;
        stat->st_mode = S_IRUSR | S_IRGRP | S_IROTH;

        for (size_t i = 0; i < hdl->number_of_files; i++) {

                file_entry_t entry;
                err = read_file_entry(hdl, i, &entry);
                if (err) {
                        break;
                }

                file_descriptor_t desc;
                err = read_file_descriptor(hdl, &entry, &desc);
                if (err) {
                        break;
                }

                err = read_file_descriptor_name(hdl, &entry, &desc, hdl->tmp, sizeof(hdl->tmp));
                if (err) {
                        break;
                }

                size_t path_len = strlen(path);

                if (strcmp(hdl->tmp, path) == 0) {

                        stat->st_size = desc.file_size;
                        stat->st_mode |= S_IFREG | (S_IXUSR * hdl->exec_reg_files);

                        break;

                } else if ((strncmp(hdl->tmp, path, path_len) == 0) and(hdl->tmp[path_len] == '/')) {

                        stat->st_mode |= S_IFDIR;

                        break;

                } else {
                        err = ENOENT;
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

        struct stat statbuf;
        int err = sys_fstat(hdl->file, &statbuf);
        if (not err) {
                statfs->f_bsize = 1;
                statfs->f_blocks = statbuf.st_size;
                statfs->f_bfree = 0;
                statfs->f_ffree = 0;
                statfs->f_files = hdl->number_of_files;
                statfs->f_type = SYS_FS_TYPE__SOLID;
                statfs->f_fsname = "romfs";
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

        struct fpkg_dir *dir;
        int err = sys_zalloc(sizeof(*dir), cast(void **, &dir));
        if (err) {
                return err;
        }

        size_t path_len = strlen(path);

        err = sys_strndup(path, path_len + 2, &dir->dir_root);
        if (err) {
                goto error;
        }

        err = sys_strdup("", &dir->item_name);
        if (err) {
                goto error;
        }

        strcpy(dir->dir_root, path);

        if (dir->dir_root[path_len - 1] != '/') {
                dir->dir_root[path_len - 1] = '/';
        }

        dir->dir_root_len = strlen(dir->dir_root);

        err = sys_mutex_lock(hdl->mtx, _WAIT_FOREVER_MS);
        if (not err) {
                for (size_t i = 0; i < hdl->number_of_files; i++) {

                        file_entry_t entry;
                        err = read_file_entry(hdl, i, &entry);
                        if (err) {
                                break;
                        }

                        file_descriptor_t desc;
                        err = read_file_descriptor(hdl, &entry, &desc);
                        if (err) {
                                break;
                        }

                        err = read_file_descriptor_name(hdl, &entry, &desc, hdl->tmp, sizeof(hdl->tmp));
                        if (err) {
                                break;
                        }

                        if (strncmp(hdl->tmp, dir->dir_root, dir->dir_root_len) == 0) {
                                kdir->d_hdl = dir;
                                kdir->d_items = 0; // FIXME
                                kdir->d_seek = 0;

                                hdl->open_dirs_ctr++;
                                break;
                        } else {
                                err = ENOENT;
                        }
                }

                sys_mutex_unlock(hdl->mtx);
        }

error:
        if (err) {
                sys_free(cast(void *, &dir->dir_root));
                sys_free(cast(void *, &dir->item_name));
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

        sys_free(cast(void *, &dir->dir_root));
        sys_free(cast(void *, &dir->item_name));
        sys_free(cast(void *, &dir));

        int err = sys_mutex_lock(hdl->mtx, _WAIT_FOREVER_MS);
        if (not err) {
                hdl->open_dirs_ctr--;
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
                sys_free(cast(void *, &dir->item_name));

                int err = sys_strdup("", &dir->item_name);
                if (err) {
                        return err;
                }

                dir->seek = kdir->d_seek;
        }

        int err = sys_mutex_lock(hdl->mtx, _WAIT_FOREVER_MS);
        if (err) {
                return err;
        }

        err = ENOENT;

        while (dir->seek < hdl->number_of_files) {

                file_entry_t entry;
                err = read_file_entry(hdl, dir->seek, &entry);
                if (err) {
                        break;
                }

                dir->seek++;
                kdir->d_seek = dir->seek;

                file_descriptor_t desc;
                err = read_file_descriptor(hdl, &entry, &desc);
                if (err) {
                        break;
                }

                err = read_file_descriptor_name(hdl, &entry, &desc, hdl->tmp, sizeof(hdl->tmp));
                if (err) {
                        break;
                }

                char *item_name = hdl->tmp + dir->dir_root_len;

                char *slash = strchr(item_name, '/');
                if (slash) {
                        *slash = '\0';
                        kdir->dirent.mode = S_IFDIR;
                        kdir->dirent.size = 0;
                } else {
                        kdir->dirent.mode = S_IFREG | (S_IXUSR * hdl->exec_reg_files);
                        kdir->dirent.size = desc.file_size;
                }

                if ((strncmp(hdl->tmp, dir->dir_root, strlen(dir->dir_root)) == 0)
                                and(strcmp(dir->item_name, item_name) != 0)) {

                        err = sys_free(cast(void *, &dir->item_name));
                        if (err) {
                                break;
                        }

                        err = sys_strdup(item_name, &dir->item_name);
                        if (err) {
                                break;
                        }

                        kdir->dirent.dev = 0;
                        kdir->dirent.mode |= S_IRUSR | S_IRGRP | S_IROTH;
                        kdir->dirent.d_name = dir->item_name;

                        break;
                } else {
                        err = ENOENT;
                }
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
 * @brief  Set file position.
 *
 * @param  hdl          FS handler
 * @param  file         File representation object.
 * @param  seek         position
 */
//==============================================================================
static void fpkg__fseek(romfs_t *hdl, struct fpkg_file *file, uint32_t seek)
{
        uint32_t old_seek = file->seek;
        file->seek = seek;

        if (seek > file->size) {
                file->seek = file->size;
        }

        if (is_encrypted(hdl)) {
                size_t n = 0;

                if (file->seek >= old_seek) {
                        n = file->seek - old_seek;

                } else {
                        file->current_seed = file->content_prng_seed;
                        n = file->seek;
                }

                while (n > 0) {
                        prng(&file->current_seed);
                        n--;
                }
        }
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
        const uint64_t a = 1103515245;
        const uint32_t c = 12345;
        const uint64_t m = (1ULL << 32);

        *seed = (a * *seed + c) % m;

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

        return 0;
}

//==============================================================================
/**
 * @brief  Read container header.
 *
 * @param  self         container instance
 * @param  hdr          header destination buffer
 *
 * @return On success 0 is returned.
 */
//==============================================================================
static int read_container_header(romfs_t *hdl, container_header_t *hdr)
{
        int32_t position = -sizeof(container_header_t);

        int err = sys_fseek(hdl->file, position, VFS_SEEK_END);
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
 * @param  self         container instance
 * @param  n            n-th entry
 * @param  entry        file entry buffer
 *
 * @return On success 0 is returned.
 */
//==============================================================================
static int read_file_entry(romfs_t *hdl, uint16_t n, file_entry_t *entry)
{
        int32_t entry_address = sizeof(container_header_t) + (sizeof(file_entry_t) * hdl->number_of_files) -
                (sizeof(file_entry_t) * n);

        int err = sys_fseek(hdl->file, -entry_address, VFS_SEEK_END);
        if (err) {
                return err;
        }

        sys_clearerr(hdl->file);

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
 * @param  self         container instance
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
 * @brief  Read file name. If buffer is too small them error will be returned.
 *
 * @param  self         container instance
 * @param  entry        file entry
 * @param  desc         descriptor
 * @param  buf          destination buffer
 * @param  buf_len      destination buffer size
 *
 * @return On success 0 is returned.
 */
//==============================================================================
static int read_file_descriptor_name(romfs_t *hdl, file_entry_t *entry, file_descriptor_t *desc, char *buf,
        size_t buf_len)
{
        if (buf_len < 2) {
                return EINVAL;
        }

        if ((buf_len - 1) < desc->file_name_len) {
                return ENOMEM;
        }

        size_t name_len = 0;

        int err = sys_fseek(hdl->file, entry->file_descriptor_address + sizeof(file_descriptor_t),
                VFS_SEEK_SET);
        if (err) {
                return err;
        }

        name_len = min(desc->file_name_len, buf_len - 1);

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
 * @brief  Function check if file is valid.
 *
 * @param  self         container instance
 * @param  full_check   full file check
 *
 * @return On success 0 is returned.
 */
//==============================================================================
static int file_validity_check(romfs_t *hdl, bool full_check)
{
        int err = 0;

        uint32_t buflen = sizeof(hdl->tmp);

        uint8_t *buf = cast(typeof(buf), hdl->tmp);

        while (full_check) {
                uint32_t size = 0;
                uint32_t crc = ~FPKG_CRC32_INIT;

                err = sys_fseek(hdl->file, 0, VFS_SEEK_END);
                if (err) {
                        break;
                }

                i64_t lseek;
                err = sys_ftell(hdl->file, &lseek);
                if (err) {
                        break;
                }

                size = lseek - sizeof(uint32_t);

                err = sys_fseek(hdl->file, 0, VFS_SEEK_SET);
                if (err) {
                        break;
                }

                while (size > 0) {
                        size_t len = min(size, buflen);

                        err = read_file(buf, len, hdl->file);
                        if (not err) {
                                crc = calc_CRC32(buf, len, ~crc);
                                size -= len;

                        } else {
                                break;
                        }
                }

                if (err) {
                        break;
                }

                uint32_t file_crc;
                err = read_file(&file_crc, sizeof(file_crc), hdl->file);
                if (not err) {
                        err = (file_crc != crc) ? EILSEQ : 0;
                }

                break;
        }

        while (not err) {
                container_header_t header;
                err = read_container_header(hdl, &header);
                if (err) {
                        break;
                }

                if (not is_container_header_valid(&header)) {
                        err = EILSEQ;
                        break;
                }

                hdl->prng_seed = header.prng_seed;
                hdl->number_of_files = header.number_of_files;
                hdl->flags = header.flags;

                for (uint16_t i = 0; i < header.number_of_files; i++) {

                        file_entry_t file_entry;
                        err = read_file_entry(hdl, i, &file_entry);
                        if (err) {
                                break;
                        }

                        if (not is_file_entry_valid(&file_entry)) {
                                err = EILSEQ;
                                break;
                        }

                        file_descriptor_t file_desc;
                        err = read_file_descriptor(hdl, &file_entry, &file_desc);
                        if (err) {
                                break;
                        }

                        if (not is_file_descriptor_valid(&file_desc)) {
                                err = EILSEQ;
                                break;
                        }

                        char *buf_name = cast(typeof(buf_name), buf);
                        err = read_file_descriptor_name(hdl, &file_entry, &file_desc, buf_name, buflen);
                        if (err) {
                                break;
                        }

                        if (not is_file_descriptor_name_valid(&file_desc, buf_name)) {
                                err = EILSEQ;
                                break;
                        }
                }

                break;
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
                sizeof(*hdr) - offsetof(container_header_t, prng_seed) - sizeof(hdr->file_crc32),
                FPKG_CRC32_INIT);

        if ((hdr->magic == CONTAINER_HEADER_MAGIC) && (hdr->crc32 == crc)) {

                if (hdr->prng_seed != 0) {
                        valid = (hdr->flags & FLAG_ENCRYPTED);
                } else {
                        valid = true;
                }
        }

        return valid;
}

//==============================================================================
/**
 * @brief  Check file entry coherency.
 *
 * @param  file_entry           file entry
 *
 * @return True if valid, otherwise false.
 */
//==============================================================================
static bool is_file_entry_valid(const file_entry_t *file_entry)
{
        uint32_t crc = calc_CRC32(&file_entry->file_descriptor_address,
                sizeof(*file_entry) - offsetof(file_entry_t, file_descriptor_address), FPKG_CRC32_INIT);

        return ((file_entry->magic == FILE_ENTRY_MAGIC) and(file_entry->crc32 == crc)
                        and(file_entry->file_descriptor_address < cast(uintptr_t, file_entry)));
}

//==============================================================================
/**
 * @brief  Check file descriptor coherency.
 *
 * @param  file_desc_address    file descriptor address
 * @param  buf                  operational buffer for file CRC32 calculation (can be
 * NULL)
 * @param  buflen               buffer length
 *
 * @return True if valid, otherwise false.
 */
//==============================================================================
static bool is_file_descriptor_valid(const file_descriptor_t *file_desc)
{
        uint32_t crc = calc_CRC32(&file_desc->file_name_len,
                sizeof(*file_desc) - offsetof(file_descriptor_t, file_name_len), FPKG_CRC32_INIT);

        return ((file_desc->magic == FILE_DESCRIPTOR_MAGIC) and(file_desc->crc32 == crc));
}

//==============================================================================
/**
 * @brief  Check file descriptor coherency.
 *
 * @param  file_desc_address    file descriptor address
 * @param  buf                  operational buffer for file CRC32 calculation (can be
 * NULL)
 * @param  buflen               buffer length
 *
 * @return True if valid, otherwise false.
 */
//==============================================================================
static bool is_file_descriptor_name_valid(const file_descriptor_t *file_desc, const char *name)
{
        uint32_t crc = calc_CRC32(name, file_desc->file_name_len, FPKG_CRC32_INIT);

        return file_desc->file_name_crc32 == crc;
}

/*==============================================================================
  End of file
==============================================================================*/
