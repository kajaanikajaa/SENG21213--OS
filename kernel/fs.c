#include "fs.h"
#include "ramdisk.h"
#include "vga.h"

#define FS_MAGIC            0x53465334u /* "SFS4" */
#define FS_VERSION          1u

#define SUPER_BLOCK         0u
#define ROOT_DIR_BLOCK      1u
#define BLOCK_BITMAP_BLOCK  2u
#define INODE_BITMAP_BLOCK  3u
#define INODE_TABLE_START   4u
#define INODE_TABLE_BLOCKS  2u
#define DATA_START_BLOCK    6u
#define JOURNAL_START       254u

#define INODE_FREE          0u
#define INODE_FILE          1u
#define INODE_DIR           2u
#define FS_INVALID_INODE    0xFFFFFFFFu
#define FS_MAX_FILE_BLOCKS  248u

#define DIR_ENTRY_SIZE      32u
#define DIR_ENTRIES         (FS_BLOCK_SIZE / DIR_ENTRY_SIZE)
#define PTRS_PER_BLOCK      (FS_BLOCK_SIZE / sizeof(uint32_t))

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t block_count;
    uint32_t inode_count;
    uint32_t block_bitmap;
    uint32_t inode_bitmap;
    uint32_t inode_table;
    uint32_t root_inode;
    uint32_t data_start;
    uint32_t journal_start;
} superblock_t;

typedef struct {
    char name[28];
    uint32_t inode;
} dirent_t;

typedef struct {
    uint32_t inode;
    uint32_t offset;
    uint32_t flags;
    uint8_t used;
} file_desc_t;

static file_desc_t fds[FS_MAX_FD];
static uint32_t cwd_inode;
static uint8_t fs_ready;

static uint8_t block_buf[FS_BLOCK_SIZE];
static uint8_t block_buf2[FS_BLOCK_SIZE];

static void mem_zero(void *p, uint32_t n)
{
    uint8_t *b = (uint8_t *)p;
    uint32_t i;
    for (i = 0; i < n; i++) b[i] = 0;
}

static void mem_copy(void *d, const void *s, uint32_t n)
{
    uint8_t *dst = (uint8_t *)d;
    const uint8_t *src = (const uint8_t *)s;
    uint32_t i;
    for (i = 0; i < n; i++) dst[i] = src[i];
}

static uint32_t str_len(const char *s)
{
    uint32_t n = 0;
    if (!s) return 0;
    while (s[n]) n++;
    return n;
}

static int str_eq(const char *a, const char *b)
{
    uint32_t i = 0;
    while (a[i] && b[i] && a[i] == b[i]) i++;
    return a[i] == '\0' && b[i] == '\0';
}

