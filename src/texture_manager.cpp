#include "texture_manager.h"

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <sstream>

#include <glad/glad.h>
#include <stb_image.h>

// SVG support
#include "nanosvg.h"
#include "nanosvgrast.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include "index.h" // For SVG_THUMBNAIL_SIZE

TextureManager::TextureManager()
  : default_texture_(0), preview_texture_(0), preview_depth_texture_(0),
  preview_framebuffer_(0), preview_shader_(0), preview_initialized_(false) {
}

TextureManager::~TextureManager() {
  cleanup();
}

bool TextureManager::initialize() {
  // Load default texture
  default_texture_ = load_texture("images/texture.png");
  if (default_texture_ == 0) {
    std::cerr << "Failed to load default texture\n";
    return false;
  }

  // Load type-specific textures
  load_type_textures();

  std::cout << "TextureManager initialized successfully\n";
  return true;
}

void TextureManager::cleanup() {
  cleanup_all_textures();
  cleanup_preview_system();
}

void TextureManager::cleanup_all_textures() {
  // Clean up texture cache
  for (auto& entry : texture_cache_) {
    if (entry.second.texture_id != 0) {
      glDeleteTextures(1, &entry.second.texture_id);
    }
  }
  texture_cache_.clear();

  // Clean up default texture
  if (default_texture_ != 0) {
    glDeleteTextures(1, &default_texture_);
    default_texture_ = 0;
  }

  // Clean up type-specific texture icons
  for (auto& [type, texture_id] : type_icons_) {
    if (texture_id != 0) {
      glDeleteTextures(1, &texture_id);
    }
  }
  type_icons_.clear();
}

unsigned int TextureManager::load_texture(const char* filename) {
  int width, height, channels;
  unsigned char* data = stbi_load(filename, &width, &height, &channels, 4);
  if (!data) {
    std::cerr << "Failed to load texture: " << filename << '\n';
    return 0;
  }

  unsigned int texture_id;
  glGenTextures(1, &texture_id);
  glBindTexture(GL_TEXTURE_2D, texture_id);

  // Set texture parameters for pixel art (nearest neighbor filtering)
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

  // Upload texture data
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);

  stbi_image_free(data);
  return texture_id;
}

unsigned int TextureManager::load_svg_texture(
  const char* filename, int target_width, int target_height) {
  std::cout << "Loading SVG: " << filename << std::endl;

  // Parse SVG directly from file like the nanosvg examples do
  NSVGimage* image = nsvgParseFromFile(filename, "px", 96.0f);
  if (!image) {
    std::cerr << "Failed to parse SVG: " << filename << '\n';
    return 0;
  }

  std::cout << "SVG parsed successfully. Original size: " << image->width << "x" << image->height << std::endl;

  if (image->width <= 0 || image->height <= 0) {
    std::cerr << "Invalid SVG dimensions: " << image->width << "x" << image->height << std::endl;
    nsvgDelete(image);
    return 0;
  }

  // Use target dimensions for rasterization to match thumbnail size
  int w = target_width;
  int h = target_height;

  // Calculate scale factor to fit SVG into target dimensions while preserving aspect ratio
  float scale_x = static_cast<float>(target_width) / image->width;
  float scale_y = static_cast<float>(target_height) / image->height;
  float scale = std::min(scale_x, scale_y);

  std::cout << "Rasterizing at target size: " << w << "x" << h << " (scale: " << scale << ")" << std::endl;

  if (w <= 0 || h <= 0) {
    std::cerr << "Invalid raster dimensions: " << w << "x" << h << std::endl;
    nsvgDelete(image);
    return 0;
  }

  // Create rasterizer
  NSVGrasterizer* rast = nsvgCreateRasterizer();
  if (!rast) {
    std::cerr << "Failed to create SVG rasterizer for: " << filename << '\n';
    nsvgDelete(image);
    return 0;
  }

  // Allocate image buffer like the nanosvg examples do
  unsigned char* img_data = static_cast<unsigned char*>(malloc(w * h * 4));
  if (!img_data) {
    std::cerr << "Failed to allocate image buffer for: " << filename << '\n';
    nsvgDeleteRasterizer(rast);
    nsvgDelete(image);
    return 0;
  }

  // Rasterize SVG at calculated scale to fit target dimensions
  nsvgRasterize(rast, image, 0, 0, scale, img_data, w, h, w * 4);

  // Create OpenGL texture
  unsigned int texture_id;
  glGenTextures(1, &texture_id);
  glBindTexture(GL_TEXTURE_2D, texture_id);

  // Set texture parameters (use linear filtering for SVG)
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

  // Upload texture data at original size
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, img_data);

  std::cout << "OpenGL texture created successfully with ID: " << texture_id << std::endl;

  // Cleanup
  free(img_data);
  nsvgDeleteRasterizer(rast);
  nsvgDelete(image);

  return texture_id;
}

