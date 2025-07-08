#pragma once
#include <chrono>
#include <string>
#include <vector>

using time_point = std::chrono::system_clock::time_point;

// File type enum
enum class AssetType { Texture, Model, Sound, Font, Shader, Document, Archive, Directory, Unknown };

// File information struct
struct FileInfo {
  std::string name;           // File name (without path)
  std::string extension;      // File extension (lowercase)
  std::string full_path;      // Full path to the file
  std::string relative_path;  // Path relative to the scanned directory
  uint64_t size;              // File size in bytes
  time_point last_modified;   // Last modification time
  bool is_directory = false;  // Whether this is a directory
  AssetType type;             // Asset type enum

  FileInfo() : size(0), type(AssetType::Unknown) {}
};

// Function declarations
AssetType get_asset_type(const std::string &extension);
std::string get_asset_type_string(AssetType type);
std::vector<FileInfo> scan_directory(const std::string &root_path);
void print_file_info(const FileInfo &file);
void test_indexing();
