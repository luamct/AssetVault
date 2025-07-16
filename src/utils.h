#pragma once

#include <cstdint>
#include <string>

// String utility functions
std::string truncate_filename(const std::string& filename, size_t max_length = 20);
std::string to_lowercase(const std::string& str);
std::string format_display_path(const std::string& full_path);
std::string format_file_size(uint64_t size_bytes);
