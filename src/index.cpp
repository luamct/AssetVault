#include "index.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <iostream>
#include <map>
#include <string>
#include <unordered_set>
#include <vector>
#include <unordered_map>

#include <windows.h>

#include "database.h"
#include "texture_manager.h"


namespace fs = std::filesystem;

// Asset type mapping based on file extensions - O(1) lookup using map
AssetType get_asset_type(const std::string& extension) {
  static const std::map<std::string, AssetType> type_map = {
    // Textures
    {".png", AssetType::Texture},
    {".jpg", AssetType::Texture},
    {".jpeg", AssetType::Texture},
    {".bmp", AssetType::Texture},
    {".tga", AssetType::Texture},
    {".dds", AssetType::Texture},
    {".hdr", AssetType::Texture},
    {".exr", AssetType::Texture},
    {".ktx", AssetType::Texture},

    // Models
    {".fbx", AssetType::Model},
    {".obj", AssetType::Model},
    {".dae", AssetType::Model},
    {".gltf", AssetType::Model},
    {".glb", AssetType::Model},
    {".ply", AssetType::Model},
    {".stl", AssetType::Model},
    {".3ds", AssetType::Model},

    // Audio
    {".wav", AssetType::Sound},
    {".mp3", AssetType::Sound},
    {".ogg", AssetType::Sound},
    {".flac", AssetType::Sound},
    {".aac", AssetType::Sound},
    {".m4a", AssetType::Sound},

    // Fonts
    {".ttf", AssetType::Font},
    {".otf", AssetType::Font},
    {".woff", AssetType::Font},
    {".woff2", AssetType::Font},
    {".eot", AssetType::Font},

    // Shaders
    {".vert", AssetType::Shader},
    {".frag", AssetType::Shader},
    {".geom", AssetType::Shader},
    {".tesc", AssetType::Shader},
    {".tese", AssetType::Shader},
    {".comp", AssetType::Shader},
    {".glsl", AssetType::Shader},
    {".hlsl", AssetType::Shader},

    // Documents
    {".txt", AssetType::Document},
    {".md", AssetType::Document},
    {".pdf", AssetType::Document},
    {".doc", AssetType::Document},
    {".docx", AssetType::Document},

    // Archives
    {".zip", AssetType::Archive},
    {".rar", AssetType::Archive},
    {".7z", AssetType::Archive},
    {".tar", AssetType::Archive},
    {".gz", AssetType::Archive},

    // Vector graphics
    {".svg", AssetType::Texture},

    // Auxiliary files (not shown in search results)
    {".mtl", AssetType::Auxiliary}
  };

  std::string ext = extension;
  std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(static_cast<int>(c)));
    });

  auto it = type_map.find(ext);
  return (it != type_map.end()) ? it->second : AssetType::Unknown;
}

// Convert AssetType enum to string for display
std::string get_asset_type_string(AssetType type) {
  switch (type) {
  case AssetType::Texture:
    return "Texture";
  case AssetType::Model:
    return "Model";
  case AssetType::Sound:
    return "Sound";
  case AssetType::Font:
    return "Font";
  case AssetType::Shader:
    return "Shader";
  case AssetType::Document:
    return "Document";
  case AssetType::Archive:
    return "Archive";
  case AssetType::Directory:
    return "Directory";
  case AssetType::Auxiliary:
    return "Auxiliary";
  case AssetType::Unknown:
    return "Unknown";
  default:
    return "NotFound";
  }
}

// Convert string back to AssetType enum (reverse of get_asset_type_string)
AssetType get_asset_type_from_string(const std::string& type_string) {
  if (type_string == "Texture")
    return AssetType::Texture;
  else if (type_string == "Model")
    return AssetType::Model;
  else if (type_string == "Sound")
    return AssetType::Sound;
  else if (type_string == "Font")
    return AssetType::Font;
  else if (type_string == "Shader")
    return AssetType::Shader;
  else if (type_string == "Document")
    return AssetType::Document;
  else if (type_string == "Archive")
    return AssetType::Archive;
  else if (type_string == "Directory")
    return AssetType::Directory;
  else if (type_string == "Auxiliary")
    return AssetType::Auxiliary;
  else if (type_string == "Unknown")
    return AssetType::Unknown;
  else
    return AssetType::Unknown; // Default fallback
}


