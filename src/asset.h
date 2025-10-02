#pragma once
#include <chrono>
#include <filesystem>
#include <string>
#include <map>
#include <shared_mutex>
#include <functional>

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
  std::string path;          // Full path to the file (UTF-8 with normalized separators)
  std::string relative_path; // Path relative to the configured assets root
  uint64_t size;             // File size in bytes
  time_point last_modified;  // Last modification time (system clock - for user display)
  AssetType type;            // Asset type enum

  Asset() : id(0), size(0), type(AssetType::Unknown) {}
  
  // Get the thumbnail path for this asset (primarily for 3D models)
};

// Asset type utility functions
AssetType get_asset_type(const std::string& extension);
std::string get_asset_type_string(AssetType type);
AssetType get_asset_type_from_string(const std::string& type_string);

// Early filtering helper - determines if asset should be skipped based on extension
bool should_skip_asset(const std::string& extension);

// Thread-safe wrapper for the assets map with read/write lock support
// Uses shared_mutex to allow multiple concurrent readers or single writer
class SafeAssets {
public:
    using AssetMap = std::map<std::string, Asset>;

    SafeAssets() = default;

    // Disable copy (mutex is not copyable)
    SafeAssets(const SafeAssets&) = delete;
    SafeAssets& operator=(const SafeAssets&) = delete;

    // Read-only access (returns shared_lock + const ref)
    // Usage: auto [lock, assets] = safe_assets.read();
    auto read() const -> std::pair<std::shared_lock<std::shared_mutex>, const AssetMap&> {
        return {std::shared_lock{mutex_}, assets_};
    }

    // Write access (returns unique_lock + ref)
    // Usage: auto [lock, assets] = safe_assets.write();
    auto write() -> std::pair<std::unique_lock<std::shared_mutex>, AssetMap&> {
        return {std::unique_lock{mutex_}, assets_};
    }

private:
    mutable std::shared_mutex mutex_;
    AssetMap assets_;
};
