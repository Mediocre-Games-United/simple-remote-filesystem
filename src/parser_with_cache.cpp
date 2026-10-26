#include "parser_with_cache.hpp"
#include "file.hpp"
#include "logger.hpp"
#include <filesystem>
#include <format>

static fpath cache_dir = cbu::cache_path();

static bool cache_exists(fpath abs_path,fpath cache_path) {
    if (!std::filesystem::exists(cache_path)) {
        if (!std::filesystem::exists(abs_path)) return false;

        std::error_code ec;
        std::filesystem::copy(abs_path,cache_path,ec);
        if (ec) cbu::log_warn(std::format("copy '{}' -> '{}' failed: {}",cbu::path_to_utf8(abs_path),cbu::path_to_utf8(cache_path),ec.message()));
        if (!std::filesystem::exists(cache_path)) return false;
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

    if (std::filesystem::is_regular_file(cache_path)) {
        res.is_file = true;
        res.filesize = std::filesystem::file_size(cache_path);
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
