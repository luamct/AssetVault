#pragma once
#include <chrono>
#include <filesystem>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

using time_point = std::chrono::system_clock::time_point;

// SVG thumbnail constants
constexpr int SVG_THUMBNAIL_SIZE = 240;

// File type enum
enum class AssetType { Texture, Model, Sound, Font, Shader, Document, Archive, Directory, Auxiliary, Unknown };

// File information struct
struct FileInfo {
  std::string name;          // File name (without path)
  std::string extension;     // File extension (lowercase)
  std::string full_path;     // Full path to the file
  std::string relative_path; // Path relative to the scanned directory
  uint64_t size;             // File size in bytes
  time_point last_modified;  // Last modification time (system clock - for user display)
  uint32_t created_or_modified_seconds; // Max of creation/modification time as seconds since Jan 1, 2000 (for fast comparison)
  bool is_directory = false; // Whether this is a directory
  AssetType type;            // Asset type enum

  FileInfo() : size(0), type(AssetType::Unknown) {}
};

// Progress callback types
using ProgressCallback = std::function<void(size_t current, size_t total, float progress)>;


// Forward declaration
class AssetDatabase;

// Unified asset indexing class - provides consistent indexing logic
class AssetIndexer {
public:
    AssetIndexer(const std::string& root_path);
    ~AssetIndexer() = default;

    // Main indexing method - processes a single file/directory
    FileInfo process_file(const std::string& full_path);
    FileInfo process_file(const std::string& full_path, const std::chrono::system_clock::time_point& timestamp);

    // Database operations with consistent insert/update logic
    bool save_to_database(AssetDatabase& database, const FileInfo& file_info);
    bool delete_from_database(AssetDatabase& database, const std::string& full_path);

    // Utility methods
    void set_root_path(const std::string& root_path) { root_path_ = root_path; }
    const std::string& get_root_path() const { return root_path_; }

private:
    std::string root_path_;

    // Helper methods
};

// Function declarations
AssetType get_asset_type(const std::string& extension);
std::string get_asset_type_string(AssetType type);
AssetType get_asset_type_from_string(const std::string& type_string);

void reindex_new_or_modified(
  AssetDatabase& database, std::vector<FileInfo>& assets, std::atomic<bool>& assets_updated,
  std::atomic<bool>& initial_scan_complete, std::atomic<bool>& initial_scan_in_progress,
  std::atomic<float>& scan_progress, std::atomic<size_t>& files_processed, std::atomic<size_t>& total_files_to_process
);
void print_file_info(const FileInfo& file);
