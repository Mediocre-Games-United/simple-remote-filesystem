#pragma once

#include "base_types.hpp"

struct Config {
    fpath rclone_path = "";
    fpath mount_path = "";

    operator bool() {
        return !rclone_path.empty() and !mount_path.empty();
    }
};
extern Config current_config;

void update_config();
void read_config();
