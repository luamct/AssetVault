#include "index.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <map>
#include <mutex>
#include <string>
#include <vector>

// Global map with thread safety
static std::map<std::string, AssetType> g_type_map;
static std::mutex g_type_map_mutex;
static bool g_type_map_initialized = false;

namespace fs = std::filesystem;

// Asset type mapping based on file extensions - O(1) lookup using map
AssetType get_asset_type(const std::string& extension) {
  // Thread-safe initialization
  {
    std::lock_guard<std::mutex> lock(g_type_map_mutex);
    if (!g_type_map_initialized) {
      // Textures
      g_type_map[".png"] = AssetType::Texture;
      g_type_map[".jpg"] = AssetType::Texture;
      g_type_map[".jpeg"] = AssetType::Texture;
      g_type_map[".bmp"] = AssetType::Texture;
      g_type_map[".tga"] = AssetType::Texture;
      g_type_map[".dds"] = AssetType::Texture;
      g_type_map[".hdr"] = AssetType::Texture;
      g_type_map[".exr"] = AssetType::Texture;
      g_type_map[".ktx"] = AssetType::Texture;

      // Models
      g_type_map[".fbx"] = AssetType::Model;
      g_type_map[".obj"] = AssetType::Model;
      g_type_map[".dae"] = AssetType::Model;
      g_type_map[".3ds"] = AssetType::Model;
      g_type_map[".blend"] = AssetType::Model;
      g_type_map[".max"] = AssetType::Model;
      g_type_map[".ma"] = AssetType::Model;
      g_type_map[".mb"] = AssetType::Model;
      g_type_map[".c4d"] = AssetType::Model;
      g_type_map[".gltf"] = AssetType::Model;
      g_type_map[".glb"] = AssetType::Model;
      g_type_map[".ply"] = AssetType::Model;
      g_type_map[".stl"] = AssetType::Model;
      g_type_map[".x"] = AssetType::Model;
      g_type_map[".iqm"] = AssetType::Model;
      g_type_map[".iqe"] = AssetType::Model;

      // Audio
      g_type_map[".wav"] = AssetType::Sound;
      g_type_map[".mp3"] = AssetType::Sound;
      g_type_map[".ogg"] = AssetType::Sound;
      g_type_map[".flac"] = AssetType::Sound;
      g_type_map[".aac"] = AssetType::Sound;
      g_type_map[".m4a"] = AssetType::Sound;

      // Fonts
      g_type_map[".ttf"] = AssetType::Font;
      g_type_map[".otf"] = AssetType::Font;
      g_type_map[".woff"] = AssetType::Font;
      g_type_map[".woff2"] = AssetType::Font;
      g_type_map[".eot"] = AssetType::Font;

      // Shaders
      g_type_map[".vert"] = AssetType::Shader;
      g_type_map[".frag"] = AssetType::Shader;
      g_type_map[".geom"] = AssetType::Shader;
      g_type_map[".tesc"] = AssetType::Shader;
      g_type_map[".tese"] = AssetType::Shader;
      g_type_map[".comp"] = AssetType::Shader;
      g_type_map[".glsl"] = AssetType::Shader;
      g_type_map[".hlsl"] = AssetType::Shader;

      // Documents
      g_type_map[".txt"] = AssetType::Document;
      g_type_map[".md"] = AssetType::Document;
      g_type_map[".pdf"] = AssetType::Document;
      g_type_map[".doc"] = AssetType::Document;
      g_type_map[".docx"] = AssetType::Document;

      // Archives
      g_type_map[".zip"] = AssetType::Archive;
      g_type_map[".rar"] = AssetType::Archive;
      g_type_map[".7z"] = AssetType::Archive;
      g_type_map[".tar"] = AssetType::Archive;
      g_type_map[".gz"] = AssetType::Archive;

      // Auxiliary files (not shown in search results)
      g_type_map[".mtl"] = AssetType::Auxiliary;

      g_type_map_initialized = true;
    }
  }

  std::string ext = extension;
  std::transform(ext.begin(), ext.end(), ext.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(static_cast<int>(c))); });

  auto it = g_type_map.find(ext);
  return (it != g_type_map.end()) ? it->second : AssetType::Unknown;
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
    return "Unknown";
  }
}

// Recursively scan directory and collect file information
std::vector<FileInfo> scan_directory(const std::string& root_path) {
  std::vector<FileInfo> files;

  try {
    fs::path root(root_path);
    if (!fs::exists(root) || !fs::is_directory(root)) {
      std::cerr << "Error: Path does not exist or is not a directory: " << root_path << '\n';
      return files;
    }

    std::cout << "Scanning directory: " << root_path << '\n';

    for (const auto& entry : fs::recursive_directory_iterator(root)) {
      FileInfo file_info;

      // Basic file information
      file_info.full_path = entry.path().string();
      file_info.name = entry.path().filename().string();
      file_info.is_directory = entry.is_directory();

      // Relative path from root
      file_info.relative_path = fs::relative(entry.path(), root).string();

      if (!file_info.is_directory) {
        // File-specific information
        file_info.extension = entry.path().extension().string();
        file_info.type = get_asset_type(file_info.extension);

        try {
          file_info.size = fs::file_size(entry.path());
          // Convert file_time_type to system_clock::time_point (portable way)
          auto ftime = fs::last_write_time(entry.path());
          auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
              ftime - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
          file_info.last_modified = sctp;
        } catch (const fs::filesystem_error& e) {
          std::cerr << "Warning: Could not get file info for " << file_info.full_path << ": " << e.what() << '\n';
          file_info.size = 0;
        }
      } else {
        file_info.type = AssetType::Directory;
        file_info.extension = "";
        file_info.size = 0;
      }

      files.push_back(file_info);
    }

    std::cout << "Found " << files.size() << " files and directories\n";

  } catch (const fs::filesystem_error& e) {
    std::cerr << "Error scanning directory: " << e.what() << '\n';
  }

  return files;
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

// Test function to demonstrate the indexing
void test_indexing() {
  // Hardcoded path for testing - change this to your assets folder
  std::string scan_path = "assets";

  std::cout << "Starting file indexing...\n";
  auto start_time = std::chrono::high_resolution_clock::now();

  std::vector<FileInfo> files = scan_directory(scan_path);

  auto end_time = std::chrono::high_resolution_clock::now();
  auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

  std::cout << "\nIndexing completed in " << duration.count() << "ms\n";
  std::cout << "Total files found: " << files.size() << '\n';

  // Print first 10 files as a sample
  std::cout << "\nSample files:" << '\n';
  int count = 0;
  for (const auto& file : files) {
    if (count++ >= 10)
      break;
    print_file_info(file);
  }

  // Count files by type
  std::map<std::string, int> type_count;
  for (const auto& file : files) {
    type_count[get_asset_type_string(file.type)]++;
  }

  std::cout << "\nFiles by type:" << '\n';
  for (const auto& pair : type_count) {
    std::cout << "  " << pair.first << ": " << pair.second << '\n';
  }
}
