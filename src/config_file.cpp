#include "config_file.hpp"
#include "binary.hpp"
#include "file.hpp"

Config current_config{};


class ConfigFileFormatV1 : public cbu::BinaryFileVersion {
public:
    cbu::BinaryFileSection *get_sections() override {
        return new cbu::MainBinaryFileSection({
            new cbu::StringBinarySection([](void *o,auto value) {
                ((Config*) o)->mount_path = value;
            },[](void *o) -> string {
                return cbu::path_to_utf8(((Config*) o)->mount_path);
            }),
            new cbu::StringBinarySection([](void *o,auto value) {
                ((Config*) o)->rclone_path = value;
            },[](void *o) -> string {
                return cbu::path_to_utf8(((Config*) o)->rclone_path);
            })
        });
    }
};
class ConfigFileFormat : public cbu::BinaryFileFormat {
public:
    ConfigFileFormat() : BinaryFileFormat({new ConfigFileFormatV1()},"cfg") {}
};

static fpath cpath = cbu::resolve_path("user://config.cfg");
void update_config() {
    auto fmt = ConfigFileFormat();

    fmt.load_object_to_buffer(&current_config);
    fmt.save_buffer_to_file(cpath);
}
void read_config() {
    auto fmt = ConfigFileFormat();

    fmt.load_file_to_buffer(cpath);
    fmt.load_buffer_to_object(&current_config);
}
