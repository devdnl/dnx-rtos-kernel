/*==============================================================================
@file    ramfs.c

@author  Daniel Zorychta

@brief   This file support RAM file system (grows dynamically in RAM)

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
#include "fs/fs.h"

/*==============================================================================
  Local symbolic constants/macros
==============================================================================*/
#define MTX_TIMEOUT        60000
#define PIPE_LENGTH        __OS_STREAM_BUFFER_LENGTH__
#define PIPE_WRITE_TIMEOUT 1
#define PIPE_READ_TIMEOUT  MAX_DELAY
#define DATA_CHAIN_SIZE    __RAMFS_FILE_CHAIN_SIZE__

/*
 * Memory accounting estimates. RAMFS counts memory of its content (nodes,
 * names, file data, list items, pipes, open file descriptors). Allocations
 * made by other modules (llist, pipe) cannot be measured exactly, so fixed
 * costs are used. Values should be calibrated against the dnx heap manager.
 */
#ifndef RAMFS_ALLOC_ALIGN
#define RAMFS_ALLOC_ALIGN sizeof(size_t) //!< heap block alignment
#endif

#ifndef RAMFS_ALLOC_OVERHEAD
#define RAMFS_ALLOC_OVERHEAD (2 * sizeof(size_t)) //!< heap header per block
#endif

#ifndef RAMFS_LLIST_COST
#define RAMFS_LLIST_COST mem_cost(8 * sizeof(void *)) //!< empty list object
#endif

#ifndef RAMFS_LLIST_ITEM_COST
#define RAMFS_LLIST_ITEM_COST mem_cost(3 * sizeof(void *)) //!< single list item
#endif

#ifndef RAMFS_PIPE_COST
#define RAMFS_PIPE_COST (mem_cost(PIPE_LENGTH) + mem_cost(16 * sizeof(void *)))
#endif

/** cost of single entry on list of opened files */
#define OPENED_FILE_COST (mem_cost(sizeof(struct opened_file_info)) + RAMFS_LLIST_ITEM_COST)

#ifndef RAMFS_AVG_NAME_LEN
#define RAMFS_AVG_NAME_LEN 16 //!< used to estimate f_ffree
#endif

/*==============================================================================
  Local types, enums definitions
==============================================================================*/
/** regular file data chain */
typedef struct data_chain {
        struct data_chain *next;
        u8_t buf[DATA_CHAIN_SIZE];
} data_chain_t;

/** node structure */
typedef struct node {
        char *name;        //!< file name
        mode_t mode;       //!< protection
        uid_t uid;         //!< user ID of owner
        gid_t gid;         //!< group ID of owner
        size_t size;       //!< file size
        time_t mtime;      //!< time of last modification
        time_t ctime;      //!< time of creation
        u32_t opendir_cnt; //!< number of open directory handles (dirs only)

        union {
                pipe_t *pipe_t;
                llist_t *llist_t;
                data_chain_t *data_chain_t;
                dev_t dev_t;
        } data;
} node_t;

/** info of opened file */
struct opened_file_info {
        node_t *child;        //!< opened node
        node_t *parent;       //!< base of opened node
        bool remove_at_close; //!< file to remove after close
};

/** main memory structure */
struct RAMFS {
        node_t root_dir;        //!< root dir '/'
        kmtx_t *resource_mtx;   //!< lock mutex
        llist_t *opended_files; //!< list with opened files
        size_t file_count;      //!< number of files
        u32_t opendir_count;    //!< number of all open directory handles
        size_t used_mem;        //!< memory used by file system content [B]
        size_t limit;           //!< size limit from "size=" option (0 = no limit) [B]
};

/*==============================================================================
  Local function prototypes
==============================================================================*/
static int new_node(struct RAMFS *hdl, node_t *parent, char *filename, mode_t mode, i32_t *item, node_t **child);
static int delete_node(struct RAMFS *hdl, node_t *base, node_t *target, u32_t position);
static int get_node(const char *path, node_t *startnode, i32_t deep, i32_t *item, node_t **node);
static uint get_path_deep(const char *path);
static int add_node_to_open_files_list(struct RAMFS *hdl, node_t *parent, node_t *child);
static void clear_regular_file(struct RAMFS *hdl, node_t *node);
static int write_regular_file(struct RAMFS *hdl, node_t *node, const u8_t *src, size_t count, fpos_t fpos,
        size_t *wrcnt);
static int read_regular_file(node_t *node, u8_t *dst, size_t count, fpos_t fpos, size_t *rdcnt);
static int copy_basename(struct RAMFS *hdl, const char *path, char **name);
static int find_child_index(node_t *parent, node_t *child);
static int release_tree(struct RAMFS *hdl);
static size_t mem_cost(size_t size);
static int mem_reserve(struct RAMFS *hdl, size_t cost);
static void mem_release(struct RAMFS *hdl, size_t cost);
static int mem_zalloc(struct RAMFS *hdl, size_t size, void **mem);
static void mem_free(struct RAMFS *hdl, void **mem, size_t size);
static void free_name(struct RAMFS *hdl, char **name);
static int parse_options(const char *opts, size_t *limit);

/*==============================================================================
  Local object definitions
==============================================================================*/

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
 * Supported options (comma separated):
 *      size=<n>[K|M]   limit of file system content size in bytes/KiB/MiB
 *      size=<n>%       limit as percent of total system memory
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
API_FS_INIT(ramfs, void **fs_handle, const char *src_path, const char *opts)
{
        UNUSED_ARG1(src_path);

        size_t limit = 0;
        int err = parse_options(opts, &limit);
        if (err) {
                return err;
        }

        err = sys_zalloc(sizeof(struct RAMFS), fs_handle);
        if (!err) {
                struct RAMFS *hdl = *fs_handle;
                hdl->limit = limit;

                err = sys_mutex_create(KMTX_TYPE_RECURSIVE, &hdl->resource_mtx);
                if (err) {
                        goto finish;
                }

                err = sys_llist_create(NULL, NULL, &hdl->root_dir.data.llist_t);
                if (err) {
                        goto finish;
                }

                err = sys_llist_create(sys_llist_functor_cmp_pointers, NULL, &hdl->opended_files);
                if (err) {
                        goto finish;
                }

                hdl->root_dir.name = "/";
                hdl->root_dir.size = sizeof(node_t);
                hdl->root_dir.mode = (S_IRWXU | S_IRWXG | S_IRWXO) | S_IFDIR;

        finish:
                if (err) {
                        if (hdl->resource_mtx) {
                                sys_mutex_destroy(hdl->resource_mtx);
                        }

                        if (hdl->root_dir.data.llist_t) {
                                sys_llist_destroy(hdl->root_dir.data.llist_t);
                        }

                        if (hdl->opended_files) {
                                sys_llist_destroy(hdl->opended_files);
                        }

                        sys_free(fs_handle);
                }
        }

        return err;
}