/**
 * Helper to convert FILETIME to seconds since Jan 1, 2000. This format is a bit arbitrary,
 * choosen such that conversions are fast, the precision (seconds) and the range (2000 to 2136)
 * are enough for this application
 */
uint32_t filetime_to_seconds_since_2000(const FILETIME& ft) {
  uint64_t filetime_64 = ((uint64_t) ft.dwHighDateTime << 32) | ft.dwLowDateTime;

  // Convert to seconds since Jan 1, 1601
  uint64_t seconds_since_1601 = filetime_64 / 10000000ULL; // 100-nanosecond intervals per second

  // Convert to seconds since Jan 1, 2000
  constexpr uint64_t SECONDS_1601_TO_2000 = 12622780800ULL; // 399 years in seconds

  // Files older than 2000 are set to 0
  uint64_t seconds_since_2000 = std::max(0ULL, seconds_since_1601 - SECONDS_1601_TO_2000);

  // Clamp to uint32_t range (handles files up until year 2136)
  return (uint32_t) std::min(seconds_since_2000, (uint64_t) UINT32_MAX);
}

// Windows-only helper to get both creation and modification time using single GetFileTime call
// Returns the more recent of creation time or modification time as seconds since Jan 1, 2000
uint32_t get_max_creation_or_modification_seconds(const fs::path& path) {
  HANDLE hFile = CreateFileW(
    path.wstring().c_str(),
    GENERIC_READ,
    FILE_SHARE_READ | FILE_SHARE_WRITE,
    nullptr,
    OPEN_EXISTING,
    FILE_ATTRIBUTE_NORMAL,
    nullptr
  );

  if (hFile == INVALID_HANDLE_VALUE) {
    std::cerr << "Warning: Could not open file for time reading: " << path << std::endl;
    return 0;
  }

  FILETIME ftCreated, ftModified;
  if (!GetFileTime(hFile, &ftCreated, nullptr, &ftModified)) {
    std::cerr << "Warning: Could not get file times for: " << path << std::endl;
    CloseHandle(hFile);
    return 0;
  }

  CloseHandle(hFile);

  // Convert both times to seconds since Jan 1, 2000
  uint32_t creation_seconds = filetime_to_seconds_since_2000(ftCreated);
  uint32_t modification_seconds = filetime_to_seconds_since_2000(ftModified);

  // Return the more recent time
  return std::max(creation_seconds, modification_seconds);
}

// AssetIndexer implementation
AssetIndexer::AssetIndexer(const std::string& root_path) 
    : root_path_(root_path) {}

FileInfo AssetIndexer::process_file(const std::string& full_path) {
  // Use current time as default timestamp
  auto current_time = std::chrono::system_clock::now();
  return process_file(full_path, current_time);
}

FileInfo AssetIndexer::process_file(const std::string& full_path, const std::chrono::system_clock::time_point& timestamp) {
  FileInfo file_info;

  try {
    fs::path path(full_path);
    fs::path root(root_path_);

    // Basic file information
    file_info.full_path = full_path;
    file_info.name = path.filename().string();
    file_info.is_directory = fs::is_directory(path);

    // Relative path from root
    try {
      file_info.relative_path = fs::relative(path, root).string();
    }
    catch (const fs::filesystem_error& e) {
      // Fallback to full path if relative path calculation fails
      file_info.relative_path = full_path;
      std::cerr << "Warning: Could not calculate relative path for " << full_path << ": " << e.what() << std::endl;
    }

    if (!file_info.is_directory) {
      // File-specific information
      file_info.extension = path.extension().string();
      file_info.type = get_asset_type(file_info.extension);

      try {
        file_info.size = fs::file_size(path);

        // Get both creation and modification time using Windows API
        file_info.created_or_modified_seconds = get_max_creation_or_modification_seconds(path);

        // Store display time as modification time (for user-facing display)
        try {
          auto ftime = fs::last_write_time(path);
          auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
            ftime - fs::file_time_type::clock::now() + std::chrono::system_clock::now()
          );
          file_info.last_modified = sctp;
        }
        catch (const fs::filesystem_error& e) {
          // Fallback to provided timestamp for display
          file_info.last_modified = timestamp;
          std::cerr << "Warning: Using provided timestamp for display for " << full_path << ": " << e.what() << std::endl;
        }
      }
      catch (const fs::filesystem_error& e) {
        std::cerr << "Warning: Could not get file info for " << file_info.full_path << ": " << e.what() << std::endl;
        file_info.size = 0;
        file_info.last_modified = timestamp;
      }

      // Generate SVG thumbnail if this is an SVG file
      if (file_info.extension == ".svg" && file_info.type == AssetType::Texture) {
        TextureManager::generate_svg_thumbnail(file_info.full_path, file_info.name);
      }
    }
    else {
      // Directory-specific information
      file_info.type = AssetType::Directory;
      file_info.extension = "";
      file_info.size = 0;

      // Directories don't need timestamp tracking - we track individual files instead
      file_info.created_or_modified_seconds = 0;

      // Store display time as modification time (for user-facing display)
      try {
        auto ftime = fs::last_write_time(path);
        auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
          ftime - fs::file_time_type::clock::now() + std::chrono::system_clock::now()
        );
        file_info.last_modified = sctp;
      }
      catch (const fs::filesystem_error& e) {
        std::cerr << "Warning: Could not get modification time for directory " << file_info.full_path << ": "
          << e.what() << std::endl;
        file_info.last_modified = timestamp; // Fallback to provided timestamp
      }
    }
  }
  catch (const fs::filesystem_error& e) {
    std::cerr << "Error creating file info for " << full_path << ": " << e.what() << std::endl;
    // Return minimal file info on error
    file_info.full_path = full_path;
    file_info.name = fs::path(full_path).filename().string();
    file_info.last_modified = timestamp;
  }

  return file_info;
}

