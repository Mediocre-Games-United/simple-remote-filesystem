#pragma once
#include "base_types.hpp"
#include "file.hpp"

struct ParserResult {
    fpath absolute_read_path = "";
    bool exists = false;
    bool is_file = false;
    bool is_directory = false;
    size_t filesize = 0;
};
void background_checks();
void read_meta();

ParserResult get_file_attr_with_cache(fpath abs,const char *rel);
ParserResult list_dir_with_cache(fpath abs,const char *rel,vector<string> &opts);
bool open_file_with_cache(fpath abs,const char *rel);
bool read_file_with_cache(fpath abs,const char *rel,BYTEARRAY &out);
