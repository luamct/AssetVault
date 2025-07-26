#pragma once
#include <chrono>
#include <string>

using time_point = std::chrono::system_clock::time_point;

// SVG thumbnail constants
constexpr int SVG_THUMBNAIL_SIZE = 240;

// Asset type enum
enum class AssetType { Texture, Model, Sound, Font, Shader, Document, Archive, Directory, Auxiliary, Unknown };

// Asset information struct (renamed from FileInfo)
struct Asset {
  std::string name;          // File name (without path)
  std::string extension;     // File extension (lowercase)
  std::string full_path;     // Full path to the file
  std::string relative_path; // Path relative to the scanned directory
  uint64_t size;             // File size in bytes
  time_point last_modified;  // Last modification time (system clock - for user display)
  uint32_t created_or_modified_seconds; // Max of creation/modification time as seconds since Jan 1, 2000 (for fast comparison)
  bool is_directory = false; // Whether this is a directory
  AssetType type;            // Asset type enum

  Asset() : size(0), type(AssetType::Unknown) {}
};

// Asset type utility functions
AssetType get_asset_type(const std::string& extension);
std::string get_asset_type_string(AssetType type);
AssetType get_asset_type_from_string(const std::string& type_string);