bool AssetIndexer::save_to_database(AssetDatabase& database, const FileInfo& file_info) {
  try {
    // Check if asset already exists
    auto existing_asset = database.get_asset_by_path(file_info.full_path);
    if (existing_asset.full_path.empty()) {
      // Asset doesn't exist - insert new
      database.insert_asset(file_info);
      std::cout << "Inserted: " << file_info.name << std::endl;
    }
    else {
      // Asset exists - update
      database.update_asset(file_info);
      std::cout << "Updated: " << file_info.name << std::endl;
    }
    return true;
  }
  catch (const std::exception& e) {
    std::cerr << "Error saving asset to database: " << e.what() << std::endl;
    return false;
  }
}

bool AssetIndexer::delete_from_database(AssetDatabase& database, const std::string& full_path) {
  try {
    database.delete_asset(full_path);
    std::cout << "Deleted from database: " << full_path << std::endl;
    return true;
  }
  catch (const std::exception& e) {
    std::cerr << "Error deleting asset from database: " << e.what() << std::endl;
    return false;
  }
}




// Print file information for debugging
void print_file_info(const FileInfo& file) {
  std::cout << "Name: " << file.name << '\n';
  std::cout << "  Path: " << file.relative_path << '\n';
  std::cout << "  Type: " << get_asset_type_string(file.type) << '\n';
  std::cout << "  Size: " << file.size << " bytes" << '\n';
  std::cout << "  Extension: " << file.extension << '\n';
  std::cout << "  Is Directory: " << (file.is_directory ? "Yes" : "No") << '\n';
  std::cout << "---" << '\n';
}

