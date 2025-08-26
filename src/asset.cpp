#include "asset.h"

#include <algorithm>
#include <map>
#include <string>

// Asset type mapping based on file extensions - O(1) lookup using map
AssetType get_asset_type(const std::string& extension) {

  static const std::map<std::string, AssetType> type_map = {
    // Textures
    {".png", AssetType::_2D},
    {".jpg", AssetType::_2D},
    {".jpeg", AssetType::_2D},
    {".gif", AssetType::_2D},
    {".bmp", AssetType::_2D},
    {".tga", AssetType::_2D},
    {".dds", AssetType::_2D},
    {".hdr", AssetType::_2D},
    {".exr", AssetType::_2D},
    {".ktx", AssetType::_2D},

    // Models
    {".fbx", AssetType::_3D},
    {".obj", AssetType::_3D},
    {".dae", AssetType::_3D},
    {".gltf", AssetType::_3D},
    {".glb", AssetType::_3D},
    {".ply", AssetType::_3D},
    {".stl", AssetType::_3D},
    {".3ds", AssetType::_3D},

    // Audio
    {".wav", AssetType::Audio},
    {".mp3", AssetType::Audio},
    {".ogg", AssetType::Audio},
    {".flac", AssetType::Audio},
    {".aac", AssetType::Audio},
    {".m4a", AssetType::Audio},

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
    {".svg", AssetType::_2D},

    // Auxiliary files (not shown in search results)
    {".mtl", AssetType::Auxiliary},

    // Temporary and backup files
    {".log", AssetType::Auxiliary},
    {".cache", AssetType::Auxiliary},
    {".tmp", AssetType::Auxiliary},
    {".temp", AssetType::Auxiliary},
    {".bak", AssetType::Auxiliary},
    {".backup", AssetType::Auxiliary}
  };

  std::string ext = extension;
  std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(static_cast<int>(c)));
    });

  auto it = type_map.find(ext);
  return (it != type_map.end()) ? it->second : AssetType::Unknown;
}

// Convert AssetType enum to string for display and database storage (lowercase)
std::string get_asset_type_string(AssetType type) {
  switch (type) {
  case AssetType::_2D:
    return "2d";
  case AssetType::_3D:
    return "3d";
  case AssetType::Audio:
    return "audio";
  case AssetType::Font:
    return "font";
  case AssetType::Shader:
    return "shader";
  case AssetType::Document:
    return "document";
  case AssetType::Archive:
    return "archive";
  case AssetType::Directory:
    return "directory";
  case AssetType::Auxiliary:
    return "auxiliary";
  case AssetType::Unknown:
    return "unknown";
  default:
    return "notfound";
  }
}

// Convert lowercase string back to AssetType enum (assumes input is already lowercase)
AssetType get_asset_type_from_string(const std::string& type_string) {
  static const std::map<std::string, AssetType> type_map = {
    {"2d", AssetType::_2D},
    {"3d", AssetType::_3D},
    {"audio", AssetType::Audio},
    {"font", AssetType::Font},
    {"shader", AssetType::Shader},
    {"document", AssetType::Document},
    {"archive", AssetType::Archive},
    {"directory", AssetType::Directory},
    {"auxiliary", AssetType::Auxiliary},
    {"unknown", AssetType::Unknown}
  };

  auto it = type_map.find(type_string);
  return (it != type_map.end()) ? it->second : AssetType::Unknown;
}

// Early filtering helper - determines if asset should be skipped based on extension
// Returns true for asset types that should be ignored (Auxiliary, Unknown, Document, Directory)
bool should_skip_asset(const std::string& extension) {
  AssetType type = get_asset_type(extension);
  
  // Skip ignored asset types (same as Config::IGNORED_ASSET_TYPES)
  return type == AssetType::Auxiliary || 
         type == AssetType::Unknown || 
         type == AssetType::Document || 
         type == AssetType::Directory;
}
