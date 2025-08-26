#pragma once
#include <chrono>
#include <filesystem>
#include <string>

using time_point = std::chrono::system_clock::time_point;

// SVG thumbnail constants
constexpr int SVG_THUMBNAIL_SIZE = 240;

// Asset type enum
enum class AssetType { _2D, _3D, Audio, Font, Shader, Document, Archive, Directory, Auxiliary, Unknown };

// Asset information struct
struct Asset {
  uint32_t id = 0;           // Unique database ID (0 means not yet assigned)
  std::string name;          // File name (without path)
  std::string extension;     // File extension (lowercase)
  std::filesystem::path full_path; // Full path to the file
  uint64_t size;             // File size in bytes
  time_point last_modified;  // Last modification time (system clock - for user display)
  bool is_directory = false; // Whether this is a directory
  AssetType type;            // Asset type enum

  Asset() : id(0), size(0), type(AssetType::Unknown) {}
};

// Asset type utility functions
AssetType get_asset_type(const std::string& extension);
std::string get_asset_type_string(AssetType type);
AssetType get_asset_type_from_string(const std::string& type_string);

// Early filtering helper - determines if asset should be skipped based on extension
bool should_skip_asset(const std::string& extension);