void TextureManager::load_type_textures() {
  const std::unordered_map<AssetType, const char*> texture_paths = {
      {AssetType::Texture, "images/texture.png"},
      {AssetType::Model, "images/model.png"},
      {AssetType::Sound, "images/sound.png"},
      {AssetType::Font, "images/font.png"},
      {AssetType::Shader, "images/document.png"},
      {AssetType::Document, "images/document.png"},
      {AssetType::Archive, "images/document.png"},
      {AssetType::Directory, "images/folder.png"},
      {AssetType::Auxiliary, "images/unknown.png"},
      {AssetType::Unknown, "images/unknown.png"}
  };

  for (const auto& [type, path] : texture_paths) {
    unsigned int texture_id = load_texture(path);
    type_icons_[type] = texture_id;
    if (texture_id == 0) {
      std::cerr << "Failed to load type texture: " << path << '\n';
    }
  }
}

unsigned int TextureManager::get_asset_texture(const FileInfo& asset) {
  // For non-texture assets, return type-specific icon
  if (asset.type != AssetType::Texture) {
    auto it = type_icons_.find(asset.type);
    if (it != type_icons_.end()) {
      return it->second;
    }
    return default_texture_;
  }

  // Check if texture is already cached
  auto it = texture_cache_.find(asset.full_path);
  if (it != texture_cache_.end()) {
    return it->second.texture_id;
  }

  // Check if file exists before attempting to load (defensive check for deleted files)
  if (!std::filesystem::exists(asset.full_path)) {
    // Return type icon for missing texture files (no error spam)
    auto icon_it = type_icons_.find(asset.type);
    return (icon_it != type_icons_.end()) ? icon_it->second : default_texture_;
  }

  unsigned int texture_id = 0;
  int width = 0, height = 0;

  // Check if this is an SVG file
  if (asset.extension == ".svg") {
    // Load cached PNG thumbnail - should always exist if properly indexed
    std::filesystem::path asset_path(asset.full_path);
    std::string png_filename = asset_path.stem().string() + ".png";
    std::filesystem::path thumbnail_path = std::filesystem::path("thumbnails") / png_filename;

    if (std::filesystem::exists(thumbnail_path)) {
      // Load cached PNG thumbnail
      texture_id = load_texture(thumbnail_path.string().c_str());
      if (texture_id != 0) {
        // Get thumbnail dimensions
        int channels;
        unsigned char* data = stbi_load(thumbnail_path.string().c_str(), &width, &height, &channels, 4);
        if (data) {
          stbi_image_free(data);
        }
        else {
          width = 0;
          height = 0;
        }
      }
    }
    else {
      // No cached thumbnail found - this indicates indexing issue
      std::cerr << "Warning: No cached thumbnail found for SVG: " << asset.full_path << std::endl;
      texture_id = 0; // Will fallback to default unknown texture below
    }
  }
  else {
    // Load regular texture using stb_image
    texture_id = load_texture(asset.full_path.c_str());
    if (texture_id != 0) {
      // Get texture dimensions
      int channels;
      unsigned char* data = stbi_load(asset.full_path.c_str(), &width, &height, &channels, 4);
      if (data) {
        stbi_image_free(data);
      }
      else {
        width = 0;
        height = 0;
      }
    }
  }

  if (texture_id == 0) {
    std::cerr << "Failed to load texture: " << asset.full_path << '\n';
    return 0;
  }

  // Cache the result
  TextureCacheEntry& entry = texture_cache_[asset.full_path];
  entry.texture_id = texture_id;
  entry.file_path = asset.full_path;
  entry.width = width;
  entry.height = height;

  return texture_id;
}

