#ifndef FS_H
#define FS_H

#include "../include/types.h"

#define FS_BLOCK_SIZE       4096u
#define FS_DISK_SIZE        (1024u * 1024u)
#define FS_BLOCK_COUNT      (FS_DISK_SIZE / FS_BLOCK_SIZE)
#define FS_MAX_INODES       64u
#define FS_MAX_FD           16u
#define FS_NAME_MAX         27u
#define FS_PATH_MAX         255u

#define FS_O_RDONLY         0u
#define FS_O_WRONLY         1u
#define FS_O_RDWR           2u
#define FS_O_CREATE         0x100u
#define FS_O_TRUNC          0x200u
#define FS_O_APPEND         0x400u

#define FS_SEEK_SET         0
#define FS_SEEK_CUR         1
#define FS_SEEK_END         2

typedef struct {
    uint32_t size;
    uint32_t mode;              /* 1 = regular file, 2 = directory */
    uint32_t direct[8];         /* 8 x 4 KiB */
    uint32_t indirect;          /* single-indirect pointer block */
} inode_t;

/* VFS interface: the shell talks to fs_*; the implementation is behind this
 * small file-operations vtable so another filesystem can be plugged in later. */
typedef struct file_ops {
    int     (*open)(const char *path, uint32_t flags);
    ssize_t (*read)(int fd, void *buf, size_t count);
    ssize_t (*write)(int fd, const void *buf, size_t count);
    int     (*close)(int fd);
    int     (*unlink)(const char *path);
    int     (*mkdir)(const char *path);
    int     (*chdir)(const char *path);
    int     (*list)(const char *path);
    int     (*pwd)(char *buf, size_t len);
} file_ops_t;

void     fs_init(void);
int      fs_format(void);

int      fs_open(const char *path, uint32_t flags);
ssize_t  fs_read(int fd, void *buf, size_t count);
ssize_t  fs_write(int fd, const void *buf, size_t count);
int      fs_close(int fd);
int      fs_unlink(const char *path);

int      fs_mkdir(const char *path);
int      fs_chdir(const char *path);
int      fs_list(const char *path);
int      fs_pwd(char *buf, size_t len);

int      fs_stat(const char *path, inode_t *out);
uint32_t fs_free_blocks(void);
uint32_t fs_free_inodes(void);

/* Direct VFS access is useful for future filesystem implementations. */
const file_ops_t *vfs_root(void);

#endif
