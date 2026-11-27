#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

namespace tc {

struct UiAsset {
    const uint8_t* data;
    size_t size;
    const char* mime;
};

const UiAsset* find_ui_asset(const std::string& path);

} // namespace tc