void TextureManager::cleanup_texture_cache(const std::string& path) {
  auto cache_it = texture_cache_.find(path);
  if (cache_it != texture_cache_.end()) {
    if (cache_it->second.texture_id != 0) {
      glDeleteTextures(1, &cache_it->second.texture_id);
    }
    texture_cache_.erase(cache_it);
  }
}

bool TextureManager::get_texture_dimensions(const std::string& file_path, int& width, int& height) {
  auto it = texture_cache_.find(file_path);
  if (it != texture_cache_.end()) {
    width = it->second.width;
    height = it->second.height;
    return true;
  }
  return false;
}

bool TextureManager::generate_svg_thumbnail(const std::string& svg_path, const std::string& filename) {
  constexpr int MAX_THUMBNAIL_SIZE = SVG_THUMBNAIL_SIZE;

  // Create thumbnails directory if it doesn't exist
  std::filesystem::path thumbnails_dir = "thumbnails";
  if (!std::filesystem::exists(thumbnails_dir)) {
    try {
      std::filesystem::create_directory(thumbnails_dir);
    }
    catch (const std::filesystem::filesystem_error& e) {
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
  std::filesystem::path svg_file_path(filename);
  std::string png_filename = svg_file_path.stem().string() + ".png";
  std::filesystem::path output_path = thumbnails_dir / png_filename;

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

unsigned int TextureManager::load_texture_for_model(const std::string& filepath) {
  int width, height, channels;
  unsigned char* data = stbi_load(filepath.c_str(), &width, &height, &channels, 0);
  if (!data) {
    std::cout << "Failed to load texture for 3d model: " << filepath << std::endl;
    return 0;
  }

  unsigned int texture_id;
  glGenTextures(1, &texture_id);
  glBindTexture(GL_TEXTURE_2D, texture_id);

  // Set texture parameters
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

  // Upload texture data
  GLenum format = GL_RGB;
  if (channels == 4)
    format = GL_RGBA;
  else if (channels == 1)
    format = GL_RED;

  glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0, format, GL_UNSIGNED_BYTE, data);
  glGenerateMipmap(GL_TEXTURE_2D);

  stbi_image_free(data);
  return texture_id;
}

unsigned int TextureManager::create_solid_color_texture(float r, float g, float b) {
  // Create a 1x1 texture with the specified color
  unsigned char color_data[3] = {
    static_cast<unsigned char>(r * 255.0f),
    static_cast<unsigned char>(g * 255.0f),
    static_cast<unsigned char>(b * 255.0f)
  };

  unsigned int texture_id;
  glGenTextures(1, &texture_id);
  glBindTexture(GL_TEXTURE_2D, texture_id);

  // Set texture parameters
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

  // Upload the 1x1 color data
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 1, 1, 0, GL_RGB, GL_UNSIGNED_BYTE, color_data);

  return texture_id;
}

bool TextureManager::initialize_preview_system() {
  if (preview_initialized_) {
    return true;
  }

  // Create shaders (assuming shader code is provided separately)
  const char* vertex_shader_source = R"(
    #version 330 core
    layout (location = 0) in vec3 aPos;
    layout (location = 1) in vec3 aNormal;
    layout (location = 2) in vec2 aTexCoord;

    out vec3 FragPos;
    out vec3 Normal;
    out vec2 TexCoord;

    uniform mat4 model;
    uniform mat4 view;
    uniform mat4 projection;

    void main()
    {
        FragPos = vec3(model * vec4(aPos, 1.0));
        Normal = mat3(transpose(inverse(model))) * aNormal;
        TexCoord = aTexCoord;

        gl_Position = projection * view * vec4(FragPos, 1.0);
    }
  )";

  const char* fragment_shader_source = R"(
    #version 330 core
    out vec4 FragColor;

    in vec3 FragPos;
    in vec3 Normal;
    in vec2 TexCoord;

    uniform sampler2D texture_diffuse1;
    uniform bool has_texture;
    uniform vec3 material_color;

    void main()
    {
        vec3 result;
        if(has_texture) {
            result = texture(texture_diffuse1, TexCoord).rgb;
        } else {
            result = material_color;
        }
        FragColor = vec4(result, 1.0);
    }
  )";

  // Compile vertex shader
  unsigned int vertex_shader = glCreateShader(GL_VERTEX_SHADER);
  glShaderSource(vertex_shader, 1, &vertex_shader_source, nullptr);
  glCompileShader(vertex_shader);

  int success;
  char info_log[512];
  glGetShaderiv(vertex_shader, GL_COMPILE_STATUS, &success);
  if (!success) {
    glGetShaderInfoLog(vertex_shader, 512, nullptr, info_log);
    std::cerr << "ERROR::SHADER::VERTEX::COMPILATION_FAILED\n" << info_log << std::endl;
    return false;
  }

  // Compile fragment shader
  unsigned int fragment_shader = glCreateShader(GL_FRAGMENT_SHADER);
  glShaderSource(fragment_shader, 1, &fragment_shader_source, nullptr);
  glCompileShader(fragment_shader);

  glGetShaderiv(fragment_shader, GL_COMPILE_STATUS, &success);
  if (!success) {
    glGetShaderInfoLog(fragment_shader, 512, nullptr, info_log);
    std::cerr << "ERROR::SHADER::FRAGMENT::COMPILATION_FAILED\n" << info_log << std::endl;
    return false;
  }

  // Create shader program
  preview_shader_ = glCreateProgram();
  glAttachShader(preview_shader_, vertex_shader);
  glAttachShader(preview_shader_, fragment_shader);
  glLinkProgram(preview_shader_);

  glGetProgramiv(preview_shader_, GL_LINK_STATUS, &success);
  if (!success) {
    glGetProgramInfoLog(preview_shader_, 512, nullptr, info_log);
    std::cerr << "ERROR::SHADER::PROGRAM::LINKING_FAILED\n" << info_log << std::endl;
    return false;
  }

  // Clean up shaders
  glDeleteShader(vertex_shader);
  glDeleteShader(fragment_shader);

  // Create framebuffer
  glGenFramebuffers(1, &preview_framebuffer_);
  glBindFramebuffer(GL_FRAMEBUFFER, preview_framebuffer_);

  // Create color texture
  glGenTextures(1, &preview_texture_);
  glBindTexture(GL_TEXTURE_2D, preview_texture_);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 800, 800, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, preview_texture_, 0);

  // Create depth texture
  glGenTextures(1, &preview_depth_texture_);
  glBindTexture(GL_TEXTURE_2D, preview_depth_texture_);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH24_STENCIL8, 800, 800, 0, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, nullptr);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, preview_depth_texture_, 0);

  // Check framebuffer completeness
  if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
    std::cout << "ERROR::FRAMEBUFFER:: Framebuffer is not complete!" << std::endl;
    return false;
  }

  glBindFramebuffer(GL_FRAMEBUFFER, 0);

  // Enable depth testing
  glEnable(GL_DEPTH_TEST);

  preview_initialized_ = true;
  std::cout << "3D preview initialized successfully!" << std::endl;
  return true;
}

void TextureManager::cleanup_preview_system() {
  if (preview_initialized_) {
    glDeleteProgram(preview_shader_);
    glDeleteTextures(1, &preview_texture_);
    glDeleteTextures(1, &preview_depth_texture_);
    glDeleteFramebuffers(1, &preview_framebuffer_);
    preview_initialized_ = false;
  }
}
