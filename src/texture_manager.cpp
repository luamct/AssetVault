#include "texture_manager.h"

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <sstream>

#include <glad/glad.h>

// Include stb_image for PNG loading
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

// Include NanoSVG for SVG support
#define NANOSVG_IMPLEMENTATION
#include "nanosvg.h"
#define NANOSVGRAST_IMPLEMENTATION
#include "nanosvgrast.h"

#include "asset.h" // For SVG_THUMBNAIL_SIZE
#include "config.h" // For MODEL_THUMBNAIL_SIZE
#include "3d.h" // For Model, load_model, render_model, cleanup_model

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
  int width, height;
  return load_texture(filename, &width, &height);
}

unsigned int TextureManager::load_texture(const char* filename, int* out_width, int* out_height) {
  int width, height, channels;
  unsigned char* data = stbi_load(filename, &width, &height, &channels, 4);
  if (!data) {
    std::cerr << "Failed to load texture: " << filename << '\n';
    return 0;
  }

  // Return dimensions if requested
  if (out_width) *out_width = width;
  if (out_height) *out_height = height;

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
  const char* filename,
  int target_width, int target_height,
  int* actual_width, int* actual_height
) {
  // Parse SVG directly from file like the nanosvg examples do
  NSVGimage* image = nsvgParseFromFile(filename, "px", 96.0f);
  if (!image) {
    std::cerr << "Failed to parse SVG: " << filename << '\n';
    return 0;
  }

  if (image->width <= 0 || image->height <= 0) {
    std::cerr << "Invalid SVG dimensions: " << image->width << "x" << image->height << std::endl;
    nsvgDelete(image);
    return 0;
  }

  // Calculate scale factor to fit SVG into target dimensions while preserving aspect ratio
  float scale_x = static_cast<float>(target_width) / image->width;
  float scale_y = static_cast<float>(target_height) / image->height;
  float scale = std::min(scale_x, scale_y);

  // Calculate actual output dimensions maintaining aspect ratio
  int w = static_cast<int>(image->width * scale);
  int h = static_cast<int>(image->height * scale);

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

  // Return actual dimensions if requested
  if (actual_width) *actual_width = w;
  if (actual_height) *actual_height = h;

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

unsigned int TextureManager::get_asset_texture(const Asset& asset) {
  const auto u8_path = asset.full_path.u8string();

  // Handle 3D models - check for thumbnails first, generate on-demand if needed
  if (asset.type == AssetType::Model) {
    // Generate thumbnail path using full relative path structure
    // TODO: Move this logic to Asset::thumbnail_path()
    std::filesystem::path asset_root(Config::ASSET_ROOT_DIRECTORY);
    std::filesystem::path relative_path = std::filesystem::relative(asset.full_path, asset_root);
    std::filesystem::path thumbnail_path = "thumbnails" / relative_path.replace_extension(".png");

    // Check if thumbnail exists and load it
    if (std::filesystem::exists(thumbnail_path)) {
      // Check if thumbnail is already cached using original asset path as key
      auto it = texture_cache_.find(u8_path);
      if (it != texture_cache_.end()) {
        return it->second.texture_id;
      }

      // Load thumbnail
      unsigned int texture_id = load_texture(thumbnail_path.string().c_str());
      if (texture_id != 0) {
        // Cache the thumbnail using original asset path as key
        TextureCacheEntry& entry = texture_cache_[u8_path];
        entry.texture_id = texture_id;
        entry.file_path = thumbnail_path.string();
        entry.width = Config::MODEL_THUMBNAIL_SIZE;
        entry.height = Config::MODEL_THUMBNAIL_SIZE;
        return texture_id;
      }
    }
    else {
      // Thumbnail doesn't exist - try to generate it on-demand
      // This only works if we're in the main thread with OpenGL context
      // Check if this model previously failed to load
      if (failed_models_cache_.find(u8_path) != failed_models_cache_.end()) {
        // Model failed before, skip thumbnail generation
        auto it = type_icons_.find(asset.type);
        if (it != type_icons_.end()) {
          return it->second;
        }
        return default_texture_;
      }
      
      if (is_preview_initialized() && std::filesystem::exists(asset.full_path)) {
        if (generate_3d_model_thumbnail(u8_path, relative_path.u8string(), *this)) {
          // Thumbnail was successfully generated, try to load it
          if (std::filesystem::exists(thumbnail_path)) {
            unsigned int texture_id = load_texture(thumbnail_path.string().c_str());
            if (texture_id != 0) {
              // Cache the thumbnail using original asset path as key
              TextureCacheEntry& entry = texture_cache_[u8_path];
              entry.texture_id = texture_id;
              entry.file_path = thumbnail_path.string();
              entry.width = Config::MODEL_THUMBNAIL_SIZE;
              entry.height = Config::MODEL_THUMBNAIL_SIZE;
              return texture_id;
            }
          }
        }
        else {
          // Thumbnail generation failed, add to failed cache to prevent retry
          failed_models_cache_.insert(u8_path);
        }
      }
    }

    // Fallback to model icon if no thumbnail or generation failed
    auto it = type_icons_.find(asset.type);
    if (it != type_icons_.end()) {
      return it->second;
    }
    return default_texture_;
  }

  // For other non-texture assets, return type-specific icon
  if (asset.type != AssetType::Texture) {
    auto it = type_icons_.find(asset.type);
    if (it != type_icons_.end()) {
      return it->second;
    }
    return default_texture_;
  }

  // Check if texture is already cached
  auto it = texture_cache_.find(u8_path);
  if (it != texture_cache_.end()) {
    return it->second.texture_id;
  }

  // Check if file exists before attempting to load (defensive check for deleted files)
  if (!std::filesystem::exists(asset.full_path)) {
    auto icon_it = type_icons_.find(asset.type);
    return (icon_it != type_icons_.end()) ? icon_it->second : default_texture_;
  }

  unsigned int texture_id = 0;
  int width = 0, height = 0;

  // Handle SVG files - render on-demand directly to OpenGL texture
  if (asset.extension == ".svg") {
    texture_id = load_svg_texture(asset.full_path.string().c_str(), Config::SVG_THUMBNAIL_SIZE, Config::SVG_THUMBNAIL_SIZE, &width, &height);
  }
  else {
    // Load regular texture using stb_image
    texture_id = load_texture(asset.full_path.string().c_str(), &width, &height);
  }

  if (texture_id == 0) {
    std::cerr << "[TextureManager] Failed to load texture, returning default icon for: " << u8_path << '\n';
    // Return type icon instead of 0
    auto icon_it = type_icons_.find(asset.type);
    return (icon_it != type_icons_.end()) ? icon_it->second : default_texture_;
  }

  // Cache the result
  TextureCacheEntry& entry = texture_cache_[u8_path];
  entry.texture_id = texture_id;
  entry.file_path = u8_path;
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


// TODO: Make this method not static and remove texture_manager from argument (just use this if needed)
bool TextureManager::generate_3d_model_thumbnail(const std::string& model_path, const std::string& relative_path, TextureManager& texture_manager) {
  // Check if preview system is initialized
  if (!texture_manager.is_preview_initialized()) {
    std::cerr << "Cannot generate 3D model thumbnail: preview system not initialized" << std::endl;
    return false;
  }

  // Create thumbnail directory path
  std::filesystem::path thumbnail_path = "thumbnails" / std::filesystem::path(relative_path).replace_extension(".png");

  // Create directory structure if it doesn't exist
  std::filesystem::path thumbnail_dir = thumbnail_path.parent_path();
  if (!std::filesystem::exists(thumbnail_dir)) {
    try {
      std::filesystem::create_directories(thumbnail_dir);
    }
    catch (const std::filesystem::filesystem_error& e) {
      std::cerr << "Failed to create thumbnail directory " << thumbnail_dir << ": " << e.what() << std::endl;
      return false;
    }
  }

  // Load the 3D model
  Model model;
  if (!load_model(model_path, model, texture_manager)) {
    std::cerr << "Failed to load 3D model for thumbnail generation: " << model_path << std::endl;
    return false;
  }

  // Create temporary framebuffer for thumbnail rendering
  unsigned int temp_framebuffer, temp_texture, temp_depth_texture;
  const int thumbnail_size = Config::MODEL_THUMBNAIL_SIZE;

  // Create framebuffer
  glGenFramebuffers(1, &temp_framebuffer);
  glBindFramebuffer(GL_FRAMEBUFFER, temp_framebuffer);

  // Create color texture
  glGenTextures(1, &temp_texture);
  glBindTexture(GL_TEXTURE_2D, temp_texture);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, thumbnail_size, thumbnail_size, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, temp_texture, 0);

  // Create depth texture
  glGenTextures(1, &temp_depth_texture);
  glBindTexture(GL_TEXTURE_2D, temp_depth_texture);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH24_STENCIL8, thumbnail_size, thumbnail_size, 0, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, nullptr);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, temp_depth_texture, 0);

  // Check framebuffer completeness
  if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
    std::cerr << "Thumbnail framebuffer is not complete!" << std::endl;
    glDeleteFramebuffers(1, &temp_framebuffer);
    glDeleteTextures(1, &temp_texture);
    glDeleteTextures(1, &temp_depth_texture);
    cleanup_model(model);
    return false;
  }

  // Render model to framebuffer
  glViewport(0, 0, thumbnail_size, thumbnail_size);
  glClearColor(0.0f, 0.0f, 0.0f, 0.0f); // Transparent background for thumbnails
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  // Render the model using existing render_model function
  render_model(model, texture_manager);

  // Read pixels from framebuffer (no flipping needed - already correct orientation)
  std::vector<unsigned char> pixels(thumbnail_size * thumbnail_size * 4);
  glReadPixels(0, 0, thumbnail_size, thumbnail_size, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());

  // Save as PNG
  int result = stbi_write_png(
    thumbnail_path.string().c_str(),
    thumbnail_size, thumbnail_size, 4,
    pixels.data(),
    thumbnail_size * 4
  );

  // Cleanup OpenGL resources
  glDeleteFramebuffers(1, &temp_framebuffer);
  glDeleteTextures(1, &temp_texture);
  glDeleteTextures(1, &temp_depth_texture);
  cleanup_model(model);

  // Restore original framebuffer
  glBindFramebuffer(GL_FRAMEBUFFER, 0);

  if (!result) {
    std::cerr << "Failed to write 3D model thumbnail: " << thumbnail_path.string() << std::endl;
    return false;
  }

  return true;
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
    in vec3 FragPos;
    in vec3 Normal;
    in vec2 TexCoord;

    uniform vec3 lightPos;
    uniform vec3 viewPos;
    uniform vec3 lightColor;
    uniform sampler2D diffuseTexture;
    uniform bool useTexture;
    uniform vec3 materialColor;

    out vec4 FragColor;

    void main()
    {
        // Sample texture color or use material color
        vec3 objectColor = useTexture ? texture(diffuseTexture, TexCoord).rgb : materialColor;

        vec3 norm = normalize(Normal);
        vec3 viewDir = normalize(viewPos - FragPos);

        // Moderate ambient lighting to allow for some shadows
        float ambientStrength = 0.25;
        vec3 ambient = ambientStrength * lightColor;

        // Main key light (from camera direction)
        vec3 lightDir = normalize(lightPos - FragPos);
        float diff = max(dot(norm, lightDir), 0.0);
        vec3 diffuse = diff * lightColor * 0.7; // Slightly stronger main light

        // Add subtle fill light from opposite direction to soften shadows
        vec3 fillLightDir = normalize(-lightPos); // Opposite direction
        float fillDiff = max(dot(norm, fillLightDir), 0.0);
        vec3 fillLight = fillDiff * lightColor * 0.15; // Gentler fill light

        // Softer specular highlights
        float specularStrength = 0.2; // Reduced from 0.5
        vec3 reflectDir = reflect(-lightDir, norm);
        float spec = pow(max(dot(viewDir, reflectDir), 0.0), 64); // Higher for softer highlights
        vec3 specular = specularStrength * spec * lightColor;

        // Add subtle rim lighting for better shape definition
        float rimStrength = 0.3;
        float rimFactor = 1.0 - max(dot(viewDir, norm), 0.0);
        vec3 rimLight = rimStrength * pow(rimFactor, 3.0) * lightColor;

        vec3 result = (ambient + diffuse + fillLight + specular + rimLight) * objectColor;
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

void TextureManager::queue_texture_invalidation(const std::string& file_path) {
  std::lock_guard<std::mutex> lock(invalidation_mutex_);
  invalidation_queue_.push(file_path);
}

void TextureManager::process_invalidation_queue() {
  std::lock_guard<std::mutex> lock(invalidation_mutex_);

  while (!invalidation_queue_.empty()) {
    const std::string& file_path = invalidation_queue_.front();

    // Remove from texture cache if present
    auto cache_it = texture_cache_.find(file_path);
    if (cache_it != texture_cache_.end()) {
      if (cache_it->second.texture_id != 0) {
        glDeleteTextures(1, &cache_it->second.texture_id);
      }
      texture_cache_.erase(cache_it);
    }

    invalidation_queue_.pop();
  }
}

void TextureManager::clear_texture_cache() {
  std::lock_guard<std::mutex> lock(invalidation_mutex_);

  // Clean up all cached textures (but preserve type icons and default texture)
  for (auto& entry : texture_cache_) {
    if (entry.second.texture_id != 0) {
      glDeleteTextures(1, &entry.second.texture_id);
    }
  }
  texture_cache_.clear();
}
