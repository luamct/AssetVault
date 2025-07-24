#include "asset.h"

#include <algorithm>
#include <map>
#include <string>

// Asset type mapping based on file extensions - O(1) lookup using map
AssetType get_asset_type(const std::string& extension) {

  static const std::map<std::string, AssetType> type_map = {
    // Textures
    {".png", AssetType::Texture},
    {".jpg", AssetType::Texture},
    {".jpeg", AssetType::Texture},
    {".gif", AssetType::Texture},
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
