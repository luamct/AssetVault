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

// SVG thumbnail generation dependencies
#include "nanosvg.h"
#include "nanosvgrast.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"


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

// Lightweight directory scan - only gets paths and native modification times
void quick_scan(const std::string& root_path, std::unordered_map<std::string, fs::file_time_type>& files) {
  try {
    fs::path root(root_path);
    if (!fs::exists(root) || !fs::is_directory(root)) {
      std::cerr << "Error: Path does not exist or is not a directory: " << root_path << '\n';
      return;
    }

    std::cout << "Quick scanning directory: " << root_path << '\n';

    // Single pass: Get paths and native file times (no expensive conversion)
    for (const auto& entry : fs::recursive_directory_iterator(root)) {
      std::string full_path = entry.path().string();

      try {
        // Store native file time directly - no conversion needed for comparison
        auto ftime = fs::last_write_time(entry.path());
        files[full_path] = ftime;
      }
      catch (const fs::filesystem_error& e) {
        std::cerr << "Warning: Could not get modification time for " << full_path << ": " << e.what()
          << '\n';
        // Skip files we can't access - don't add to map
        continue;
      }
    }

    std::cout << "Quick scan found " << files.size() << " files and directories\n";

  }
  catch (const fs::filesystem_error& e) {
    std::cerr << "Error quick scanning directory: " << e.what() << '\n';
  }
}

// AssetIndexer implementation
AssetIndexer::AssetIndexer(const std::string& root_path) : root_path_(root_path) {}

FileInfo AssetIndexer::process_file(const std::string& full_path) {
  // Use current time as default timestamp
  auto current_time = std::chrono::system_clock::now();
  return process_file(full_path, current_time);
}