// Smart incremental reindexing with two-phase approach
void reindex_new_or_modified(
  AssetDatabase& database, std::vector<FileInfo>& assets, std::atomic<bool>& assets_updated,
  std::atomic<bool>& initial_scan_complete, std::atomic<bool>& initial_scan_in_progress,
  std::atomic<float>& scan_progress, std::atomic<size_t>& files_processed, std::atomic<size_t>& total_files_to_process
) {
  std::cout << "Starting smart incremental asset reindexing...\n";
  initial_scan_in_progress = true;
  scan_progress = 0.0f;
  files_processed = 0;
  total_files_to_process = 0;

  // Get current database state
  std::vector<FileInfo> db_assets = database.get_all_assets();
  std::unordered_map<std::string, FileInfo> db_map;
  for (const auto& asset : db_assets) {
    db_map[asset.full_path] = asset;
  }

  // Phase 1: Get filesystem paths (fast scan)
  std::unordered_set<std::string> current_files;
  try {
    fs::path root("assets");
    if (!fs::exists(root) || !fs::is_directory(root)) {
      std::cerr << "Error: Path does not exist or is not a directory: assets\n";
      initial_scan_complete = true;
      initial_scan_in_progress = false;
      return;
    }

    std::cout << "Scanning directory: assets\n";

    // Single pass: Get all file paths
    for (const auto& entry : fs::recursive_directory_iterator(root)) {
      try {
        current_files.insert(entry.path().string());
      }
      catch (const fs::filesystem_error& e) {
        std::cerr << "Warning: Could not access " << entry.path().string() << ": " << e.what() << '\n';
        continue;
      }
    }

    std::cout << "Found " << current_files.size() << " files and directories\n";
  }
  catch (const fs::filesystem_error& e) {
    std::cerr << "Error scanning directory: " << e.what() << '\n';
    initial_scan_complete = true;
    initial_scan_in_progress = false;
    return;
  }

  // Track what paths need reindexing
  std::vector<std::string> paths_to_insert;
  std::vector<std::string> paths_to_update;
  std::unordered_set<std::string> found_paths;

  // Compare filesystem state with database state
  for (const auto& path : current_files) {
    found_paths.insert(path);

    auto db_it = db_map.find(path);
    if (db_it == db_map.end()) {
      // File not in database - needs to be inserted
      paths_to_insert.push_back(path);
    }
    else {
      // File exists in database - check if modified
      const FileInfo& db_asset = db_it->second;

      // Skip timestamp comparison for directories - they don't need reprocessing
      if (fs::is_directory(path)) {
        // Directories are just containers; we process their contents individually
        continue;
      }

      // Get the max of creation and modification time for current file
      uint32_t current_created_or_modified = get_max_creation_or_modification_seconds(path);

      // Direct integer comparison
      if (current_created_or_modified > db_asset.created_or_modified_seconds) {
        // File has been created or modified more recently - needs to be updated
        paths_to_update.push_back(path);
      }
    }
  }

  // Find files in database that no longer exist on filesystem
  std::vector<std::string> assets_to_delete;
  for (const auto& db_asset : db_assets) {
    if (found_paths.find(db_asset.full_path) == found_paths.end()) {
      assets_to_delete.push_back(db_asset.full_path);
    }
  }

  // Set up progress tracking for expensive operations
  total_files_to_process = paths_to_insert.size() + paths_to_update.size();
  files_processed = 0;

  // Phase 2: Only do expensive processing for files that actually changed
  std::vector<FileInfo> assets_to_insert;
  std::vector<FileInfo> assets_to_update;

  // Create indexer for consistent processing
  AssetIndexer indexer("assets");

  std::cout << "Processing " << paths_to_insert.size() << " new files...\n";
  for (const std::string& path : paths_to_insert) {
    FileInfo full_info = indexer.process_file(path);
    assets_to_insert.push_back(full_info);

    // Update progress
    files_processed++;
    if (total_files_to_process > 0) {
      scan_progress = static_cast<float>(files_processed.load()) / static_cast<float>(total_files_to_process.load());
    }
  }

  std::cout << "Processing " << paths_to_update.size() << " modified files...\n";
  for (const std::string& path : paths_to_update) {
    FileInfo full_info = indexer.process_file(path);
    assets_to_update.push_back(full_info);

    // Update progress
    files_processed++;
    if (total_files_to_process > 0) {
      scan_progress = static_cast<float>(files_processed.load()) / static_cast<float>(total_files_to_process.load());
    }
  }

  // Apply changes to database
  if (!assets_to_insert.empty()) {
    std::cout << "Inserting " << assets_to_insert.size() << " new assets into database...\n";
    database.insert_assets_batch(assets_to_insert);
  }

  if (!assets_to_update.empty()) {
    std::cout << "Updating " << assets_to_update.size() << " modified assets in database...\n";
    for (const auto& asset : assets_to_update) {
      database.update_asset(asset);
    }
  }

  if (!assets_to_delete.empty()) {
    std::cout << "Removing " << assets_to_delete.size() << " deleted assets from database...\n";
    for (const auto& path : assets_to_delete) {
      database.delete_asset(path);
    }
  }

  // Always load assets from database (either updated ones or existing ones)
  assets = database.get_all_assets();
  assets_updated = true; // Trigger UI update - main thread will handle filtering

  size_t unchanged_count = current_files.size() - paths_to_insert.size() - paths_to_update.size();
  std::cout << "Reindexing completed - " << assets_to_insert.size() << " new, " << assets_to_update.size()
    << " updated, " << assets_to_delete.size() << " removed, " << unchanged_count
    << " unchanged (skipped expensive processing)\n";

  initial_scan_complete = true;
  initial_scan_in_progress = false;
  scan_progress = 1.0f;
}
