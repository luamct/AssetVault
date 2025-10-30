#pragma once

#include <cstdint>
#include <string>
#include <ctime>
#include <filesystem>
#include <map>
#include <vector>
#include <chrono>

using TimePoint = std::chrono::steady_clock::time_point;

// Forward declarations
struct Asset;

// String utility functions
std::string truncate_filename(const std::string& filename, size_t max_length);
std::string to_lowercase(const std::string& str);
std::string trim_string(const std::string& str);

// Path utility functions
std::string normalize_path_separators(const std::string& path);
std::string get_relative_path(const std::string& full_path, const std::string& assets_directory);
std::string format_display_path(const std::string& full_path, const std::string& assets_directory);
std::string format_file_size(uint64_t size_bytes);
std::filesystem::path get_thumbnail_path(const std::string& relative_path);
void ensure_executable_working_directory();
std::vector<std::filesystem::path> list_root_directories();

std::string get_home_directory();

// Time utility functions
void safe_localtime(std::tm* tm_buf, const std::time_t* time);

// String copy utility functions
void safe_strcpy(char* dest, size_t dest_size, const char* src);

// Asset management utility functions
// Efficiently find all assets under a directory path using binary search O(log n + k)
// Returns vector of asset paths that are children of the given directory
std::vector<std::filesystem::path> find_assets_under_directory(const std::map<std::string, Asset>& assets, const std::filesystem::path& dir_path);

// Thumbnail management utility functions
// Clear all thumbnails from the configured thumbnail directory
void clear_all_thumbnails();

// Drag-and-drop utility functions
// Find related files that should be included when dragging an asset
// For example: MTL files for OBJ models, texture files for 3D models, etc.
// Returns vector of absolute file paths (always includes the main file)
std::vector<std::string> find_related_files(const Asset& asset);
