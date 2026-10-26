#pragma once
#include "base_types.hpp"

struct ParserResult {
    fpath absolute_read_path = "";
    bool exists = false;
    bool is_file = false;
    bool is_directory = false;
    size_t filesize = 0;
};
ParserResult get_file_attr_with_cache(fpath abs,const char *rel);
ParserResult list_dir_with_cache(fpath abs,const char *rel,vector<string> &opts);