FileInfo AssetIndexer::process_file(const std::string& full_path, const std::chrono::system_clock::time_point& timestamp) {
  return create_file_info(full_path, timestamp);
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

// Generate PNG thumbnail from SVG file during indexing
bool AssetIndexer::generate_svg_thumbnail(const std::string& svg_path, const std::string& filename) {
  constexpr int MAX_THUMBNAIL_SIZE = SVG_THUMBNAIL_SIZE;

  // Create thumbnails directory if it doesn't exist
  fs::path thumbnails_dir = "thumbnails";
  if (!fs::exists(thumbnails_dir)) {
    try {
      fs::create_directory(thumbnails_dir);
    }
    catch (const fs::filesystem_error& e) {
      std::cerr << "Failed to create thumbnails directory: " << e.what() << std::endl;
      return false;
    }
  }

  // Parse SVG file
  NSVGimage* image = nsvgParseFromFile(svg_path.c_str(), "px", 96.0f);
  if (!image) {
    std::cerr << "Failed to parse SVG: " << svg_path << std::endl;
    return false;
  }

  if (image->width <= 0 || image->height <= 0) {
    std::cerr << "Invalid SVG dimensions: " << image->width << "x" << image->height << std::endl;
    nsvgDelete(image);
    return false;
  }

  // Calculate scale factor and actual output dimensions
  // The largest dimension should be MAX_THUMBNAIL_SIZE, maintaining aspect ratio
  float scale_x = static_cast<float>(MAX_THUMBNAIL_SIZE) / image->width;
  float scale_y = static_cast<float>(MAX_THUMBNAIL_SIZE) / image->height;
  float scale = std::min(scale_x, scale_y);

  // Calculate actual output dimensions maintaining aspect ratio
  int output_width = static_cast<int>(image->width * scale);
  int output_height = static_cast<int>(image->height * scale);

  // Create rasterizer
  NSVGrasterizer* rast = nsvgCreateRasterizer();
  if (!rast) {
    std::cerr << "Failed to create SVG rasterizer for: " << svg_path << std::endl;
    nsvgDelete(image);
    return false;
  }

  // Allocate image buffer with actual dimensions (RGBA format)
  unsigned char* img_data = static_cast<unsigned char*>(malloc(output_width * output_height * 4));
  if (!img_data) {
    std::cerr << "Failed to allocate image buffer for: " << svg_path << std::endl;
    nsvgDeleteRasterizer(rast);
    nsvgDelete(image);
    return false;
  }

  // Clear buffer to transparent
  memset(img_data, 0, output_width * output_height * 4);

  // Rasterize SVG at calculated scale with proper dimensions
  nsvgRasterize(rast, image, 0, 0, scale, img_data, output_width, output_height, output_width * 4);

  // Generate output PNG path
  fs::path svg_file_path(filename);
  std::string png_filename = svg_file_path.stem().string() + ".png";
  fs::path output_path = thumbnails_dir / png_filename;

  // Write PNG file with actual dimensions
  int result = stbi_write_png(output_path.string().c_str(), output_width, output_height, 4, img_data, output_width * 4);

  if (!result) {
    std::cerr << "Failed to write PNG thumbnail: " << output_path.string() << std::endl;
  }

  // Cleanup
  free(img_data);
  nsvgDeleteRasterizer(rast);
  nsvgDelete(image);

  return result != 0;
}

// Create comprehensive file info
FileInfo AssetIndexer::create_file_info(const std::string& full_path, const std::chrono::system_clock::time_point& timestamp) {
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

        // Use provided timestamp or get from filesystem
        try {
          auto ftime = fs::last_write_time(path);
          // Store both formats - native file time for fast comparison, system time for display
          file_info.last_modified_filetime = ftime;
          auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
            ftime - fs::file_time_type::clock::now() + std::chrono::system_clock::now()
          );
          file_info.last_modified = sctp;
        }
        catch (const fs::filesystem_error& e) {
          // Fallback to provided timestamp
          file_info.last_modified = timestamp;
          // For native file time, use a default epoch value when filesystem access fails
          file_info.last_modified_filetime = fs::file_time_type{};
          std::cerr << "Warning: Using provided timestamp for " << full_path << ": " << e.what() << std::endl;
        }
      }
      catch (const fs::filesystem_error& e) {
        std::cerr << "Warning: Could not get file info for " << file_info.full_path << ": " << e.what() << std::endl;
        file_info.size = 0;
        file_info.last_modified = timestamp;
      }

      // Generate SVG thumbnail if this is an SVG file
      if (file_info.extension == ".svg" && file_info.type == AssetType::Texture) {
        generate_svg_thumbnail(file_info.full_path, file_info.name);
      }
    }
    else {
      // Directory-specific information
      file_info.type = AssetType::Directory;
      file_info.extension = "";
      file_info.size = 0;

      try {
        // Get modification time for directory
        auto ftime = fs::last_write_time(path);
        // Store both formats - native file time for fast comparison, system time for display
        file_info.last_modified_filetime = ftime;
        auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
          ftime - fs::file_time_type::clock::now() + std::chrono::system_clock::now()
        );
        file_info.last_modified = sctp;
      }
      catch (const fs::filesystem_error& e) {
        std::cerr << "Warning: Could not get modification time for directory " << file_info.full_path << ": "
          << e.what() << std::endl;
        file_info.last_modified = timestamp; // Fallback to provided timestamp
        file_info.last_modified_filetime = fs::file_time_type{}; // Default epoch value
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

  // Phase 1: Quick scan to get only paths and native modification times (fast)
  std::unordered_map<std::string, fs::file_time_type> current_files;
  quick_scan("assets", current_files);

  // Track what paths need reindexing
  std::vector<std::string> paths_to_insert;
  std::vector<std::string> paths_to_update;
  std::unordered_set<std::string> found_paths;

  // Compare filesystem state with database state (fast comparison)
  for (const auto& [path, file_time] : current_files) {
    found_paths.insert(path);

    auto db_it = db_map.find(path);
    if (db_it == db_map.end()) {
      // File not in database - needs to be inserted
      paths_to_insert.push_back(path);
      std::cout << "New file: " << path << std::endl;
    }
    else {
      // File exists in database - check if modified using native file times (fast comparison)
      const FileInfo& db_asset = db_it->second;

      // Direct comparison using native file_time_type - no expensive conversion needed
      if (file_time > db_asset.last_modified_filetime) {
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