//==============================================================================
/**
 * @brief Release file system
 *
 * Function releases all nodes (files, directories, pipes, device nodes),
 * file data, internal lists, mutex and the file system handle itself.
 * Release is refused when any file or directory is still open.
 *
 * @note Device nodes are only removed from the tree; drivers are not released.
 *
 * @param[in ]          *fs_handle              file system allocated memory
 *
 * @retval EBUSY        file or directory is still open
 * @return One of errno value (errno.h)
 */
//==============================================================================
API_FS_RELEASE(ramfs, void *fs_handle)
{
        struct RAMFS *hdl = fs_handle;

        int err = sys_mutex_lock(hdl->resource_mtx, MTX_TIMEOUT);
        if (err) {
                return err;
        }

        /* file system in use */
        if (sys_llist_size(hdl->opended_files) > 0 || hdl->opendir_count > 0) {
                sys_mutex_unlock(hdl->resource_mtx);
                return EBUSY;
        }

        /* remove all nodes except root */
        err = release_tree(hdl);
        if (err) {
                sys_mutex_unlock(hdl->resource_mtx);
                return err;
        }

        sys_llist_destroy(hdl->root_dir.data.llist_t);
        hdl->root_dir.data.llist_t = NULL;

        sys_llist_destroy(hdl->opended_files);
        hdl->opended_files = NULL;

        /* mutex cannot be destroyed while locked */
        kmtx_t *mtx = hdl->resource_mtx;
        hdl->resource_mtx = NULL;
        sys_mutex_unlock(mtx);
        sys_mutex_destroy(mtx);

        sys_free(cast(void **, &hdl));

        return ESUCC;
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
API_FS_MKNOD(ramfs, void *fs_handle, const char *path, const dev_t dev)
{
        struct RAMFS *hdl = fs_handle;

        /* trailing slash is allowed only for directories */
        if (!path || path[0] == '\0' || strlch(path) == '/') {
                return EINVAL;
        }

        int err = sys_mutex_lock(hdl->resource_mtx, MTX_TIMEOUT);
        if (!err) {

                // parent node must exist
                node_t *parent;
                err = get_node(path, &hdl->root_dir, -1, NULL, &parent);
                if (!err) {
                        // create new node
                        char *child_name;
                        err = copy_basename(hdl, path, &child_name);
                        if (!err) {
                                node_t *child;
                                err = new_node(hdl, parent, child_name, S_IRWXU | S_IRGRP | S_IROTH | S_IFDEV, NULL,
                                        &child);
                                if (!err) {
                                        child->data.dev_t = dev;
                                } else {
                                        free_name(hdl, &child_name);
                                }
                        }
                }

                sys_mutex_unlock(hdl->resource_mtx);
        }

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
API_FS_MKDIR(ramfs, void *fs_handle, const char *path, mode_t mode)
{
        struct RAMFS *hdl = fs_handle;

        if (isstreq(path, "/")) {
                return EEXIST;
        }

        int err = sys_mutex_lock(hdl->resource_mtx, MTX_TIMEOUT);
        if (!err) {

                // parent node must exist
                node_t *parent;
                err = get_node(path, &hdl->root_dir, -1, NULL, &parent);
                if (!err) {
                        // create new node
                        char *child_name;
                        err = copy_basename(hdl, path, &child_name);
                        if (!err) {
                                node_t *child;
                                err = new_node(hdl, parent, child_name, (mode & S_IPMT) | S_IFDIR, NULL, &child);
                                if (err) {
                                        free_name(hdl, &child_name);
                                }
                        }
                }

                sys_mutex_unlock(hdl->resource_mtx);
        }

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
API_FS_MKFIFO(ramfs, void *fs_handle, const char *path, mode_t mode)
{
        struct RAMFS *hdl = fs_handle;

        /* trailing slash is allowed only for directories */
        if (!path || path[0] == '\0' || strlch(path) == '/') {
                return EINVAL;
        }

        int err = sys_mutex_lock(hdl->resource_mtx, MTX_TIMEOUT);
        if (!err) {

                // parent node must exist
                node_t *parent;
                err = get_node(path, &hdl->root_dir, -1, NULL, &parent);
                if (!err) {
                        // create new node
                        char *child_name;
                        err = copy_basename(hdl, path, &child_name);
                        if (!err) {
                                node_t *child;
                                err = new_node(hdl, parent, child_name, (mode & S_IPMT) | S_IFIFO, NULL, &child);
                                if (err) {
                                        free_name(hdl, &child_name);
                                }
                        }
                }

                sys_mutex_unlock(hdl->resource_mtx);
        }

        return err;
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
API_FS_OPENDIR(ramfs, void *fs_handle, const char *path, kdir_t *dir)
{
        struct RAMFS *hdl = fs_handle;

        int err = sys_mutex_lock(hdl->resource_mtx, MTX_TIMEOUT);
        if (!err) {

                node_t *parent;
                err = get_node(path, &hdl->root_dir, 0, NULL, &parent);
                if (!err) {
                        if (S_ISDIR(parent->mode)) {
                                dir->d_items = sys_llist_size(parent->data.llist_t);
                                dir->d_seek = 0;
                                dir->d_hdl = parent;
                                parent->opendir_cnt++; // protect against removal while open
                                hdl->opendir_count++;  // protect against release while open
                        } else {
                                err = ENOTDIR;
                        }
                }

                sys_mutex_unlock(hdl->resource_mtx);
        }

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
API_FS_CLOSEDIR(ramfs, void *fs_handle, kdir_t *dir)
{
        struct RAMFS *hdl = fs_handle;

        int err = sys_mutex_lock(hdl->resource_mtx, MTX_TIMEOUT);
        if (!err) {
                node_t *node = dir->d_hdl;
                if (node && node->opendir_cnt > 0) {
                        node->opendir_cnt--;

                        if (hdl->opendir_count > 0) {
                                hdl->opendir_count--;
                        }
                }

                dir->d_hdl = NULL;

                sys_mutex_unlock(hdl->resource_mtx);
        }

        return err;
}

//==============================================================================
/**
 * @brief Read directory
 *
 * @param[in ]          *fs_handle              file system allocated memory
 * @param[in ]          *dir                    directory object
 * @param[out]          **dirent                directory entry
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
API_FS_READDIR(ramfs, void *fs_handle, kdir_t *dir)
{
        struct RAMFS *hdl = fs_handle;

        int err = sys_mutex_lock(hdl->resource_mtx, MTX_TIMEOUT);
        if (!err) {

                node_t *parent = dir->d_hdl;
                node_t *child = sys_llist_at(parent->data.llist_t, dir->d_seek++);

                if (child) {
                        dir->dirent.d_name = child->name;
                        dir->dirent.mode = child->mode;
                        dir->dirent.size = child->size;

                        if (S_ISDEV(child->mode)) {
                                /* copy data before unlock: child can be removed meanwhile */
                                dev_t dev = child->data.dev_t;
                                dir->dirent.dev = dev;

                                sys_mutex_unlock(hdl->resource_mtx);

                                struct vfs_dev_stat dev_stat;
                                err = sys_driver_stat(dev, &dev_stat);
                                if (!err) {
                                        dir->dirent.size = dev_stat.st_size;
                                } else if (err == ENODEV) {
                                        dir->dirent.size = 0;
                                        err = ESUCC;
                                }

                                return err;
                        } else {
                                err = ESUCC;
                        }
                } else {
                        err = ENOENT;
                }

                sys_mutex_unlock(hdl->resource_mtx);
        }

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
API_FS_REMOVE(ramfs, void *fs_handle, const char *path)
{
        struct RAMFS *hdl = fs_handle;

        int err = sys_mutex_lock(hdl->resource_mtx, MTX_TIMEOUT);
        if (!err) {

                node_t *parent;
                err = get_node(path, &hdl->root_dir, -1, NULL, &parent);
                if (err) {
                        goto finish;
                }

                i32_t item;
                node_t *child;
                err = get_node(path, &hdl->root_dir, 0, &item, &child);
                if (err) {
                        goto finish;
                }

                if (child == &hdl->root_dir) {
                        err = EPERM;
                        goto finish;
                }

                /* if path is ending on slash, the object must be DIR */
                if (strlch(path) == '/' && !S_ISDIR(child->mode)) {
                        err = ENOTDIR;
                        goto finish;
                }

                bool remove_file = true;

                /* check if file is opened */
                if (!S_ISDIR(child->mode)) {
                        sys_llist_foreach (struct opened_file_info *, opened_file, hdl->opended_files) {
                                if (opened_file->child == child) {
                                        opened_file->remove_at_close = true;
                                        remove_file = false;
                                }
                        }
                }

                /* remove node if possible */
                if (remove_file == true) {
                        err = delete_node(hdl, parent, child, item);
                } else {
                        err = ESUCC;
                }

        finish:
                sys_mutex_unlock(hdl->resource_mtx);
        }

        return err;
}

//==============================================================================
/**
 * @brief Rename (move) file/directory
 *
 * Object can be moved to other directory of the same file system. Node is
 * relinked (not copied), so open file handles and open directory handles stay
 * valid.
 *
 * @param[in ]          *fs_handle              file system allocated memory
 * @param[in ]          *old_name               old object name
 * @param[in ]          *new_name               new object name
 *
 * @retval EEXIST       object new_name already exists
 * @retval EINVAL       directory would be moved into itself or its subdirectory
 * @retval ENOTDIR      parent of new_name is not a directory
 * @return One of errno value (errno.h)
 */
//==============================================================================
API_FS_RENAME(ramfs, void *fs_handle, const char *old_name, const char *new_name)
{
        struct RAMFS *hdl = fs_handle;

        if (strlch(old_name) == '/' || strlch(new_name) == '/') {
                return EISDIR;
        }

        int err = sys_mutex_lock(hdl->resource_mtx, MTX_TIMEOUT);
        if (!err) {

                node_t *old_parent, *new_parent, *target;
                i32_t item;

                err = get_node(old_name, &hdl->root_dir, -1, NULL, &old_parent);
                if (err) {
                        goto finish;
                }

                err = get_node(old_name, &hdl->root_dir, 0, &item, &target);
                if (err) {
                        goto finish;
                }

                if (target == &hdl->root_dir) {
                        err = EPERM;
                        goto finish;
                }

                err = get_node(new_name, &hdl->root_dir, -1, NULL, &new_parent);
                if (err) {
                        goto finish;
                }

                if (!S_ISDIR(new_parent->mode)) {
                        err = ENOTDIR;
                        goto finish;
                }

                node_t *existing;
                err = get_node(new_name, &hdl->root_dir, 0, NULL, &existing);
                if (err == ESUCC) {
                        /* renaming to the same object is a no-op */
                        err = (existing == target) ? ESUCC : EEXIST;
                        goto finish;
                } else if (err != ENOENT) {
                        goto finish;
                }

                /* directory cannot be moved into itself or its subdirectory */
                if (S_ISDIR(target->mode) && old_parent != new_parent) {
                        int path_deep = get_path_deep(new_name);
                        for (int up = 1; up <= path_deep; up++) {
                                node_t *ancestor;
                                if (get_node(new_name, &hdl->root_dir, -up, NULL, &ancestor) == ESUCC &&
                                        ancestor == target) {
                                        err = EINVAL;
                                        goto finish;
                                }
                        }
                }

                char *newname;
                err = copy_basename(hdl, new_name, &newname);
                if (err) {
                        goto finish;
                }

                if (old_parent != new_parent) {
                        /* link in new directory first, then unlink from old one */
                        if (!sys_llist_push_back(new_parent->data.llist_t, target)) {
                                free_name(hdl, &newname);
                                err = ENOMEM;
                                goto finish;
                        }

                        /* take does not destroy object (in contrast to erase) */
                        sys_llist_take(old_parent->data.llist_t, item);

                        /* open handles keep parent to locate node at close */
                        sys_llist_foreach (struct opened_file_info *, file, hdl->opended_files) {
                                if (file->child == target) {
                                        file->parent = new_parent;
                                }
                        }
                }

                free_name(hdl, &target->name);
                target->name = newname;

        finish:
                sys_mutex_unlock(hdl->resource_mtx);
        }

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
API_FS_CHMOD(ramfs, void *fs_handle, const char *path, mode_t mode)
{
        struct RAMFS *hdl = fs_handle;

        int err = sys_mutex_lock(hdl->resource_mtx, MTX_TIMEOUT);
        if (!err) {

                node_t *target;
                err = get_node(path, &hdl->root_dir, 0, NULL, &target);
                if (!err) {
                        target->mode = (target->mode & S_IFMT) | (mode & S_IPMT);
                }

                sys_mutex_unlock(hdl->resource_mtx);
        }

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
API_FS_CHOWN(ramfs, void *fs_handle, const char *path, uid_t owner, gid_t group)
{
        struct RAMFS *hdl = fs_handle;

        int err = sys_mutex_lock(hdl->resource_mtx, MTX_TIMEOUT);
        if (!err) {

                node_t *target;
                err = get_node(path, &hdl->root_dir, 0, NULL, &target);
                if (!err) {
                        target->uid = owner;
                        target->gid = group;
                }

                sys_mutex_unlock(hdl->resource_mtx);
        }

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
API_FS_FSTAT(ramfs, void *fs_handle, void *extra, struct stat *stat)
{
        UNUSED_ARG1(fs_handle);

        int err = ENOENT;

        struct opened_file_info *opened_file = extra;
        if (opened_file && opened_file->child) {

                stat->st_gid = opened_file->child->gid;
                stat->st_mode = opened_file->child->mode;
                stat->st_mtime = opened_file->child->mtime;
                stat->st_ctime = opened_file->child->ctime;
                stat->st_size = opened_file->child->size;
                stat->st_uid = opened_file->child->uid;

                if (S_ISDEV(opened_file->child->mode)) {

                        stat->st_dev = opened_file->child->data.dev_t;

                        struct vfs_dev_stat dev_stat;
                        err = sys_driver_stat(opened_file->child->data.dev_t, &dev_stat);
                        if (!err) {
                                stat->st_size = dev_stat.st_size;

                        } else if (err == ENODEV) {
                                stat->st_size = 0;
                                err = ESUCC;
                        }

                } else {
                        stat->st_dev = 0;
                        err = ESUCC;
                }
        }

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
API_FS_STAT(ramfs, void *fs_handle, const char *path, struct stat *stat)
{
        struct RAMFS *hdl = fs_handle;

        int err = sys_mutex_lock(hdl->resource_mtx, MTX_TIMEOUT);
        if (!err) {

                node_t *target;
                err = get_node(path, &hdl->root_dir, 0, NULL, &target);
                if (!err) {
                        if ((strlch(path) == '/' && S_ISDIR(target->mode)) || strlch(path) != '/') {

                                struct opened_file_info file;
                                file.child = target;
                                file.parent = NULL;
                                file.remove_at_close = false;

                                err = _ramfs_fstat(fs_handle, &file, stat);
                        } else {
                                err = EINVAL;
                        }
                }

                sys_mutex_unlock(hdl->resource_mtx);
        }

        return err;
}

//==============================================================================
/**
 * @brief Return file system status
 *
 * Used space is the memory allocated by this RAMFS instance (f_blocks - f_bfree).
 * Without size limit the capacity follows the free system memory:
 *      f_blocks = used + free memory, f_bfree = free memory.
 * With size limit:
 *      f_blocks = limit, f_bfree = min(limit - used, free memory).
 *
 * @param[in ]          *fs_handle              file system allocated memory
 * @param[out]          *statfs                 file system status
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
API_FS_STATFS(ramfs, void *fs_handle, struct statfs *statfs)
{
        struct RAMFS *hdl = fs_handle;

        int err = sys_mutex_lock(hdl->resource_mtx, MTX_TIMEOUT);
        if (err) {
                return err;
        }

        size_t used = hdl->used_mem;
        size_t limit = hdl->limit;
        size_t files = hdl->file_count;

        sys_mutex_unlock(hdl->resource_mtx);

        size_t free_mem = sys_get_free_mem();
        size_t blocks, bfree;

        if (limit) {
                blocks = limit;
                bfree = min(limit - min(used, limit), free_mem);
        } else {
                blocks = used + free_mem;
                bfree = free_mem;
        }

        /* estimated cost of a new file: node, name and parent's list item */
        size_t file_cost = mem_cost(sizeof(node_t)) + mem_cost(RAMFS_AVG_NAME_LEN + 1) + RAMFS_LLIST_ITEM_COST;

        statfs->f_bsize = 1;
        statfs->f_blocks = cast(u32_t, blocks);
        statfs->f_bfree = cast(u32_t, bfree);
        statfs->f_ffree = cast(u32_t, bfree / file_cost);
        statfs->f_files = cast(u32_t, files);
        statfs->f_type = SYS_FS_TYPE__RAM;
        statfs->f_fsname = "ramfs";

        return ESUCC;
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
API_FS_OPEN(ramfs, void *fs_handle, void **fhdl, fpos_t *fpos, const char *path, u32_t flags)
{
        struct RAMFS *hdl = fs_handle;

        int err = sys_mutex_lock(hdl->resource_mtx, MTX_TIMEOUT);
        if (!err) {

                // open file parent
                node_t *parent;
                err = get_node(path, &hdl->root_dir, -1, NULL, &parent);
                if (err) {
                        goto finish;
                }

                // try to open selected file, if not exist then try create if O_CREAT flag is set
                i32_t item;
                node_t *child;
                bool created = false;
                err = get_node(path, &hdl->root_dir, 0, &item, &child);
                if (err == ENOENT) {
                        // check that file should be created
                        if (!(flags & O_CREAT)) {
                                goto finish;
                        }

                        /* "path/" names a directory, regular file cannot be created */
                        if (strlch(path) == '/') {
                                err = EISDIR;
                                goto finish;
                        }

                        char *file_name;
                        err = copy_basename(hdl, path, &file_name);
                        if (err) {
                                goto finish;
                        }

                        err = new_node(hdl, parent, file_name, 0666 | S_IFREG, &item, &child);
                        if (err) {
                                free_name(hdl, &file_name);
                                goto finish;
                        }

                        created = true;

                } else if (err) {
                        goto finish;

                } else {
                        if ((flags & O_CREAT) && (flags & O_EXCL)) {
                                err = EEXIST;
                                goto finish;
                        }

                        if (S_ISDIR(child->mode)) {
                                err = EISDIR;
                                goto finish;
                        }
                }

                /* add file to list of open files */
                err = add_node_to_open_files_list(hdl, parent, child);
                if (err) {
                        /* do not leave an empty, just created file behind */
                        if (created) {
                                delete_node(hdl, parent, child, item);
                        }
                        goto finish;
                }

                // load pointer to file descriptor
                *fhdl = sys_llist_back(hdl->opended_files);

                /* set file parameters */
                if (S_ISREG(child->mode)) {
                        // truncate file if requested
                        if (flags & O_TRUNC) {
                                clear_regular_file(hdl, child);
                        }

                        if (flags & O_APPEND) {
                                *fpos = child->size;
                        } else {
                                *fpos = 0;
                        }

                } else if (S_ISDEV(child->mode)) {
                        err = sys_driver_open(child->data.dev_t, flags);
                        if (!err) {
                                *fpos = 0;
                        } else {
                                int pos = sys_llist_find_end(hdl->opended_files, *fhdl);
                                sys_llist_erase(hdl->opended_files, pos);
                                mem_release(hdl, OPENED_FILE_COST);
                                *fhdl = NULL;
                        }
                }

        finish:
                sys_mutex_unlock(hdl->resource_mtx);
        }

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
API_FS_CLOSE(ramfs, void *fs_handle, void *fhdl)
{
        struct RAMFS *hdl = fs_handle;

        int err = sys_mutex_lock(hdl->resource_mtx, MTX_TIMEOUT);
        if (!err) {

                struct opened_file_info *opened_file = fhdl;
                int pos = sys_llist_find_begin(hdl->opended_files, opened_file);

                if (opened_file && opened_file->child && pos >= 0) {
                        node_t *target = opened_file->child;

                        /* close device if file is driver type */
                        if (S_ISDEV(target->mode)) {
                                err = sys_driver_close(target->data.dev_t, true);

                        } else if (S_ISFIFO(target->mode)) {
                                err = sys_pipe_close(target->data.pipe_t);

                        } else {
                                err = ESUCC;
                        }

                        /* file to remove (any type), check that no other handle still uses it */
                        if (opened_file->remove_at_close) {
                                bool remove = true;

                                sys_llist_foreach (struct opened_file_info *, file, hdl->opended_files) {
                                        if (file != opened_file && file->child == target) {
                                                remove = false;
                                                break;
                                        }
                                }

                                if (remove) {
                                        /* position of node in PARENT list, not in opened files list */
                                        int item = find_child_index(opened_file->parent, target);
                                        if (item >= 0) {
                                                delete_node(hdl, opened_file->parent, target, item);
                                        }
                                }
                        }

                        /* remove file from list (opened_file is freed here) */
                        sys_llist_erase(hdl->opended_files, pos);
                        mem_release(hdl, OPENED_FILE_COST);
                } else {
                        err = ENOENT;
                }

                sys_mutex_unlock(hdl->resource_mtx);
        }

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
API_FS_WRITE(ramfs, void *fs_handle, void *fhdl, const u8_t *src, size_t count, fpos_t *fpos, size_t *wrcnt,
        struct vfs_fattr fattr)
{
        struct RAMFS *hdl = fs_handle;

        int err = ENOENT;

        struct opened_file_info *opened_file = fhdl;
        if (opened_file && opened_file->child) {
                node_t *node = opened_file->child;

                sys_gettime(&node->mtime);

                if (S_ISDEV(node->mode)) {
                        dev_t dev = node->data.dev_t;
                        err = sys_driver_write(dev, src, count, fpos, wrcnt, fattr);

                } else if (S_ISFIFO(node->mode)) {
                        pipe_t *pipe = node->data.pipe_t;

                        err = sys_pipe_write(pipe, src, count, wrcnt, fattr.non_blocking_wr);
                        if (!err) {
                                if (*wrcnt > 0) {
                                        size_t size = 0;
                                        err = sys_pipe_get_length(pipe, &size);
                                        node->size = size;
                                }
                        }

                } else if (S_ISREG(node->mode)) {
                        err = sys_mutex_lock(hdl->resource_mtx, MTX_TIMEOUT);
                        if (!err) {
                                err = write_regular_file(hdl, node, src, count, *fpos, wrcnt);
                                sys_mutex_unlock(hdl->resource_mtx);
                        }
                }
        }

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
API_FS_READ(ramfs, void *fs_handle, void *fhdl, u8_t *dst, size_t count, fpos_t *fpos, size_t *rdcnt,
        struct vfs_fattr fattr)
{
        struct RAMFS *hdl = fs_handle;

        int err = ENOENT;

        struct opened_file_info *opened_file = fhdl;
        if (opened_file && opened_file->child) {
                node_t *node = opened_file->child;

                if (S_ISDEV(node->mode)) {
                        dev_t dev = node->data.dev_t;
                        err = sys_driver_read(dev, dst, count, fpos, rdcnt, fattr);

                } else if (S_ISFIFO(node->mode)) {
                        pipe_t *pipe = node->data.pipe_t;

                        err = sys_pipe_read(pipe, dst, count, rdcnt, fattr.non_blocking_rd or fattr.non_strict_rd);
                        if (!err) {
                                if (*rdcnt > 0) {
                                        size_t size;
                                        err = sys_pipe_get_length(pipe, &size);
                                        node->size = size;
                                }
                        }

                } else if (S_ISREG(node->mode)) {
                        err = sys_mutex_lock(hdl->resource_mtx, MTX_TIMEOUT);
                        if (!err) {
                                err = read_regular_file(node, dst, count, *fpos, rdcnt);
                                sys_mutex_unlock(hdl->resource_mtx);
                        }
                }
        }

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
API_FS_IOCTL(ramfs, void *fs_handle, void *fhdl, int request, void *arg)
{
        UNUSED_ARG1(fs_handle);

        int err = ENOENT;

        struct opened_file_info *opened_file = fhdl;
        if (opened_file && opened_file->child) {
                if (S_ISDEV(opened_file->child->mode)) {
                        dev_t dev = opened_file->child->data.dev_t;
                        err = sys_driver_ioctl(dev, request, arg);

                } else if (S_ISFIFO(opened_file->child->mode)) {

                        switch (request) {
                        case IOCTL_PIPE__CLOSE: {
                                pipe_t *pipe = opened_file->child->data.pipe_t;
                                err = sys_pipe_close(pipe);
                                break;
                        }

                        case IOCTL_PIPE__CLEAR: {
                                pipe_t *pipe = opened_file->child->data.pipe_t;
                                err = sys_pipe_clear(pipe);
                                break;
                        }

                        case IOCTL_PIPE__PERMANENT: {
                                pipe_t *pipe = opened_file->child->data.pipe_t;
                                err = sys_pipe_permanent(pipe);
                                break;
                        }

                        default:
                                err = EBADRQC;
                                break;
                        }

                } else {
                        err = EBADRQC; // file exists, request not supported for this type
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
API_FS_FLUSH(ramfs, void *fs_handle, void *fhdl)
{
        struct RAMFS *hdl = fs_handle;

        int err = sys_mutex_lock(hdl->resource_mtx, MTX_TIMEOUT);
        if (!err) {

                struct opened_file_info *opened_file = fhdl;
                if (opened_file && opened_file->child) {
                        if (S_ISDEV(opened_file->child->mode)) {
                                sys_mutex_unlock(hdl->resource_mtx);
                                return sys_driver_flush(opened_file->child->data.dev_t);
                        } else {
                                err = ESUCC;
                        }
                } else {
                        err = ENOENT;
                }

                sys_mutex_unlock(hdl->resource_mtx);
        }

        return err;
}

//==============================================================================
/**
 * @brief Synchronize all buffers to a medium
 *
 * @param[in ]          *fs_handle              file system allocated memory
 *
 * @return None
 */
//==============================================================================
API_FS_SYNC(ramfs, void *fs_handle)
{
        UNUSED_ARG1(fs_handle);
        return ESUCC;
}

//==============================================================================
/**
 * @brief Remove selected node
 *
 * @param[in] *base             base node
 * @param[in] *target           target node
 * @param[in]  position         item in base node that point to target
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
static int delete_node(struct RAMFS *hdl, node_t *base, node_t *target, u32_t position)
{
        if (S_ISDIR(target->mode)) {
                if (sys_llist_size(target->data.llist_t) > 0) {
                        return ENOTEMPTY;
                } else if (target->opendir_cnt > 0) {
                        return EBUSY;
                } else {
                        sys_llist_destroy(target->data.llist_t);
                        target->data.llist_t = NULL;
                        mem_release(hdl, RAMFS_LLIST_COST);
                }

        } else if (S_ISFIFO(target->mode)) {
                if (target->data.pipe_t) {
                        sys_pipe_destroy(target->data.pipe_t);
                        target->data.pipe_t = NULL;
                        mem_release(hdl, RAMFS_PIPE_COST);
                }

        } else if (S_ISREG(target->mode)) {
                clear_regular_file(hdl, target);
        }

        free_name(hdl, &target->name);

        /* list destructor frees the node itself */
        sys_llist_erase(base->data.llist_t, position);
        mem_release(hdl, mem_cost(sizeof(node_t)) + RAMFS_LLIST_ITEM_COST);

        hdl->file_count--;

        return ESUCC;
}

//==============================================================================
/**
 * @brief Check path deep
 *
 * @param[in] *path             path
 *
 * @return path deep
 */
//==============================================================================
static uint get_path_deep(const char *path)
{
        uint deep = 0;

        if (strfch(path) == '/') {
                const char *last_path = path++;

                while ((path = strchr(path, '/'))) {
                        last_path = path;
                        path++;
                        deep++;
                }

                if (last_path[1] != '\0') {
                        deep++;
                }
        }

        return deep;
}

//==============================================================================
/**
 * @brief Function find node by path
 *
 * @param[in]  path             path
 * @param[in]  startnode        start node
 * @param[out] extPath          external path begin (pointer from path)
 * @param[in]  deep             deep control
 * @param[out] item             node is n-item of list which was found
 * @param[out] node             found node
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
static int get_node(const char *path, node_t *startnode, i32_t deep, i32_t *item, node_t **node)
{
        if (!path || !startnode) {
                return ENOENT;
        }

        if (!S_ISDIR(startnode->mode)) {
                return ENOTDIR;
        }

        node_t *current_node = startnode;
        int dir_deep = get_path_deep(path);
        int err = ENOENT;

        /* go to selected node -----------------------------------------------*/
        while (dir_deep + deep > 0) {
                /* get element from path */
                if ((path = strchr(path, '/')) == NULL) {
                        break;
                } else {
                        path++;
                }

                char *path_end = strchr(path, '/');
                uint path_length = !path_end ? strlen(path) : (size_t)path_end - (size_t)path;

                /* only directory can be traversed (data union holds llist only for dirs) */
                if (!S_ISDIR(current_node->mode)) {
                        return ENOTDIR;
                }

                /* get number of list items */
                int list_size = sys_llist_size(current_node->data.llist_t);

                /* find that object exist ------------------------------------*/
                int i = 0;
                while (list_size > 0) {
                        node_t *next_node = sys_llist_at(current_node->data.llist_t, i++);

                        if (next_node == NULL) {
                                /* list inconsistency: treat as not found */
                                list_size = 0;
                                break;
                        }

                        if (strlen(next_node->name) == path_length &&
                                strncmp(next_node->name, path, path_length) == 0) {

                                current_node = next_node;

                                if (item) {
                                        *item = i - 1;
                                }

                                break;
                        }

                        list_size--;
                }

                /* directory does not found or error */
                if (list_size == 0 || current_node == NULL) {
                        current_node = NULL;
                        break;
                }

                dir_deep--;
        }

        if (current_node) {
                *node = current_node;
                err = ESUCC;
        }

        return err;
}

//==============================================================================
/**
 * @brief Function create new file in selected node
 * Function allocate new node. If node is created successfully then filename
 * cannot be freed!
 *
 * @param[in]  parent           parent node
 * @param[in]  filename         filename (must be earlier allocated)
 * @param[in]  mode             mode (permissions, file type)
 * @param[out] item             new node number in base node (can be NULL)
 * @param[out] child            new node
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
static int new_node(struct RAMFS *hdl, node_t *parent, char *filename, mode_t mode, i32_t *item, node_t **child)
{
        if (!parent || !filename) {
                return EINVAL;
        }

        if (!S_ISDIR(parent->mode)) {
                return ENOTDIR;
        }

        sys_llist_foreach (node_t *, node, parent->data.llist_t) {
                if (strcmp(node->name, filename) == 0) {
                        return EEXIST;
                }
        }

        /* reserve memory for node, parent's list item and node's own object */
        size_t cost = mem_cost(sizeof(node_t)) + RAMFS_LLIST_ITEM_COST;
        if (S_ISDIR(mode)) {
                cost += RAMFS_LLIST_COST;
        } else if (S_ISFIFO(mode)) {
                cost += RAMFS_PIPE_COST;
        }

        int err = mem_reserve(hdl, cost);
        if (err) {
                return err;
        }

        node_t *node;
        err = sys_zalloc(sizeof(node_t), cast(void **, &node));
        if (!err) {

                time_t tm = 0;
                sys_gettime(&tm);

                node->name = filename;
                node->data.llist_t = NULL;
                node->gid = 0;
                node->uid = 0;
                node->mode = mode;
                node->mtime = tm;
                node->ctime = tm;
                node->size = 0;

                if (S_ISDIR(mode)) {
                        err = sys_llist_create(NULL, NULL, cast(llist_t **, &node->data));

                } else if (S_ISFIFO(mode)) {
                        err = sys_pipe_create(cast(pipe_t **, &node->data));
                }

                if (!err) {
                        if (sys_llist_push_back(parent->data.llist_t, node)) {
                                if (item) {
                                        *item = sys_llist_size(parent->data.llist_t) - 1;
                                }

                                *child = node;

                                hdl->file_count++;
                        } else {
                                err = ENOMEM;

                                /* release objects created above */
                                if (S_ISDIR(mode) && node->data.llist_t) {
                                        sys_llist_destroy(node->data.llist_t);
                                } else if (S_ISFIFO(mode) && node->data.pipe_t) {
                                        sys_pipe_destroy(node->data.pipe_t);
                                }
                        }
                }

                if (err) {
                        sys_free(cast(void **, &node));
                }
        }

        if (err) {
                mem_release(hdl, cost);
        }

        return err;
}

//==============================================================================
/**
 * @brief Function add node to list of open files
 *
 * @param [in] lfs              pointer to current LFS instance
 * @param [in] parent           parent node
 * @param [in] child            child added to list of open files
 *
 * @retval One of errno value (errno.h)
 */
//==============================================================================
static int add_node_to_open_files_list(struct RAMFS *hdl, node_t *parent, node_t *child)
{
        struct opened_file_info *opened_file_info;
        int err = sys_zalloc(sizeof(struct opened_file_info), cast(void **, &opened_file_info));
        if (!err) {
                /* accounted, but not limited: opening a file must not fail on full FS */
                hdl->used_mem += OPENED_FILE_COST;

                opened_file_info->remove_at_close = false;
                opened_file_info->child = child;
                opened_file_info->parent = parent;

                /* find if file shall be removed */
                sys_llist_foreach (struct opened_file_info *, opened_file, hdl->opended_files) {
                        if (opened_file->child == child && opened_file->remove_at_close) {
                                opened_file_info->remove_at_close = true;
                                break;
                        }
                }

                /* add open file info to list */
                if (!sys_llist_push_back(hdl->opended_files, opened_file_info)) {
                        sys_free(cast(void **, &opened_file_info));
                        mem_release(hdl, OPENED_FILE_COST);
                        err = ENOMEM;
                }
        }

        return err;
}

//==============================================================================
/**
 * @brief Function clear data of regular file.
 *
 * @param node                  node to write
 */
//==============================================================================
static void clear_regular_file(struct RAMFS *hdl, node_t *node)
{
        data_chain_t *chain = node->data.data_chain_t;

        while (chain) {
                data_chain_t *next = chain->next;
                mem_free(hdl, cast(void **, &chain), sizeof(data_chain_t));
                chain = next;
        }

        node->size = 0;
        node->data.data_chain_t = NULL;
}

//==============================================================================
/**
 * @brief Function write data to regular file.
 *
 * @param node                  node to write
 * @param src                   source buffer
 * @param count                 number of bytes to write
 * @param fpos                  file seek
 * @param wrcnt                 write counter
 *
 * @note When the limit is reached after part of data was written, the write is
 *       short (ESUCC with *wrcnt < count); next write returns ENOSPC.
 *
 * @retval ENOSPC               file system size limit reached, nothing written
 * @retval One of errno value (errno.h)
 */
//==============================================================================
static int write_regular_file(struct RAMFS *hdl, node_t *node, const u8_t *src, size_t count, fpos_t fpos,
        size_t *wrcnt)
{
        if (count == 0) {
                return ESUCC;
        }

        int err = ESUCC;
        size_t wrcnt_start = *wrcnt;
        data_chain_t *chain = node->data.data_chain_t;
        size_t depth = fpos / DATA_CHAIN_SIZE;
        size_t seek = fpos - (depth * DATA_CHAIN_SIZE);

        if (chain == NULL) {
                err = mem_zalloc(hdl, sizeof(data_chain_t), cast(void **, &chain));
                if (!err) {
                        node->data.data_chain_t = chain;
                }
        }

        // travel to selected chain
        while (!err && depth) {
                if (chain->next == NULL) {
                        err = mem_zalloc(hdl, sizeof(data_chain_t), cast(void **, &chain->next));
                }

                chain = chain->next;
                depth--;
        }

        // at position
        while (!err && count) {
                size_t tocpy = min(DATA_CHAIN_SIZE - seek, count);
                memcpy(&chain->buf[seek], src, tocpy);
                src += tocpy;
                fpos += tocpy;
                *wrcnt += tocpy;
                count -= tocpy;
                seek = 0;

                if (count) {
                        if (chain->next == NULL) {
                                err = mem_zalloc(hdl, sizeof(data_chain_t), cast(void **, &chain->next));
                        }

                        chain = chain->next;
                }
        }

        // calculate file size
        if (depth == 0) {
                node->size = max(node->size, fpos);
        }

        // short write if part of data was stored before limit was reached
        if (err == ENOSPC && *wrcnt > wrcnt_start) {
                err = ESUCC;
        }

        return err;
}

//==============================================================================
/**
 * @brief Function read data from regular file.
 *
 * @param node                  node to read
 * @param dst                   destination buffer
 * @param count                 number of bytes to read
 * @param fpos                  file seek
 * @param rdcnt                 read counter
 *
 * @retval One of errno value (errno.h)
 */
//==============================================================================
static int read_regular_file(node_t *node, u8_t *dst, size_t count, fpos_t fpos, size_t *rdcnt)
{
        int err = ESUCC;

        data_chain_t *chain = node->data.data_chain_t;
        size_t depth = fpos / DATA_CHAIN_SIZE;
        size_t seek = fpos - (depth * DATA_CHAIN_SIZE);

        for (; chain && depth; chain = chain->next, depth--)
                ;

        while (chain && count && fpos < node->size) {
                size_t tocpy = min(DATA_CHAIN_SIZE - seek, count);
                tocpy = min(tocpy, node->size - fpos);

                memcpy(dst, &chain->buf[seek], tocpy);
                dst += tocpy;
                fpos += tocpy;
                *rdcnt += tocpy;
                count -= tocpy;
                seek = 0;

                chain = chain->next;
        }

        return err;
}

//==============================================================================
/**
 * @brief Function allocate copy of last path component (basename).
 *
 * @param hdl                   file system handle (memory accounting)
 * @param path                  path
 * @param name                  allocated name (free by free_name())
 *
 * @retval EINVAL               path is not absolute or basename is empty
 * @retval One of errno value (errno.h)
 */
//==============================================================================
static int copy_basename(struct RAMFS *hdl, const char *path, char **name)
{
        if (!path || path[0] != '/') {
                return EINVAL;
        }

        /* ignore single trailing slash ("/dir/" -> "dir") */
        size_t end = strlen(path);
        if (end > 1 && path[end - 1] == '/') {
                end--;
        }

        size_t begin = end;
        while (begin > 0 && path[begin - 1] != '/') {
                begin--;
        }

        size_t len = end - begin;
        if (len == 0) {
                return EINVAL;
        }

        int err = mem_zalloc(hdl, len + 1, cast(void **, name));
        if (!err) {
                memcpy(*name, &path[begin], len);
                (*name)[len] = '\0';
        }

        return err;
}

//==============================================================================
/**
 * @brief Function find position of child in parent's list.
 *
 * Position is searched by pointer manually because directory lists are
 * created without compare functor.
 *
 * @param parent                parent directory node
 * @param child                 child node
 *
 * @return child position or -1 if not found
 */
//==============================================================================
static int find_child_index(node_t *parent, node_t *child)
{
        if (!parent || !S_ISDIR(parent->mode) || !parent->data.llist_t) {
                return -1;
        }

        int size = sys_llist_size(parent->data.llist_t);
        for (int i = 0; i < size; i++) {
                if (sys_llist_at(parent->data.llist_t, i) == child) {
                        return i;
                }
        }

        return -1;
}

//==============================================================================
/**
 * @brief Function removes all nodes from the tree (root stays).
 *
 * Implementation is iterative (no recursion) to keep stack usage constant
 * regardless of directory depth. In each step the first leaf (file or empty
 * directory) reachable by first-child links is removed. Complexity is
 * O(nodes * depth), which is acceptable for a RAM file system.
 *
 * @param hdl                   file system handle
 *
 * @return One of errno value (errno.h)
 */
//==============================================================================
static int release_tree(struct RAMFS *hdl)
{
        node_t *root = &hdl->root_dir;

        while (sys_llist_size(root->data.llist_t) > 0) {
                node_t *parent = root;
                node_t *child = sys_llist_at(parent->data.llist_t, 0);

                /* descend to the first leaf */
                while (child && S_ISDIR(child->mode) && child->data.llist_t &&
                        sys_llist_size(child->data.llist_t) > 0) {
                        parent = child;
                        child = sys_llist_at(parent->data.llist_t, 0);
                }

                if (child == NULL) {
                        return EFAULT; // list inconsistency
                }

                int err = delete_node(hdl, parent, child, 0);
                if (err) {
                        return err;
                }
        }

        return ESUCC;
}

//==============================================================================
/**
 * @brief Function calculates estimated heap cost of allocation.
 *
 * @param size                  requested size
 *
 * @return estimated number of bytes taken from heap
 */
//==============================================================================
static size_t mem_cost(size_t size)
{
        size_t align = RAMFS_ALLOC_ALIGN;
        return ((size + align - 1) / align) * align + RAMFS_ALLOC_OVERHEAD;
}

//==============================================================================
/**
 * @brief Function reserves memory in accounting (checks size limit).
 *
 * @param hdl                   file system handle
 * @param cost                  number of bytes to reserve
 *
 * @retval ENOSPC               size limit would be exceeded
 * @retval ESUCC                memory reserved
 */
//==============================================================================
static int mem_reserve(struct RAMFS *hdl, size_t cost)
{
        if (hdl->limit && (cost > hdl->limit || hdl->used_mem > hdl->limit - cost)) {
                return ENOSPC;
        }

        hdl->used_mem += cost;

        return ESUCC;
}

//==============================================================================
/**
 * @brief Function releases memory from accounting.
 *
 * @param hdl                   file system handle
 * @param cost                  number of bytes to release
 */
//==============================================================================
static void mem_release(struct RAMFS *hdl, size_t cost)
{
        hdl->used_mem = (hdl->used_mem > cost) ? hdl->used_mem - cost : 0;
}

//==============================================================================
/**
 * @brief Function allocates accounted memory.
 *
 * @param hdl                   file system handle
 * @param size                  size to allocate
 * @param mem                   allocated memory
 *
 * @retval ENOSPC               size limit reached
 * @retval One of errno value (errno.h)
 */
//==============================================================================
static int mem_zalloc(struct RAMFS *hdl, size_t size, void **mem)
{
        size_t cost = mem_cost(size);

        int err = mem_reserve(hdl, cost);
        if (!err) {
                err = sys_zalloc(size, mem);
                if (err) {
                        mem_release(hdl, cost);
                }
        }

        return err;
}

//==============================================================================
/**
 * @brief Function frees memory allocated by mem_zalloc().
 *
 * @param hdl                   file system handle
 * @param mem                   memory to free (set to NULL)
 * @param size                  size used at allocation
 */
//==============================================================================
static void mem_free(struct RAMFS *hdl, void **mem, size_t size)
{
        if (*mem) {
                sys_free(mem);
                mem_release(hdl, mem_cost(size));
        }
}

//==============================================================================
/**
 * @brief Function frees node name allocated by copy_basename().
 *
 * @param hdl                   file system handle
 * @param name                  name to free (set to NULL)
 */
//==============================================================================
static void free_name(struct RAMFS *hdl, char **name)
{
        if (*name) {
                mem_free(hdl, cast(void **, name), strsize(*name));
        }
}

//==============================================================================
/**
 * @brief Function parses mount options.
 *
 * Options are comma separated. Unknown options are ignored.
 *      size=<n>        limit in bytes
 *      size=<n>K       limit in KiB
 *      size=<n>M       limit in MiB
 *      size=<n>%       limit in percent of total memory (1..100)
 *
 * @param opts                  options string (can be NULL)
 * @param limit                 parsed limit (0 = no limit)
 *
 * @retval EINVAL               invalid size option
 * @retval ESUCC                options parsed
 */
//==============================================================================
static int parse_options(const char *opts, size_t *limit)
{
        *limit = 0;

        if (!opts) {
                return ESUCC;
        }

        const char *opt = opts;

        while (*opt) {
                const char *end = strchr(opt, ',');
                size_t len = end ? (size_t)(end - opt) : strlen(opt);

                if (len >= 5 && strncmp(opt, "size=", 5) == 0) {
                        const char *p = opt + 5;
                        const char *e = opt + len;
                        u64_t val = 0;

                        if (p == e || *p < '0' || *p > '9') {
                                return EINVAL;
                        }

                        while (p < e && *p >= '0' && *p <= '9') {
                                val = val * 10 + (u64_t)(*p - '0');
                                if (val > SIZE_MAX) {
                                        return EINVAL;
                                }
                                p++;
                        }

                        if (p < e) {
                                switch (*p) {
                                case 'k':
                                case 'K':
                                        if (val > SIZE_MAX / 1024) {
                                                return EINVAL;
                                        }
                                        val *= 1024;
                                        break;
                                case 'm':
                                case 'M':
                                        if (val > SIZE_MAX / (1024 * 1024)) {
                                                return EINVAL;
                                        }
                                        val *= 1024 * 1024;
                                        break;
                                case '%':
                                        if (val < 1 || val > 100) {
                                                return EINVAL;
                                        }
                                        val = ((u64_t)sys_get_mem_size() * val) / 100;
                                        break;
                                default:
                                        return EINVAL;
                                }
                                p++;
                        }

                        if (p != e || val == 0 || val > SIZE_MAX) {
                                return EINVAL;
                        }

                        *limit = (size_t)val;
                }

                if (!end) {
                        break;
                }

                opt = end + 1;
        }

        return ESUCC;
}

/*==============================================================================
  End of file
==============================================================================*/
