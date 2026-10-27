#include "background.hpp"
#include "cli.hpp"
#include "config_file.hpp"
#include "file.hpp"
#include "logger.hpp"
#include "parser_with_cache.hpp"
#include <filesystem>
#include <format>
#define FUSE_USE_VERSION 31
#include <fuse3/fuse.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>

#include "command_parser.hpp"

static const char contents[] = "Hello from my filesystem!\n";
fpath target_read_path;

static int fs_getattr(const char *raw_path, struct stat *st,
                      struct fuse_file_info *fi)
{
    (void)fi;
    memset(st, 0, sizeof(*st));

    ParserResult res = get_file_attr_with_cache(current_config.rclone_path,raw_path);
    if (!res.exists) return -ENOENT;
    if (res.is_file) {
        st->st_mode = S_IFREG | 0444;
        st->st_nlink = 1;
        st->st_size = res.filesize;
        return 0;
    }
    if (res.is_directory) {
        st->st_mode = S_IFDIR | 0555;
        st->st_nlink = 2;
        return 0;
    }

    return -ENOENT;
}

static int fs_readdir(const char *raw_path, void *buf, fuse_fill_dir_t filler,
                      off_t offset, struct fuse_file_info *fi,
                      enum fuse_readdir_flags flags)
{
    (void)offset;
    (void)fi;
    (void)flags;

    vector<string> opts{};
    ParserResult res = list_dir_with_cache(current_config.rclone_path,raw_path,opts);


    filler(buf, ".", NULL, 0, fuse_fill_dir_flags(0));
    filler(buf, "..", NULL, 0, fuse_fill_dir_flags(0));
    for (auto n : opts) {
        cbu::log_debug(std::format("Add opt {}",n));
        filler(buf,n.c_str(),NULL,0,fuse_fill_dir_flags(0));
    }

    return 0;
}

static int fs_open(const char *raw_path, struct fuse_file_info *fi)
{
    if (!open_file_with_cache(current_config.rclone_path,raw_path)) return -ENOENT;

    if ((fi->flags & O_ACCMODE) != O_RDONLY) return -EACCES;

    return 0;
}

static int fs_read(const char *raw_path, char *buf, size_t size, off_t offset,
                   struct fuse_file_info *fi)
{
    (void)fi;

    BYTEARRAY data{};
    if (!read_file_with_cache(current_config.rclone_path,raw_path,data)) {
        return -ENOENT;
    }

    size_t length = data.size();
    if (offset < 0)
        return -EINVAL;
    if ((size_t)offset >= length)
        return 0;
    if (size > length - (size_t)offset)
        size = length - (size_t)offset;

    memcpy(buf, data.data() + offset, size);
    return (int)size;
}

static const struct fuse_operations operations = {
    .getattr = fs_getattr,
    .open    = fs_open,
    .read    = fs_read,
    .readdir = fs_readdir,
};

static void recurse_download_all(fpath p) {
    for (auto &f : cbu::iterate_dir(p)) {
        if (std::filesystem::is_directory(f)) recurse_download_all(f);

        cbu::log_info(std::format("Downloading '{}'",cbu::path_to_utf8(f)));
        cbu::require_file_binary_path(f);
    }
}
int main(int argc, char *argv[])
{
    auto out = cbu::parse_args(argc,argv,{
        cbu::parser_flag{'d',"daemon","Starts the program in daemon mode"},
        cbu::parser_flag{'r',"reload","Reloads the current filecache"},
        cbu::parser_flag{'f',"fetch","Downloads ALL files"}
    },{});
    if (!out.valid) {
        cbu::log_error(false,"Command parser returned an invalid state!");
        return -1;
    }
    if (!out.can_proceed) {
        return 0;
    }

    read_config();
    if (out.flags.contains('f')) {
        if (!current_config) {
            cbu::log_error(false,"Invalid config!");
            return -1;
        }

        recurse_download_all(current_config.mount_path);
        return 0;
    }
    if (out.flags.contains('r')) {
        fpath c = cbu::cache_path() / "files";
        std::filesystem::remove_all(c);

        return 0;
    }
    if (out.flags.contains('d')) {
        if (!current_config) {
            cbu::log_error(false,"Invalid config!");
            return -1;
        }

        string mountpoint = cbu::path_to_utf8(current_config.mount_path);
        char *fuse_argv[] = {
            argv[0],
            (char*) "-f",
            (char*) mountpoint.c_str(),
            (char*) "-o",
            (char*) "allow_other,default_permissions"
        };
        int fuse_argc = sizeof(fuse_argv) / sizeof(char*);

        start_background();
        int code = fuse_main(fuse_argc, fuse_argv, &operations, NULL);

        end_background();
        return code;
    }
    if (!current_config) {
        cbu::log_warn("No valid config exists for current user");
    }
    bool res;
    cbu::cli_input("Create a config for this user? (default false)");
    cbu::cli_get_valid_bool(&res,true);
    if (!res) return 0;

    fpath rclone,tgt;
    cbu::cli_input("Enter the directory of the remote filesystem such as an rclone mountpoint");
    if (!cbu::cli_get_valid_dirpath(&rclone)) {
        cbu::log_error(false,"Cancelled");
        return -1;
    }
    cbu::cli_input("Enter the directory for the target mountpoint");
    if (!cbu::cli_get_valid_dirpath(&tgt)) {
        cbu::log_error(false,"Cancelled");
        return -1;
    }

    current_config.mount_path = tgt;
    current_config.rclone_path = rclone;
    update_config();
    cbu::log_success("Config updated! Restart simplerfs to apply changes.");

    return 0;
}