static void str_copy_n(char *dst, const char *src, uint32_t max)
{
    uint32_t i = 0;
    if (!max) return;
    while (i + 1 < max && src[i]) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static uint32_t bitmap_get(uint32_t block, uint32_t bit)
{
    ramdisk_read_block(block, block_buf);
    return (block_buf[bit >> 3] >> (bit & 7)) & 1u;
}

static void bitmap_set(uint32_t block, uint32_t bit, uint32_t value)
{
    ramdisk_read_block(block, block_buf);
    if (value) block_buf[bit >> 3] |= (uint8_t)(1u << (bit & 7));
    else block_buf[bit >> 3] &= (uint8_t)~(1u << (bit & 7));
    (void)ramdisk_write_block(block, block_buf);
}

static int alloc_block(void)
{
    uint32_t b;
    for (b = DATA_START_BLOCK; b < JOURNAL_START; b++) {
        if (!bitmap_get(BLOCK_BITMAP_BLOCK, b)) {
            bitmap_set(BLOCK_BITMAP_BLOCK, b, 1);
            ramdisk_zero_block(b);
            return (int)b;
        }
    }
    return -1;
}

static void free_block(uint32_t b)
{
    if (b >= DATA_START_BLOCK && b < JOURNAL_START)
        bitmap_set(BLOCK_BITMAP_BLOCK, b, 0);
}

static int alloc_inode(void)
{
    uint32_t i;
    for (i = 0; i < FS_MAX_INODES; i++) {
        if (!bitmap_get(INODE_BITMAP_BLOCK, i)) {
            bitmap_set(INODE_BITMAP_BLOCK, i, 1);
            return (int)i;
        }
    }
    return -1;
}

static void free_inode(uint32_t ino)
{
    if (ino < FS_MAX_INODES)
        bitmap_set(INODE_BITMAP_BLOCK, ino, 0);
}

static void inode_read(uint32_t ino, inode_t *out)
{
    uint32_t block = INODE_TABLE_START + (ino * sizeof(inode_t)) / FS_BLOCK_SIZE;
    uint32_t off = (ino * sizeof(inode_t)) % FS_BLOCK_SIZE;

    ramdisk_read_block(block, block_buf);
    mem_copy(out, block_buf + off, sizeof(inode_t));
}

static void inode_write(uint32_t ino, const inode_t *in)
{
    uint32_t block = INODE_TABLE_START + (ino * sizeof(inode_t)) / FS_BLOCK_SIZE;
    uint32_t off = (ino * sizeof(inode_t)) % FS_BLOCK_SIZE;

    ramdisk_read_block(block, block_buf);
    mem_copy(block_buf + off, in, sizeof(inode_t));
    (void)ramdisk_write_block(block, block_buf);
}

static int dir_find(uint32_t dir_ino, const char *name, uint32_t *ino_out)
{
    inode_t dir;
    uint32_t i;
    dirent_t *e;

    inode_read(dir_ino, &dir);
    if (dir.mode != INODE_DIR || !dir.direct[0]) return -1;

    ramdisk_read_block(dir.direct[0], block_buf);
    for (i = 0; i < DIR_ENTRIES; i++) {
        e = (dirent_t *)(block_buf + i * DIR_ENTRY_SIZE);
        if (e->inode != FS_INVALID_INODE && str_eq(e->name, name)) {
            if (ino_out) *ino_out = e->inode;
            return (int)i;
        }
    }
    return -1;
}

static int dir_add(uint32_t dir_ino, const char *name, uint32_t child)
{
    inode_t dir;
    uint32_t i;
    dirent_t *e;

    if (!name || !name[0] || str_len(name) > FS_NAME_MAX) return -1;
    inode_read(dir_ino, &dir);
    if (dir.mode != INODE_DIR || !dir.direct[0]) return -1;
    if (dir_find(dir_ino, name, NULL) >= 0) return -1;

    ramdisk_read_block(dir.direct[0], block_buf);
    for (i = 0; i < DIR_ENTRIES; i++) {
        e = (dirent_t *)(block_buf + i * DIR_ENTRY_SIZE);
        if (e->inode == FS_INVALID_INODE) {
            mem_zero(e, DIR_ENTRY_SIZE);
            str_copy_n(e->name, name, sizeof(e->name));
            e->inode = child;
            return ramdisk_write_block(dir.direct[0], block_buf);
        }
    }
    return -1;
}

static int dir_remove(uint32_t dir_ino, const char *name)
{
    inode_t dir;
    int pos;
    dirent_t *e;

    inode_read(dir_ino, &dir);
    if (dir.mode != INODE_DIR || !dir.direct[0]) return -1;
    pos = dir_find(dir_ino, name, NULL);
    if (pos < 0) return -1;

    ramdisk_read_block(dir.direct[0], block_buf);
    e = (dirent_t *)(block_buf + (uint32_t)pos * DIR_ENTRY_SIZE);
    e->inode = FS_INVALID_INODE;
    e->name[0] = '\0';
    return ramdisk_write_block(dir.direct[0], block_buf);
}

static int next_component(const char **p, char *out)
{
    uint32_t n = 0;
    const char *s = *p;

    while (*s == '/') s++;
    if (!*s) {
        *p = s;
        out[0] = '\0';
        return 0;
    }

    while (*s && *s != '/') {
        if (n >= FS_NAME_MAX) return -2;
        out[n++] = *s++;
    }
    out[n] = '\0';
    *p = s;
    return 1;
}

static int resolve_path(const char *path, uint32_t start, uint32_t *out)
{
    char part[28];
    const char *p;
    uint32_t cur;
    uint32_t child;
    int rc;

    if (!path || !path[0]) return -1;
    cur = (path[0] == '/') ? 0u : start;
    p = path;

    for (;;) {
        rc = next_component(&p, part);
        if (rc < 0) return -1;
        if (rc == 0) {
            if (out) *out = cur;
            return 0;
        }

        if (str_eq(part, ".")) {
            /* nothing */
        } else if (str_eq(part, "..")) {
            if (dir_find(cur, "..", &child) >= 0) cur = child;
            else return -1;
        } else {
            if (dir_find(cur, part, &child) < 0) return -1;
            cur = child;
        }
    }
}

static int split_parent(const char *path, uint32_t *parent, char *name)
{
    char tmp[FS_PATH_MAX + 1];
    char part[28];
    const char *p;
    uint32_t cur;
    uint32_t child;
    uint32_t last_len = 0;
    char last[28];
    int rc;

    if (!path || !path[0]) return -1;
    if (str_len(path) > FS_PATH_MAX) return -1;

    str_copy_n(tmp, path, sizeof(tmp));
    p = tmp;
    cur = (tmp[0] == '/') ? 0u : cwd_inode;
    last[0] = '\0';

    for (;;) {
        rc = next_component(&p, part);
        if (rc < 0) return -1;
        if (rc == 0) break;

        if (str_eq(part, "."))
            continue;

        if (str_eq(part, "..")) {
            if (dir_find(cur, "..", &child) < 0) return -1;
            cur = child;
            continue;
        }

        /* Look ahead: if more components follow, this one must be a directory. */
        {
            const char *look = p;
            char next[28];
            int more = next_component(&look, next);
            if (more == 1) {
                if (dir_find(cur, part, &child) < 0) return -1;
                cur = child;
            } else {
                str_copy_n(last, part, sizeof(last));
                last_len = str_len(last);
                break;
            }
        }
    }

    if (!last_len || str_eq(last, ".") || str_eq(last, "..")) return -1;
    if (parent) *parent = cur;
    if (name) str_copy_n(name, last, 28);
    return 0;
}

static int block_for_inode(inode_t *in, uint32_t logical, int allocate)
{
    uint32_t ptrs[PTRS_PER_BLOCK];
    uint32_t b;

    if (logical < 8u) {
        if (!in->direct[logical] && allocate) {
            b = (uint32_t)alloc_block();
            if ((int)b < 0) return -1;
            in->direct[logical] = b;
        }
        return in->direct[logical] ? (int)in->direct[logical] : 0;
    }

    logical -= 8u;
    if (logical >= PTRS_PER_BLOCK) return -1;

    if (!in->indirect) {
        if (!allocate) return 0;
        b = (uint32_t)alloc_block();
        if ((int)b < 0) return -1;
        in->indirect = b;
        mem_zero(ptrs, sizeof(ptrs));
        if (ramdisk_write_block(b, ptrs) < 0) return -1;
    }

    ramdisk_read_block(in->indirect, ptrs);
    if (!ptrs[logical] && allocate) {
        b = (uint32_t)alloc_block();
        if ((int)b < 0) return -1;
        ptrs[logical] = b;
        if (ramdisk_write_block(in->indirect, ptrs) < 0) return -1;
    }
    return ptrs[logical] ? (int)ptrs[logical] : 0;
}

static void release_inode_blocks(inode_t *in)
{
    uint32_t i;
    uint32_t ptrs[PTRS_PER_BLOCK];

    for (i = 0; i < 8u; i++) {
        if (in->direct[i]) free_block(in->direct[i]);
        in->direct[i] = 0;
    }

    if (in->indirect) {
        ramdisk_read_block(in->indirect, ptrs);
        for (i = 0; i < PTRS_PER_BLOCK; i++)
            if (ptrs[i]) free_block(ptrs[i]);
        free_block(in->indirect);
        in->indirect = 0;
    }
}

int fs_format(void)
{
    superblock_t sb;
    inode_t root;
    dirent_t *e;
    uint32_t i;

    /* Fresh RAM-disk format. This is the only place that directly clears BSS. */
    mem_zero(ramdisk_data(), FS_DISK_SIZE);

    sb.magic = FS_MAGIC;
    sb.version = FS_VERSION;
    sb.block_count = FS_BLOCK_COUNT;
    sb.inode_count = FS_MAX_INODES;
    sb.block_bitmap = BLOCK_BITMAP_BLOCK;
    sb.inode_bitmap = INODE_BITMAP_BLOCK;
    sb.inode_table = INODE_TABLE_START;
    sb.root_inode = 0;
    sb.data_start = DATA_START_BLOCK;
    sb.journal_start = JOURNAL_START;

    mem_zero(block_buf, FS_BLOCK_SIZE);
    mem_copy(block_buf, &sb, sizeof(sb));
    if (ramdisk_write_block(SUPER_BLOCK, block_buf) < 0) return -1;

    /* Mark fixed metadata and journal blocks as reserved. */
    for (i = 0; i < DATA_START_BLOCK; i++) bitmap_set(BLOCK_BITMAP_BLOCK, i, 1);
    for (i = JOURNAL_START; i < FS_BLOCK_COUNT; i++) bitmap_set(BLOCK_BITMAP_BLOCK, i, 1);

    bitmap_set(INODE_BITMAP_BLOCK, 0, 1);

    mem_zero(&root, sizeof(root));
    root.mode = INODE_DIR;
    root.direct[0] = ROOT_DIR_BLOCK;
    inode_write(0, &root);

    mem_zero(block_buf, FS_BLOCK_SIZE);
    for (i = 0; i < DIR_ENTRIES; i++)
        ((dirent_t *)block_buf)[i].inode = FS_INVALID_INODE;

    e = (dirent_t *)block_buf;
    str_copy_n(e[0].name, ".", sizeof(e[0].name));
    e[0].inode = 0;
    str_copy_n(e[1].name, "..", sizeof(e[1].name));
    e[1].inode = 0;
    if (ramdisk_write_block(ROOT_DIR_BLOCK, block_buf) < 0) return -1;

    cwd_inode = 0;
    fs_ready = 1;
    return 0;
}

void fs_init(void)
{
    superblock_t sb;
    ramdisk_init();
    ramdisk_read_block(SUPER_BLOCK, block_buf);
    mem_copy(&sb, block_buf, sizeof(sb));

    if (sb.magic != FS_MAGIC || sb.version != FS_VERSION ||
        sb.block_count != FS_BLOCK_COUNT || sb.inode_count != FS_MAX_INODES) {
        (void)fs_format();
    } else {
        cwd_inode = sb.root_inode;
        fs_ready = 1;
    }

    mem_zero(fds, sizeof(fds));
}

static int create_inode(uint32_t parent, const char *name, uint32_t mode, uint32_t *out)
{
    int ino_i;
    int data_i;
    inode_t in;
    dirent_t *e;

    if (dir_find(parent, name, NULL) >= 0) return -2;

    ino_i = alloc_inode();
    if (ino_i < 0) return -3;

    mem_zero(&in, sizeof(in));
    in.mode = mode;

    data_i = alloc_block();
    if (data_i < 0) {
        free_inode((uint32_t)ino_i);
        return -4;
    }
    in.direct[0] = (uint32_t)data_i;
    inode_write((uint32_t)ino_i, &in);

    mem_zero(block_buf, FS_BLOCK_SIZE);
    for (uint32_t i = 0; i < DIR_ENTRIES; i++)
        ((dirent_t *)block_buf)[i].inode = FS_INVALID_INODE;

    if (mode == INODE_DIR) {
        e = (dirent_t *)block_buf;
        str_copy_n(e[0].name, ".", sizeof(e[0].name));
        e[0].inode = (uint32_t)ino_i;
        str_copy_n(e[1].name, "..", sizeof(e[1].name));
        e[1].inode = parent;
    }

    if (ramdisk_write_block((uint32_t)data_i, block_buf) < 0 ||
        dir_add(parent, name, (uint32_t)ino_i) < 0) {
        free_block((uint32_t)data_i);
        free_inode((uint32_t)ino_i);
        return -5;
    }

    if (out) *out = (uint32_t)ino_i;
    return 0;
}

int fs_open(const char *path, uint32_t flags)
{
    uint32_t ino;
    uint32_t parent;
    char name[28];
    inode_t in;
    uint32_t i;

    if (!fs_ready || !path) return -1;

    if (resolve_path(path, cwd_inode, &ino) < 0) {
        if (!(flags & FS_O_CREATE)) return -2;
        if (split_parent(path, &parent, name) < 0) return -3;
        if (create_inode(parent, name, INODE_FILE, &ino) < 0) return -4;
    }

    inode_read(ino, &in);
    if (in.mode != INODE_FILE) return -5;

    if (flags & FS_O_TRUNC) {
        release_inode_blocks(&in);
        in.size = 0;
        inode_write(ino, &in);
    }

    for (i = 0; i < FS_MAX_FD; i++) {
        if (!fds[i].used) {
            fds[i].used = 1;
            fds[i].inode = ino;
            fds[i].flags = flags;
            fds[i].offset = (flags & FS_O_APPEND) ? in.size : 0;
            return (int)i;
        }
    }
    return -6;
}

ssize_t fs_read(int fd, void *buf, size_t count)
{
    inode_t in;
    uint32_t done = 0;
    uint32_t block_no;
    uint32_t off;
    uint32_t take;
    int b;

    if (fd < 0 || (uint32_t)fd >= FS_MAX_FD || !fds[fd].used || !buf) return -1;
    inode_read(fds[fd].inode, &in);
    if (in.mode != INODE_FILE) return -1;
    if (fds[fd].offset >= in.size) return 0;

    if (count > in.size - fds[fd].offset)
        count = in.size - fds[fd].offset;

    while (done < count) {
        block_no = fds[fd].offset / FS_BLOCK_SIZE;
        off = fds[fd].offset % FS_BLOCK_SIZE;
        b = block_for_inode(&in, block_no, 0);
        if (b <= 0) break;

        ramdisk_read_block((uint32_t)b, block_buf2);
        take = FS_BLOCK_SIZE - off;
        if (take > count - done) take = count - done;
        mem_copy((uint8_t *)buf + done, block_buf2 + off, take);

        done += take;
        fds[fd].offset += take;
    }
    return (ssize_t)done;
}

ssize_t fs_write(int fd, const void *buf, size_t count)
{
    inode_t in;
    uint32_t done = 0;
    uint32_t block_no;
    uint32_t off;
    uint32_t take;
    int b;

    if (fd < 0 || (uint32_t)fd >= FS_MAX_FD || !fds[fd].used || !buf) return -1;
    if ((fds[fd].flags & 3u) == FS_O_RDONLY) return -1;

    inode_read(fds[fd].inode, &in);
    if (in.mode != INODE_FILE) return -1;

    while (done < count) {
        block_no = fds[fd].offset / FS_BLOCK_SIZE;
        if (block_no >= FS_MAX_FILE_BLOCKS) break;

        b = block_for_inode(&in, block_no, 1);
        if (b <= 0) break;

        off = fds[fd].offset % FS_BLOCK_SIZE;
        take = FS_BLOCK_SIZE - off;
        if (take > count - done) take = count - done;

        ramdisk_read_block((uint32_t)b, block_buf2);
        mem_copy(block_buf2 + off, (const uint8_t *)buf + done, take);
        if (ramdisk_write_block((uint32_t)b, block_buf2) < 0) break;

        done += take;
        fds[fd].offset += take;
        if (fds[fd].offset > in.size) in.size = fds[fd].offset;
    }

    inode_write(fds[fd].inode, &in);
    return (ssize_t)done;
}

int fs_close(int fd)
{
    if (fd < 0 || (uint32_t)fd >= FS_MAX_FD || !fds[fd].used) return -1;
    fds[fd].used = 0;
    return 0;
}

int fs_unlink(const char *path)
{
    uint32_t ino;
    uint32_t parent;
    char name[28];
    inode_t in;
    uint32_t i;
    dirent_t *e;

    if (!fs_ready || !path) return -1;
    if (split_parent(path, &parent, name) < 0) return -2;
    if (dir_find(parent, name, &ino) < 0) return -3;
    if (ino == 0) return -4;

    inode_read(ino, &in);
    if (in.mode == INODE_DIR) {
        ramdisk_read_block(in.direct[0], block_buf);
        for (i = 2; i < DIR_ENTRIES; i++) {
            e = (dirent_t *)(block_buf + i * DIR_ENTRY_SIZE);
            if (e->inode != FS_INVALID_INODE) return -5; /* non-empty */
        }
    }

    if (dir_remove(parent, name) < 0) return -6;
    release_inode_blocks(&in);
    mem_zero(&in, sizeof(in));
    inode_write(ino, &in);
    free_inode(ino);

    if (cwd_inode == ino) cwd_inode = 0;
    return 0;
}

int fs_mkdir(const char *path)
{
    uint32_t parent;
    char name[28];

    if (!fs_ready || split_parent(path, &parent, name) < 0) return -1;
    if (str_eq(name, ".") || str_eq(name, "..")) return -2;
    return create_inode(parent, name, INODE_DIR, NULL);
}

int fs_chdir(const char *path)
{
    uint32_t ino;
    inode_t in;

    if (!fs_ready || resolve_path(path, cwd_inode, &ino) < 0) return -1;
    inode_read(ino, &in);
    if (in.mode != INODE_DIR) return -2;
    cwd_inode = ino;
    return 0;
}

int fs_list(const char *path)
{
    uint32_t ino;
    inode_t dir;
    uint32_t i;
    dirent_t *e;
    inode_t child;

    if (!fs_ready) return -1;
    if (!path || !path[0]) ino = cwd_inode;
    else if (resolve_path(path, cwd_inode, &ino) < 0) return -2;

    inode_read(ino, &dir);
    if (dir.mode != INODE_DIR) return -3;

    /* Keep the directory block in block_buf2.  inode_read() uses block_buf,
     * so using block_buf here would overwrite the directory while the loop
     * is still scanning it.  That used to make ls print garbage entries and
     * appear to run forever. */
    ramdisk_read_block(dir.direct[0], block_buf2);
    vga_puts("\n  NAME                         TYPE       SIZE\n");
    vga_puts("  ------------------------------------------------\n");
    for (i = 0; i < DIR_ENTRIES; i++) {
        dirent_t entry;
        e = (dirent_t *)(block_buf2 + i * DIR_ENTRY_SIZE);
        mem_copy(&entry, e, sizeof(entry));
        if (entry.inode == FS_INVALID_INODE) continue;
        if (entry.inode >= FS_MAX_INODES) continue;

        inode_read(entry.inode, &child);
        if (child.mode != INODE_FILE && child.mode != INODE_DIR) continue;
        vga_puts("  ");
        vga_puts(entry.name);
        for (uint32_t j = str_len(entry.name); j < 28; j++) vga_putchar(' ');
        vga_puts(child.mode == INODE_DIR ? "<DIR>      " : "<FILE>     ");
        vga_printf("%u\n", child.size);
    }
    vga_puts("\n");
    return 0;
}

static int pwd_rec(uint32_t ino, char *buf, uint32_t len, uint32_t *pos)
{
    uint32_t parent;
    char name[28];
    uint32_t i;
    dirent_t *e;
    inode_t dir;

    if (ino == 0) {
        if (*pos + 1 >= len) return -1;
        buf[(*pos)++] = '/';
        buf[*pos] = '\0';
        return 0;
    }

    inode_read(ino, &dir);
    if (dir_find(ino, "..", &parent) < 0) return -1;
    if (dir_find(parent, ".", NULL) < 0) return -1;

    ramdisk_read_block(dir.direct[0], block_buf2);
    name[0] = '\0';
    /* Find this inode's name in its parent directory. */
    {
        inode_t pd;
        inode_read(parent, &pd);
        ramdisk_read_block(pd.direct[0], block_buf);
        for (i = 0; i < DIR_ENTRIES; i++) {
            e = (dirent_t *)(block_buf + i * DIR_ENTRY_SIZE);
            if (e->inode == ino && !str_eq(e->name, ".") && !str_eq(e->name, "..")) {
                str_copy_n(name, e->name, sizeof(name));
                break;
            }
        }
    }
    if (!name[0]) return -1;

    if (pwd_rec(parent, buf, len, pos) < 0) return -1;
    if (*pos + str_len(name) + 1 >= len) return -1;
    if (*pos > 1) buf[(*pos)++] = '/';
    mem_copy(buf + *pos, name, str_len(name));
    *pos += str_len(name);
    buf[*pos] = '\0';
    return 0;
}

int fs_pwd(char *buf, size_t len)
{
    uint32_t pos = 0;
    if (!buf || len < 2) return -1;
    buf[0] = '\0';
    return pwd_rec(cwd_inode, buf, len, &pos);
}

int fs_stat(const char *path, inode_t *out)
{
    uint32_t ino;
    if (!out || resolve_path(path, cwd_inode, &ino) < 0) return -1;
    inode_read(ino, out);
    return 0;
}

uint32_t fs_free_blocks(void)
{
    uint32_t b, free_count = 0;
    for (b = DATA_START_BLOCK; b < JOURNAL_START; b++)
        if (!bitmap_get(BLOCK_BITMAP_BLOCK, b)) free_count++;
    return free_count;
}

uint32_t fs_free_inodes(void)
{
    uint32_t i, free_count = 0;
    for (i = 0; i < FS_MAX_INODES; i++)
        if (!bitmap_get(INODE_BITMAP_BLOCK, i)) free_count++;
    return free_count;
}

/* ---------------------------------------------------------------------------
 * VFS layer
 * --------------------------------------------------------------------------*/
static const file_ops_t ramfs_ops = {
    fs_open, fs_read, fs_write, fs_close, fs_unlink,
    fs_mkdir, fs_chdir, fs_list, fs_pwd
};

const file_ops_t *vfs_root(void)
{
    return &ramfs_ops;
}
