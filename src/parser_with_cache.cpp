#include "parser_with_cache.hpp"
#include "binary.hpp"
#include "file.hpp"
#include "logger.hpp"
#include <filesystem>
#include <format>
#include <mutex>
#include <unordered_set>

struct MetaFile {
    std::unordered_set<fpath> downloaded_files{};
    std::unordered_map<fpath,size_t> filesizes{};
};
struct FileSizeDataBlock {
    fpath path;
    size_t size;
};

class MetaFileFormatV1 : public cbu::BinaryFileVersion {
public:
    cbu::BinaryFileSection *get_sections() override {
        return new cbu::MainBinaryFileSection({
            new cbu::RepeatingBinarySection([](void *o,size_t *size,size_t *len) -> void* { // downloads
                auto *meta = (MetaFile*) o;

                *size = sizeof(fpath);
                *len = meta->downloaded_files.size();
                fpath *contents = new fpath[*len];
                size_t i = 0;
                for (auto &s : meta->downloaded_files) {
                    contents[i] = s;
                    i += 1;
                }

                return (void*) contents;
            },[](void *o) { delete[] (fpath*) o; },{
                new cbu::StringBinarySection([](void *o,auto value) {
                    auto *meta = (MetaFile*) o;

                    meta->downloaded_files.insert(value);
                },[](void *o) -> string {
                    auto *f = (fpath*) o;

                    return cbu::path_to_utf8(*f);
                })
            },false),
            new cbu::RepeatingBinarySection([](void *o,size_t *size,size_t *len) -> void* { // filesizes
                auto *meta = (MetaFile*) o;

                *size = sizeof(FileSizeDataBlock);
                *len = meta->filesizes.size();
                FileSizeDataBlock *contents = new FileSizeDataBlock[*len];
                size_t i = 0;
                for (auto &[key,sz] : meta->filesizes) {
                    contents[i] = {
                        .path = key,
                        .size = sz
                    };
                    i += 1;
                }

                return (void*) contents;
            },[](void *o) { delete[] (FileSizeDataBlock*) o; },{
                new cbu::DataBinarySection([](void *o,void *d) {
                    auto *meta = (MetaFile*) o;
                    auto *data = (FileSizeDataBlock*) d;

                    meta->filesizes[data->path] = data->size;
                },[]() -> void* { return new FileSizeDataBlock(); },[](void *o) { delete (FileSizeDataBlock*) o; },{
                    new cbu::StringBinarySection([](void *o,auto value) {
                        auto *data = (FileSizeDataBlock*) o;

                        data->path = value;
                    },[](void *o) -> string {
                        auto *data = (FileSizeDataBlock*) o;

                        return cbu::path_to_utf8(data->path);
                    }),
                    new cbu::S64BinarySection([](void *o,auto value) {
                        auto *data = (FileSizeDataBlock*) o;

                        data->size = value;
                    },[](void *o) -> S64 {
                        auto *data = (FileSizeDataBlock*) o;

                        return data->size;
                    })
                })
            },false)
        });
    }
};
class MetaFileFormat : public cbu::BinaryFileFormat {
public:
    MetaFileFormat() : BinaryFileFormat({new MetaFileFormatV1()},"cfg") {}
};

static fpath cache_root = cbu::cache_path();
static fpath cache_dir = cache_root / "files";

static std::mutex meta_lock{};
static MetaFile current_meta{};

struct CacheObject {
    fpath cache_path;
};

static fpath meta_path = cache_root / "fileinfo.meta";
static void save_meta() {
    auto fmt = MetaFileFormat();

    fmt.load_object_to_buffer(&current_meta);
    fmt.save_buffer_to_file(meta_path);
}
void read_meta() {
    auto fmt = MetaFileFormat();

    fmt.load_file_to_buffer(meta_path);
    fmt.load_buffer_to_object(&current_meta);
}
static bool copy_attr_from_src(fpath abs_path,fpath cache_path) {
    std::lock_guard<std::mutex> lock{meta_lock};

    std::filesystem::create_directories(cache_path.parent_path());
    cbu::write_file_empty(cache_path);
    current_meta.filesizes[abs_path] = std::filesystem::file_size(abs_path);
    if (current_meta.downloaded_files.contains(abs_path)) current_meta.downloaded_files.erase(abs_path);
    save_meta();

    return true;
}
static void check_attr_from_fs(fpath abs_path,fpath cache_path) {
    if (!std::filesystem::exists(cache_path)) {
        copy_attr_from_src(abs_path,cache_path);
        return;
    }
    if (!std::filesystem::exists(abs_path)) {
        std::filesystem::remove_all(cache_path);
        std::lock_guard<std::mutex> lock{meta_lock};
        if (current_meta.filesizes.contains(abs_path)) current_meta.filesizes.erase(abs_path);
        if (current_meta.downloaded_files.contains(abs_path)) current_meta.downloaded_files.erase(abs_path);
        save_meta();

        return;
    }

    auto src_stamp = std::filesystem::last_write_time(abs_path);
    auto cache_stamp = std::filesystem::last_write_time(cache_path);

    if (src_stamp < cache_stamp) return;

    copy_attr_from_src(abs_path,cache_path);
}


