#pragma once

#include <filesystem>

namespace Fonts {

// Generates a PNG thumbnail for the given font path directly to thumbnail_path.
// Throws std::runtime_error on failure.
void generate_font_thumbnail(const std::filesystem::path& font_path,
                             const std::filesystem::path& thumbnail_path);

}
