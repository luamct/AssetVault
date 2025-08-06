#pragma once

#include <cstdint>
#include <string>
#include <ctime>

// String utility functions
std::string truncate_filename(const std::string& filename, size_t max_length = 20);
std::string to_lowercase(const std::string& str);

// Path utility functions
std::string normalize_path_separators(const std::string& path);
std::string get_relative_asset_path(const std::string& full_path);
std::string format_display_path(const std::string& full_path);
std::string format_file_size(uint64_t size_bytes);

// Time utility functions
void safe_localtime(std::tm* tm_buf, const std::time_t* time);

// String copy utility functions
void safe_strcpy(char* dest, size_t dest_size, const char* src);