static void recurse_fs_dir_checks(fpath abs_path,fpath cache_path) {
    for (auto f : cbu::iterate_dir(abs_path)) {
        fpath relative = std::filesystem::relative(f,abs_path);

        if (std::filesystem::is_directory(f)) recurse_fs_dir_checks(f,cache_path / relative);
        if (!std::filesystem::is_regular_file(f)) continue;

        check_attr_from_fs(f,cache_path / relative);
    }
}
static std::mutex attr_lock{};
static umap<fpath,CacheObject> attr_recheck_queue{};
void background_checks() {
    std::lock_guard<std::mutex> lock{attr_lock};
    for (auto &[abs_path,obj] : attr_recheck_queue) {
        fpath cache_path = obj.cache_path;
        if (!std::filesystem::exists(cache_path)) continue;
        if (std::filesystem::is_directory(cache_path)) {
            recurse_fs_dir_checks(abs_path,cache_path);
            continue;
        }

        check_attr_from_fs(abs_path,cache_path);
    }

    attr_recheck_queue.clear();
}
static bool cache_exists(fpath abs_path,fpath cache_path) {
    // cbu::log_debug(std::format("Cache exists for '{}'",cbu::path_to_utf8(abs_path)));
    if (!std::filesystem::exists(cache_path)) {
        if (!std::filesystem::exists(abs_path)) return false;

        if (std::filesystem::is_directory(abs_path)) {
            std::filesystem::create_directories(cache_path);
            for (auto f : cbu::iterate_dir(abs_path)) {
                fpath relative = std::filesystem::relative(f,abs_path);
                if (!cache_exists(f,cache_path / relative)) return false;
            }
            return true;
        }

        return copy_attr_from_src(abs_path,cache_path);

        // std::error_code ec;
        // std::filesystem::create_directories(cache_path.parent_path());
        // std::filesystem::copy(abs_path,cache_path,ec);
        // if (ec) cbu::log_warn(std::format("copy '{}' -> '{}' failed: {}",cbu::path_to_utf8(abs_path),cbu::path_to_utf8(cache_path),ec.message()));
        // if (!std::filesystem::exists(cache_path)) return false;
    } else {
        std::lock_guard<std::mutex> lock{attr_lock};
        attr_recheck_queue[abs_path] = {
            .cache_path = cache_path
        };
    }

    return true;
}
ParserResult get_file_attr_with_cache(fpath abs,const char *rel) {
    fpath relative_path = fpath(rel + 1);
    fpath abs_path = abs / relative_path;
    fpath cache_path = cache_dir / relative_path;
    cbu::log_verbose(std::format("Getting attributes for '{}' -> '{}'",cbu::path_to_utf8(relative_path),cbu::path_to_utf8(abs_path)));

    ParserResult res{};

    res.absolute_read_path = cache_path;
    res.exists = true;
    if (!cache_exists(abs_path,cache_path)) return {.exists = false};

    std::lock_guard<std::mutex> lock{meta_lock};
    if (std::filesystem::is_regular_file(cache_path)) {
        res.is_file = true;
        res.filesize = current_meta.filesizes[abs_path];
    } else if (std::filesystem::is_directory(cache_path)) {
        res.is_directory = true;
    } else {
        res.exists = false;
    }

    return res;
}
ParserResult list_dir_with_cache(fpath abs,const char *rel,vector<string> &opts) {
    fpath relative_path = fpath(rel + 1);
    fpath abs_path = abs / relative_path;
    fpath cache_path = cache_dir / relative_path;
    cbu::log_debug(std::format("Listing directory '{}' -> '{}'",cbu::path_to_utf8(relative_path),cbu::path_to_utf8(abs_path)));

    ParserResult res{};
    res.absolute_read_path = cache_path;
    res.exists = true;
    if (!cache_exists(abs_path,cache_path)) return {.exists = false};

    if (!std::filesystem::is_directory(cache_path)) return {.exists = false};
    cbu::log_debug("Directory exists");

    for (auto &d : cbu::iterate_dir(cache_path)) {
        string n = cbu::path_to_utf8(d.filename());
        opts.push_back(n);
    }

    return res;
}
bool open_file_with_cache(fpath abs,const char *rel) {
    fpath relative_path = fpath(rel + 1);
    fpath abs_path = abs / relative_path;
    fpath cache_path = cache_dir / relative_path;
    cbu::log_debug(std::format("Opening file '{}' -> '{}'",cbu::path_to_utf8(relative_path),cbu::path_to_utf8(abs_path)));

    if (!cache_exists(abs_path,cache_path)) return false;
    if (std::filesystem::is_directory(cache_path)) return false;

    return true;
}
bool read_file_with_cache(fpath abs,const char *rel,BYTEARRAY &out) {
    fpath relative_path = fpath(rel + 1);
    fpath abs_path = abs / relative_path;
    fpath cache_path = cache_dir / relative_path;
    cbu::log_debug(std::format("Reading file '{}' -> '{}'",cbu::path_to_utf8(relative_path),cbu::path_to_utf8(abs_path)));

    out.clear();
    if (!cache_exists(abs_path,cache_path)) return false;
    if (std::filesystem::is_directory(cache_path)) return false;

    std::lock_guard<std::mutex> lock{meta_lock};
    if (!current_meta.downloaded_files.contains(abs_path)) { // assumes undownloaded file
        BYTEARRAY read = cbu::require_file_binary_path(abs_path);
        cbu::write_to_file_safe_binary(cache_path,read);
        cbu::log_debug(std::format("Downloaded file of size {} bytes",read.size()));

        out = read;
        current_meta.downloaded_files.insert(abs_path);
        save_meta();
    } else {
        out = cbu::require_file_binary_path(cache_path);
    }

    return true;
}
