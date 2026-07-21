#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

namespace socl{
    [[nodiscard]] std::vector<std::uint32_t> compileGlslToSpirv(
        std::string_view source,
        std::string_view sourceName = "shader.comp");
}
