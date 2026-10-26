#define FUSE_USE_VERSION 31
#include <fuse3/fuse.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>

static const char contents[] = "Hello from my filesystem!\n";

static int fs_getattr(const char *path, struct stat *st,
                      struct fuse_file_info *fi)
{
    (void)fi;
    memset(st, 0, sizeof(*st));

    if (strcmp(path, "/") == 0) {
        st->st_mode = S_IFDIR | 0555;
        st->st_nlink = 2;
        return 0;
    }

    if (strcmp(path, "/hello.txt") == 0) {
        st->st_mode = S_IFREG | 0444;
        st->st_nlink = 1;
        st->st_size = sizeof(contents) - 1;
        return 0;
    }

    return -ENOENT;
}

static int fs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                      off_t offset, struct fuse_file_info *fi,
                      enum fuse_readdir_flags flags)
{
    (void)offset;
    (void)fi;
    (void)flags;

    if (strcmp(path, "/") != 0)
        return -ENOENT;

    filler(buf, ".", NULL, 0, fuse_fill_dir_flags(0));
    filler(buf, "..", NULL, 0, fuse_fill_dir_flags(0));
    filler(buf, "hello.txt", NULL, 0, fuse_fill_dir_flags(0));
    return 0;
}

static int fs_open(const char *path, struct fuse_file_info *fi)
{
    if (strcmp(path, "/hello.txt") != 0)
        return -ENOENT;

    if ((fi->flags & O_ACCMODE) != O_RDONLY)
        return -EACCES;

    return 0;
}

static int fs_read(const char *path, char *buf, size_t size, off_t offset,
                   struct fuse_file_info *fi)
{
    (void)fi;

    if (strcmp(path, "/hello.txt") != 0)
        return -ENOENT;

    size_t length = sizeof(contents) - 1;
    if (offset < 0)
        return -EINVAL;
    if ((size_t)offset >= length)
        return 0;
    if (size > length - (size_t)offset)
        size = length - (size_t)offset;

    memcpy(buf, contents + offset, size);
    return (int)size;
}

static const struct fuse_operations operations = {
    .getattr = fs_getattr,
    .open    = fs_open,
    .read    = fs_read,
    .readdir = fs_readdir,
};

int main(int argc, char *argv[])
{
    return fuse_main(argc, argv, &operations, NULL);
}
