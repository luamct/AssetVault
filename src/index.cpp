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

#include "database.h"

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

// Lightweight directory scan - only gets paths and modification times (super fast, no progress tracking)
std::unordered_map<std::string, LightFileInfo> quick_scan(const std::string& root_path) {
  std::unordered_map<std::string, LightFileInfo> files;

  try {
    fs::path root(root_path);
    if (!fs::exists(root) || !fs::is_directory(root)) {
      std::cerr << "Error: Path does not exist or is not a directory: " << root_path << '\n';
      return files;
    }

    std::cout << "Quick scanning directory: " << root_path << '\n';

    // Single pass: Process files
    for (const auto& entry : fs::recursive_directory_iterator(root)) {
      LightFileInfo light_info;

      // Only get essential information - no expensive operations
      light_info.full_path = entry.path().string();
      light_info.is_directory = entry.is_directory();

      try {
        // Get modification time for both files and directories
        auto ftime = fs::last_write_time(entry.path());
        auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
          ftime - fs::file_time_type::clock::now() + std::chrono::system_clock::now()
        );
        light_info.last_modified = sctp;
      } catch (const fs::filesystem_error& e) {
        std::cerr << "Warning: Could not get modification time for " << light_info.full_path << ": " << e.what()
                  << '\n';
        // Skip files we can't access
        continue;
      }

      files[light_info.full_path] = light_info;
    }

    std::cout << "Quick scan found " << files.size() << " files and directories\n";

  } catch (const fs::filesystem_error& e) {
    std::cerr << "Error quick scanning directory: " << e.what() << '\n';
  }

  return files;
}

// Index a file - create full FileInfo with expensive operations (will include image resizing, 3D previews, etc.)
FileInfo index_file(const std::string& full_path, const std::string& root_path) {
  FileInfo file_info;

  try {
    fs::path path(full_path);
    fs::path root(root_path);

    // Basic file information
    file_info.full_path = full_path;
    file_info.name = path.filename().string();
    file_info.is_directory = fs::is_directory(path);

    // Relative path from root
    file_info.relative_path = fs::relative(path, root).string();

    if (!file_info.is_directory) {
      // File-specific information (expensive operations)
      file_info.extension = path.extension().string();
      file_info.type = get_asset_type(file_info.extension);

      try {
        file_info.size = fs::file_size(path);
        // Convert file_time_type to system_clock::time_point
        auto ftime = fs::last_write_time(path);
        auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
          ftime - fs::file_time_type::clock::now() + std::chrono::system_clock::now()
        );
        file_info.last_modified = sctp;
      } catch (const fs::filesystem_error& e) {
        std::cerr << "Warning: Could not get file info for " << file_info.full_path << ": " << e.what() << '\n';
        file_info.size = 0;
      }
    } else {
      // Directory-specific information
      file_info.type = AssetType::Directory;
      file_info.extension = "";
      file_info.size = 0;

      try {
        // Get modification time for directory
        auto ftime = fs::last_write_time(path);
        auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
          ftime - fs::file_time_type::clock::now() + std::chrono::system_clock::now()
        );
        file_info.last_modified = sctp;
      } catch (const fs::filesystem_error& e) {
        std::cerr << "Warning: Could not get modification time for directory " << file_info.full_path << ": "
                  << e.what() << '\n';
        file_info.last_modified = std::chrono::system_clock::now(); // Fallback
      }
    }
  } catch (const fs::filesystem_error& e) {
    std::cerr << "Error creating file info for " << full_path << ": " << e.what() << '\n';
  }

  return file_info;
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

  // Phase 1: Quick scan to get only paths and modification times (fast)
  std::unordered_map<std::string, LightFileInfo> current_files = quick_scan("assets");

  // Track what paths need reindexing
  std::vector<std::string> paths_to_insert;
  std::vector<std::string> paths_to_update;
  std::unordered_set<std::string> found_paths;

  // Compare filesystem state with database state (fast comparison)
  for (const auto& [path, light_info] : current_files) {
    found_paths.insert(path);

    auto db_it = db_map.find(path);
    if (db_it == db_map.end()) {
      // File not in database - needs to be inserted
      paths_to_insert.push_back(path);
      std::cout << "New file: " << light_info.full_path << std::endl;
    } else {
      // File exists in database - check if modified
      const FileInfo& db_asset = db_it->second;
      if (light_info.last_modified > db_asset.last_modified) {
        // File has been modified - needs to be updated
        paths_to_update.push_back(path);
      }
      // If timestamps match, no action needed
    }
  }

  // Find files in database that no longer exist on filesystem
  std::vector<std::string> assets_to_delete;
  for (const auto& db_asset : db_assets) {
    if (found_paths.find(db_asset.full_path) == found_paths.end()) {
      assets_to_delete.push_back(db_asset.full_path);
      std::cout << "Deleted file: " << db_asset.name << std::endl;
    }
  }

  // Set up progress tracking for expensive operations
  total_files_to_process = paths_to_insert.size() + paths_to_update.size();
  files_processed = 0;

  // Phase 2: Only do expensive processing for files that actually changed
  std::vector<FileInfo> assets_to_insert;
  std::vector<FileInfo> assets_to_update;

  std::cout << "Processing " << paths_to_insert.size() << " new files...\n";
  for (const std::string& path : paths_to_insert) {
    FileInfo full_info = index_file(path, "assets");
    assets_to_insert.push_back(full_info);

    // Update progress
    files_processed++;
    if (total_files_to_process > 0) {
      scan_progress = static_cast<float>(files_processed.load()) / static_cast<float>(total_files_to_process.load());
    }
  }

  std::cout << "Processing " << paths_to_update.size() << " modified files...\n";
  for (const std::string& path : paths_to_update) {
    FileInfo full_info = index_file(path, "assets");
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
