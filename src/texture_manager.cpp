#include "texture_manager.h"

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <chrono>
#include <cmath>

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <assimp/scene.h>
#include "logger.h"
#include "utils.h"

// Include stb_image for PNG loading
#ifdef _WIN32
#define STBI_WINDOWS_UTF8  // Enable UTF-8 support on Windows
#endif
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

// SVG rasterization: prefer lunasvg for CSS+style support; keep NanoSVG as fallback if needed
#include <lunasvg.h>

#include "asset.h" // For SVG_THUMBNAIL_SIZE
#include "config.h" // For MODEL_THUMBNAIL_SIZE, MAX_TEXTURE_RETRY_ATTEMPTS
#include "3d.h" // For Model, load_model, render_model, cleanup_model
#include "animation.h" // For advance_model_animation

TextureManager::TextureManager()
  : default_texture_(0), preview_texture_(0), preview_depth_texture_(0),
  preview_framebuffer_(0), preview_initialized_(false),
  play_icon_(0), pause_icon_(0), speaker_icon_(0) {
}

TextureManager::~TextureManager() {
  cleanup();
}

// TextureData cleanup implementation
void TextureData::cleanup() {
  if (data) {
    switch (on_destroy) {
      case OnDestroy::STBI_FREE:
        stbi_image_free(data);
        break;
      case OnDestroy::FREE:
        free(data);
        break;
      case OnDestroy::NONE:
        // Don't free - memory managed elsewhere
        break;
    }
    data = nullptr;
  }
}

bool TextureManager::initialize() {
  // Load default texture
  default_texture_ = load_texture("images/texture.png");
  if (default_texture_ == 0) {
    LOG_ERROR("Failed to load default texture");
    return false;
  }

  // Load type-specific textures
  load_type_textures();

  // Load audio control icons
  play_icon_ = load_texture("images/play.png");
  pause_icon_ = load_texture("images/pause.png");
  speaker_icon_ = load_texture("images/speaker.png");

  LOG_INFO("TextureManager initialized successfully");
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

  // Clean up audio control icons
  if (play_icon_ != 0) glDeleteTextures(1, &play_icon_);
  if (pause_icon_ != 0) glDeleteTextures(1, &pause_icon_);
  if (speaker_icon_ != 0) glDeleteTextures(1, &speaker_icon_);

  std::vector<std::shared_ptr<Animation2D>> animations;
  {
    std::lock_guard<std::mutex> lock(animation_mutex_);
    for (auto& [_, weak_anim] : animation_cache_) {
      if (auto anim = weak_anim.lock()) {
        animations.push_back(std::move(anim));
      }
    }
    animation_cache_.clear();
  }
}

std::shared_ptr<Animation2D> TextureManager::load_animated_gif_internal(const std::string& filepath) {
  LOG_TRACE("[GIF] Loading animated GIF: {}", filepath);

  // Read file into memory
  FILE* file = fopen(filepath.c_str(), "rb");
  if (!file) {
    LOG_WARN("[GIF] Failed to open file: {}", filepath);
    return nullptr;
  }

  // Get file size
  fseek(file, 0, SEEK_END);
  long file_size = ftell(file);
  fseek(file, 0, SEEK_SET);

  if (file_size <= 0) {
    LOG_WARN("[GIF] Invalid file size: {}", filepath);
    fclose(file);
    return nullptr;
  }

  // Read file data
  std::vector<unsigned char> file_data(file_size);
  size_t bytes_read = fread(file_data.data(), 1, file_size, file);
  fclose(file);

  if (bytes_read != static_cast<size_t>(file_size)) {
    LOG_WARN("[GIF] Failed to read complete file: {}", filepath);
    return nullptr;
  }

  // Load GIF frames using stb_image
  int* delays = nullptr;
  int width = 0, height = 0, frame_count = 0, channels = 0;
  unsigned char* frames_data = stbi_load_gif_from_memory(
    file_data.data(),
    static_cast<int>(file_size),
    &delays,
    &width, &height, &frame_count,
    &channels,
    4  // Force RGBA
  );

  if (!frames_data || frame_count == 0) {
    LOG_WARN("[GIF] Failed to load GIF frames: {}", filepath);
    if (delays) free(delays);
    return nullptr;
  }

  LOG_INFO("[GIF] Loaded {} frames ({}x{}) from {}", frame_count, width, height, filepath);

  // Create 2D animation container
  auto anim_data = std::make_shared<Animation2D>();
  anim_data->width = width;
  anim_data->height = height;
  anim_data->frame_delays.assign(delays, delays + frame_count);

  // Log delay values for debugging
  std::string delay_str;
  for (int i = 0; i < frame_count; i++) {
    delay_str += std::to_string(delays[i]);
    if (i < frame_count - 1) delay_str += ", ";
  }
  LOG_DEBUG("[GIF] Frame delays (milliseconds): [{}]", delay_str);

  // Free the delays array allocated by stbi
  free(delays);

  // Create OpenGL texture for each frame using unified texture system
  const int frame_size = width * height * 4; // RGBA
  TextureParameters ui_params = TextureParameters::ui_texture(); // Use UI texture params (nearest neighbor, no blur)

  for (int i = 0; i < frame_count; i++) {
    unsigned char* frame_data = frames_data + (i * frame_size);

    // Create TextureData for this frame
    TextureData texture_data;
    texture_data.data = frame_data;
    texture_data.width = width;
    texture_data.height = height;
    texture_data.format = GL_RGBA;
    texture_data.on_destroy = OnDestroy::NONE; // Part of frames_data buffer, freed manually later

    // Create OpenGL texture using unified method (applies UI filtering for sharp pixels)
    unsigned int texture_id = create_opengl_texture(texture_data, ui_params);
    anim_data->frame_textures.push_back(texture_id);
  }

  // Free the frames data allocated by stbi
  stbi_image_free(frames_data);

  anim_data->rebuild_timing_cache();

  LOG_DEBUG("[GIF] Created {} OpenGL textures for animated GIF: {}", frame_count, filepath);
  return anim_data;
}

std::shared_ptr<Animation2D> TextureManager::get_or_load_animated_gif(const std::string& filepath) {
  {
    std::lock_guard<std::mutex> lock(animation_mutex_);
    auto it = animation_cache_.find(filepath);
    if (it != animation_cache_.end()) {
      if (auto existing = it->second.lock()) {
        return existing;
      }
      animation_cache_.erase(it);
    }
  }

  std::shared_ptr<Animation2D> animation = load_animated_gif_internal(filepath);
  if (!animation) {
    return nullptr;
  }

  {
    std::lock_guard<std::mutex> lock(animation_mutex_);
    animation_cache_[filepath] = animation;
  }
  return animation;
}

unsigned int TextureManager::load_texture(const char* filename) {
  int width, height;
  return load_texture(filename, &width, &height);
}

unsigned int TextureManager::load_texture(const char* filename, int* out_width, int* out_height) {
  // For the load_texture function, we need to force RGBA format (like the original implementation)
  TextureData texture_data;

  int width, height, channels;
  unsigned char* data = stbi_load(filename, &width, &height, &channels, 4); // Force RGBA

  if (!data) {
    LOG_ERROR("Failed to load texture: {}", filename);
    return 0;
  }

  texture_data.data = data;
  texture_data.width = width;
  texture_data.height = height;
  texture_data.format = GL_RGBA;
  // on_destroy defaults to STBI_FREE (correct for stbi_load)

  // Return dimensions if requested
  if (out_width) *out_width = width;
  if (out_height) *out_height = height;

  // Use UI texture parameters (pixel art style with nearest neighbor filtering)
  TextureParameters params = TextureParameters::ui_texture();
  return create_opengl_texture(texture_data, params);
}


void TextureManager::load_type_textures() {
  const std::unordered_map<AssetType, const char*> texture_paths = {
      {AssetType::_2D, "images/texture.png"},
      {AssetType::_3D, "images/model.png"},
      {AssetType::Audio, "images/sound.png"},
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
      LOG_ERROR("Failed to load type texture: {}", path);
    }
  }
}

const TextureCacheEntry& TextureManager::get_asset_texture(const Asset& asset) {
  std::lock_guard<std::mutex> lock(cleanup_mutex_);
  const auto& asset_path = asset.path;

  // Check if asset is already in cache
  auto it = texture_cache_.find(asset_path);
  if (it != texture_cache_.end()) {
    return it->second;
  }

  // Get relative path for logging
  const std::string& relative_path = asset.relative_path;

  // Create new cache entry for this asset
  LOG_TRACE("[TextureManager] Cache miss for '{}', creating new entry", relative_path);
  TextureCacheEntry& entry = texture_cache_[asset_path];
  entry.file_path = asset_path;

  // Handle 3D models - only load existing thumbnails, no generation
  if (asset.type == AssetType::_3D) {
    // Generate thumbnail path using centralized method
    std::filesystem::path thumbnail_path = get_thumbnail_path(asset.relative_path);

    // Check if thumbnail exists and load it
    if (std::filesystem::exists(thumbnail_path)) {
      unsigned int texture_id = load_texture(thumbnail_path.u8string().c_str());

      if (texture_id != 0) {
        // Successfully loaded thumbnail
        entry.texture_id = texture_id;
        entry.file_path = thumbnail_path.generic_u8string();
        entry.width = Config::MODEL_THUMBNAIL_SIZE;
        entry.height = Config::MODEL_THUMBNAIL_SIZE;
        entry.loaded = true;
        LOG_TRACE("[TextureManager] 3D model '{}': thumbnail loaded, texture_id: {}", relative_path, texture_id);
        return entry;
      }
      else {
        LOG_WARN("[TextureManager] 3D model '{}': failed to load thumbnail", relative_path);
      }
    }

    // No thumbnail available - use default icon
    // The thumbnail will be generated by EventProcessor and available next frame
    auto icon_it = type_icons_.find(asset.type);
    unsigned int chosen_texture_id = (icon_it != type_icons_.end()) ? icon_it->second : default_texture_;
    entry.default_texture_id = chosen_texture_id;  // Use default_texture_id for shared icon
    entry.width = Config::THUMBNAIL_SIZE;
    entry.height = Config::THUMBNAIL_SIZE;
    LOG_TRACE("[TextureManager] 3D model '{}': using default icon, texture_id: {} (thumbnail pending)",
      relative_path, chosen_texture_id);
    return entry;
  }

  // For other non-texture assets, return type-specific icon
  if (asset.type != AssetType::_2D) {
    auto icon_it = type_icons_.find(asset.type);
    entry.default_texture_id = (icon_it != type_icons_.end()) ? icon_it->second : default_texture_;  // Use default_texture_id for shared icon
    entry.width = Config::THUMBNAIL_SIZE;
    entry.height = Config::THUMBNAIL_SIZE;
    LOG_TRACE("[TextureManager] Non-2D asset '{}' ({}): using type icon, texture_id: {}",
      relative_path, get_asset_type_string(asset.type), entry.default_texture_id);
    return entry;
  }

  // Handle 2D texture assets
  // Check if file exists before attempting to load (defensive check for deleted files)
  if (!std::filesystem::exists(std::filesystem::u8path(asset.path))) {
    auto icon_it = type_icons_.find(asset.type);
    entry.default_texture_id = (icon_it != type_icons_.end()) ? icon_it->second : default_texture_;  // Use default_texture_id for shared icon
    LOG_TRACE("[TextureManager] 2D asset '{}': file doesn't exist, using default icon, texture_id: {}",
      relative_path, entry.default_texture_id);
    entry.width = Config::THUMBNAIL_SIZE;
    entry.height = Config::THUMBNAIL_SIZE;
    return entry;
  }

  unsigned int texture_id = 0;
  int width = 0, height = 0;

  // Handle SVG files - load pre-generated PNG thumbnail
  if (asset.extension == ".svg") {
    std::filesystem::path thumbnail_path = get_thumbnail_path(asset.relative_path);
    if (std::filesystem::exists(thumbnail_path)) {
      texture_id = load_texture(thumbnail_path.string().c_str(), &width, &height);
      if (texture_id != 0) {
        LOG_TRACE("[TextureManager] 2D asset '{}': SVG thumbnail loaded, texture_id: {}, size: {}x{}",
          relative_path, texture_id, width, height);
      }
    }
    else {
      LOG_WARN("[TextureManager] 2D asset '{}': SVG thumbnail not found at {}", relative_path, thumbnail_path.string());
    }
  }
  else {
    // Load regular texture using stb_image (GIFs load first frame automatically)
    texture_id = load_texture(asset.path.c_str(), &width, &height);
    if (texture_id != 0) {
      LOG_TRACE("[TextureManager] 2D asset '{}': texture loaded, texture_id: {}, size: {}x{}",
        relative_path, texture_id, width, height);
    }
  }

  if (texture_id == 0) {
    // Mark as failed to prevent future retry loops
    auto icon_it = type_icons_.find(asset.type);
    entry.default_texture_id = (icon_it != type_icons_.end()) ? icon_it->second : default_texture_;  // Use default_texture_id for shared icon
    entry.width = Config::THUMBNAIL_SIZE;
    entry.height = Config::THUMBNAIL_SIZE;
    LOG_INFO("[TextureManager] 2D asset '{}': failed to load, using default icon, texture_id: {}",
      relative_path, entry.default_texture_id);
    return entry;
  }

  // Successfully loaded texture
  entry.texture_id = texture_id;
  entry.width = width;
  entry.height = height;
  entry.loaded = true;
  LOG_DEBUG("[TextureManager] 2D asset '{}': cache entry created, texture_id: {}",
    relative_path, texture_id);

  return entry;
}

void TextureManager::cleanup_texture_cache(const std::string& path) {
  auto cache_it = texture_cache_.find(path);
  if (cache_it != texture_cache_.end()) {
    LOG_TRACE("Manual cache cleanup for: {} (texture_id: {})", path, cache_it->second.texture_id);
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

void TextureManager::generate_3d_model_thumbnail(const std::string& model_path, const std::filesystem::path& thumbnail_path) {
  // Start total timing
  auto start_total = std::chrono::high_resolution_clock::now();

  // Extract filename from path for logging
  std::filesystem::path path_obj(model_path);
  std::string filename = path_obj.filename().string();

  LOG_TRACE("[THUMBNAIL] Generating thumbnail for model {} at {}", model_path, thumbnail_path.generic_u8string());

  // Load the 3D model (includes texture IO) - timing includes setup overhead
  LOG_DEBUG("[THUMBNAIL] Loading 3D model for thumbnail: {}", model_path);
  auto start_io = std::chrono::high_resolution_clock::now();
  Model model;
  bool load_success = load_model(model_path, model, *this);
  auto end_io = std::chrono::high_resolution_clock::now();
  auto io_duration = std::chrono::duration_cast<std::chrono::microseconds>(end_io - start_io);

  LOG_DEBUG("[THUMBNAIL] load_model returned: {}, has_no_geometry: {}, vertices: {}, indices: {}",
            load_success, model.has_no_geometry, model.vertices.size(), model.indices.size());

  if (!load_success) {
    LOG_ERROR("[THUMBNAIL] Failed to load model for thumbnail: {}", model_path);
    throw ThumbnailGenerationException("Failed to load 3D model: " + model_path);
  }

  LOG_TRACE("[THUMBNAIL] Model loaded successfully. Materials count: {}", model.materials.size());

  const bool has_renderable_geometry = !model.has_no_geometry && (model.vao != 0) && !model.indices.empty();
  const bool has_renderable_skeleton = model.has_skeleton && !model.bones.empty();

  if (!has_renderable_geometry && !has_renderable_skeleton) {
    LOG_INFO("[THUMBNAIL] Model '{}' has nothing to render for thumbnail generation.", model_path);
    cleanup_model(model);
    return;
  }

  if (!model.animations.empty() && has_renderable_skeleton) {
    if (model.active_animation >= model.animations.size()) {
      model.active_animation = model.animations.size() - 1;
    }
    const std::string& clip_name = model.animations[model.active_animation].name;
    LOG_DEBUG("[THUMBNAIL] Advancing animation '{}' to first frame for thumbnail", clip_name);
    // Ensure we sample the very first frame so skeleton transforms match the preview playback.
    model.animation_time = 0.0;
    advance_model_animation(model, 0.0f);
  }

  // Start GPU timing for rendering
  auto start_gpu = std::chrono::high_resolution_clock::now();

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
    LOG_ERROR("Thumbnail framebuffer is not complete!");
    glDeleteFramebuffers(1, &temp_framebuffer);
    glDeleteTextures(1, &temp_texture);
    glDeleteTextures(1, &temp_depth_texture);
    cleanup_model(model);
    throw ThumbnailGenerationException("OpenGL framebuffer is not complete for thumbnail generation");
  }

  // Render model to framebuffer
  glViewport(0, 0, thumbnail_size, thumbnail_size);
  glClearColor(0.0f, 0.0f, 0.0f, 0.0f); // Transparent background for thumbnails
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  // Check for OpenGL errors before rendering
  GLenum gl_error = glGetError();
  if (gl_error != GL_NO_ERROR) {
    LOG_WARN("OpenGL error before thumbnail render: {}", gl_error);
  }

  // Render the model or skeleton using existing preview routines.
  Camera3D default_camera; // Uses default rotation and zoom
  if (has_renderable_geometry) {
    render_model(model, *this, default_camera, false);
  }
  else {
    render_skeleton(model, default_camera, *this);
  }

  // Check for OpenGL errors after rendering
  gl_error = glGetError();
  if (gl_error != GL_NO_ERROR) {
    LOG_WARN("OpenGL error after thumbnail render: {}", gl_error);
  }

  // Read pixels from framebuffer (OpenGL gives bottom-to-top rows)
  std::vector<unsigned char> pixels(thumbnail_size * thumbnail_size * 4);
  glReadPixels(0, 0, thumbnail_size, thumbnail_size, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());

  auto end_gpu = std::chrono::high_resolution_clock::now();
  auto gpu_duration = std::chrono::duration_cast<std::chrono::microseconds>(end_gpu - start_gpu);

  // Start write timing (PNG file writing + cleanup)
  auto start_write = end_gpu;

  // Create directory structure if it doesn't exist
  std::filesystem::path parent_dir = thumbnail_path.parent_path();
  if (!std::filesystem::exists(parent_dir)) {
    try {
      std::filesystem::create_directories(parent_dir);
    }
    catch (const std::filesystem::filesystem_error& e) {
      LOG_ERROR("Failed to create thumbnail directory: {}: {}", parent_dir.generic_u8string(), e.what());
      throw ThumbnailGenerationException("Failed to create thumbnail directory: " + parent_dir.generic_u8string() + ": " + e.what());
    }
  }

  // Save as PNG
  // stbi_write assumes top-left origin; flip via the provided global flag to match PNG expectations
  stbi_flip_vertically_on_write(1);
  int write_result = stbi_write_png(
    thumbnail_path.u8string().c_str(),
    thumbnail_size, thumbnail_size, 4,
    pixels.data(),
    thumbnail_size * 4
  );
  stbi_flip_vertically_on_write(0);

  if (!write_result) {
    LOG_ERROR("Failed to write 3D model thumbnail: {}", thumbnail_path.generic_u8string());
    throw ThumbnailGenerationException("Failed to write 3D model thumbnail: " + thumbnail_path.generic_u8string());
  }

  // Restore original framebuffer BEFORE cleanup
  glBindFramebuffer(GL_FRAMEBUFFER, 0);

  // Cleanup OpenGL resources
  glDeleteFramebuffers(1, &temp_framebuffer);
  glDeleteTextures(1, &temp_texture);
  glDeleteTextures(1, &temp_depth_texture);

  // Clean up model
  cleanup_model(model);

  auto end_write = std::chrono::high_resolution_clock::now();
  auto write_duration = std::chrono::duration_cast<std::chrono::microseconds>(end_write - start_write);

  // Log performance metrics in single line
  auto end_total = end_write;
  auto total_duration = std::chrono::duration_cast<std::chrono::microseconds>(end_total - start_total);
  LOG_DEBUG("[THUMBNAIL] {} - Total: {:.1f}ms (IO: {:.1f}ms, GPU: {:.1f}ms, Write: {:.1f}ms)",
    filename, total_duration.count() / 1000.0, io_duration.count() / 1000.0,
    gpu_duration.count() / 1000.0, write_duration.count() / 1000.0);

}

void TextureManager::generate_svg_thumbnail(const std::filesystem::path& svg_path, const std::filesystem::path& thumbnail_path) {
  const std::string svg_path_str = svg_path.u8string();

  // Load SVG with lunasvg (supports CSS classes, gradients, styles)
  auto document = lunasvg::Document::loadFromFile(svg_path_str);
  if (!document) {
    LOG_WARN("[SVG] Failed to load SVG with lunasvg: {}", svg_path_str);
    throw ThumbnailGenerationException("Failed to load SVG with lunasvg: " + svg_path_str);
  }

  // Compute output size preserving aspect ratio, fit within Config::SVG_THUMBNAIL_SIZE
  double svg_w = document->width();
  double svg_h = document->height();
  if (svg_w <= 0.0 || svg_h <= 0.0) {
    // Fallback to square if dimensions missing
    svg_w = svg_h = static_cast<double>(Config::SVG_THUMBNAIL_SIZE);
  }
  const double target = static_cast<double>(Config::SVG_THUMBNAIL_SIZE);
  const double scale = std::min(target / svg_w, target / svg_h);
  const int out_w = std::max(1, static_cast<int>(std::round(svg_w * scale)));
  const int out_h = std::max(1, static_cast<int>(std::round(svg_h * scale)));

  auto bitmap = document->renderToBitmap(out_w, out_h);
  if (bitmap.isNull()) {
    LOG_WARN("[SVG] lunasvg failed to render: {}", svg_path_str);
    throw ThumbnailGenerationException("lunasvg failed to render: " + svg_path_str);
  }

  // lunasvg returns ARGB32 premultiplied. Convert to RGBA plain for stb_image_write.
  bitmap.convertToRGBA();

  // Ensure thumbnail directory exists
  std::filesystem::path thumbnail_dir = thumbnail_path.parent_path();
  std::error_code ec;
  std::filesystem::create_directories(thumbnail_dir, ec);
  if (ec) {
    LOG_WARN("[SVG] Failed to create thumbnail directory {}: {}", thumbnail_dir.string(), ec.message());
    throw ThumbnailGenerationException("Failed to create thumbnail directory " + thumbnail_dir.string() + ": " + ec.message());
  }

  // Write PNG using stb_image_write (bitmap is RGBA)
  const std::string out_path = thumbnail_path.u8string();
  const unsigned char* data = reinterpret_cast<const unsigned char*>(bitmap.data());
  const int stride = bitmap.stride();
  if (!stbi_write_png(out_path.c_str(), out_w, out_h, 4, data, stride)) {
    LOG_WARN("[SVG] Failed to write PNG: {}", out_path);
    throw ThumbnailGenerationException("Failed to write PNG: " + out_path);
  }

  LOG_TRACE("[SVG] Generated thumbnail via lunasvg: {} -> {} ({}x{})", svg_path_str, out_path, out_w, out_h);
}

unsigned int TextureManager::load_texture_for_model(const std::string& filepath) {
  // Use the new unified pipeline
  TextureData texture_data = load_texture_data_from_file(filepath);
  if (!texture_data.is_valid()) {
    LOG_WARN("Failed to load texture for 3d model: {}", filepath);
    return 0;
  }

  TextureParameters params = TextureParameters::model_texture();
  return create_opengl_texture(texture_data, params);
}

unsigned int TextureManager::create_material_texture(const glm::vec3& diffuse, const glm::vec3& emissive, float emissive_intensity) {
  // Now that the shader properly handles emissive colors,
  // we just use the diffuse color for the texture
  // The emissive color will be passed separately to the shader
  glm::vec3 final_color = diffuse;

  LOG_TRACE("[TEXTURE] Creating material texture: diffuse=({:.3f}, {:.3f}, {:.3f}), emissive=({:.3f}, {:.3f}, {:.3f}), intensity={:.3f}, final=({:.3f}, {:.3f}, {:.3f})",
    diffuse.r, diffuse.g, diffuse.b,
    emissive.r, emissive.g, emissive.b,
    emissive_intensity,
    final_color.r, final_color.g, final_color.b);

  // Use the new unified pipeline
  TextureData texture_data = create_solid_color_data(final_color.r, final_color.g, final_color.b);
  if (!texture_data.is_valid()) {
    LOG_ERROR("Failed to create material texture data");
    return 0;
  }

  TextureParameters params = TextureParameters::solid_color();
  return create_opengl_texture(texture_data, params);
}

unsigned int TextureManager::load_embedded_texture(const aiTexture* ai_texture) {
  // Use the new unified pipeline
  TextureData texture_data = load_texture_data_from_assimp(ai_texture);
  if (!texture_data.is_valid()) {
    LOG_WARN("[EMBEDDED] Failed to load embedded texture data");
    return 0;
  }

  TextureParameters params = TextureParameters::model_texture();
  return create_opengl_texture(texture_data, params);
}

// New unified texture loading system implementation

TextureData TextureManager::load_texture_data_from_file(const std::string& filepath) {
  TextureData texture_data;

  int width, height, channels;
  unsigned char* data = stbi_load(filepath.c_str(), &width, &height, &channels, 0);

  if (!data) {
    LOG_WARN("[TEXTURE_DATA] Failed to load texture from file: {}", filepath);
    return texture_data; // Return invalid texture data
  }

  texture_data.data = data;
  texture_data.width = width;
  texture_data.height = height;

  // Set format based on channels
  if (channels == 1) {
    texture_data.format = GL_RED;
  } else if (channels == 3) {
    texture_data.format = GL_RGB;
  } else if (channels == 4) {
    texture_data.format = GL_RGBA;
  } else {
    LOG_WARN("[TEXTURE_DATA] Unsupported channel count: {} for file: {}", channels, filepath);
    texture_data.format = GL_RGB;
  }

  LOG_TRACE("[TEXTURE_DATA] Loaded texture data from file: {} ({}x{}, {} channels, format: {})",
            filepath, width, height, channels, texture_data.format);

  return texture_data;
}

TextureData TextureManager::load_texture_data_from_memory(const unsigned char* data, int size, const std::string& source_info) {
  TextureData texture_data;

  if (!data || size <= 0) {
    LOG_WARN("[TEXTURE_DATA] Invalid input data for memory texture loading");
    return texture_data; // Return invalid texture data
  }

  int width, height, channels;
  unsigned char* decoded_data = stbi_load_from_memory(data, size, &width, &height, &channels, 0);

  if (!decoded_data) {
    LOG_WARN("[TEXTURE_DATA] Failed to decode texture from memory: {}", source_info);
    return texture_data; // Return invalid texture data
  }

  texture_data.data = decoded_data;
  texture_data.width = width;
  texture_data.height = height;

  // Set format based on channels
  if (channels == 1) {
    texture_data.format = GL_RED;
  } else if (channels == 3) {
    texture_data.format = GL_RGB;
  } else if (channels == 4) {
    texture_data.format = GL_RGBA;
  } else {
    LOG_WARN("[TEXTURE_DATA] Unsupported channel count: {} for memory texture: {}", channels, source_info);
    texture_data.format = GL_RGB;
  }

  LOG_TRACE("[TEXTURE_DATA] Decoded texture data from memory: {} ({}x{}, {} channels, format: {})",
            source_info, width, height, channels, texture_data.format);

  return texture_data;
}

TextureData TextureManager::load_texture_data_from_assimp(const aiTexture* ai_texture) {
  TextureData texture_data;

  if (!ai_texture) {
    LOG_WARN("[TEXTURE_DATA] aiTexture is null");
    return texture_data; // Return invalid texture data
  }

  LOG_TRACE("[TEXTURE_DATA] Loading embedded texture, height: {}, format: '{}'",
            ai_texture->mHeight, ai_texture->achFormatHint);

  // Check if texture is compressed (height == 0) or uncompressed (height > 0)
  if (ai_texture->mHeight == 0) {
    // Compressed texture data (PNG, JPG, etc.)
    int width, height, channels;
    unsigned char* data = stbi_load_from_memory(
      reinterpret_cast<const unsigned char*>(ai_texture->pcData),
      ai_texture->mWidth, // mWidth contains the data size for compressed textures
      &width, &height, &channels, 0
    );

    if (!data) {
      LOG_WARN("[TEXTURE_DATA] Failed to decode compressed embedded texture");
      return texture_data; // Return invalid texture data
    }

    texture_data.data = data;
    texture_data.width = width;
    texture_data.height = height;

    // Set format based on channels
    if (channels == 1) {
      texture_data.format = GL_RED;
    } else if (channels == 3) {
      texture_data.format = GL_RGB;
    } else if (channels == 4) {
      texture_data.format = GL_RGBA;
    } else {
      texture_data.format = GL_RGB;
    }

    LOG_TRACE("[TEXTURE_DATA] Decoded compressed embedded texture {}x{} (channels: {}, format: {})",
              width, height, channels, texture_data.format);
  }
  else {
    // Uncompressed texture data (raw ARGB32)
    // Note: We need to copy the data since TextureData expects to own it via stbi_image_free
    int data_size = ai_texture->mWidth * ai_texture->mHeight * 4; // ARGB32 = 4 bytes per pixel
    unsigned char* copied_data = static_cast<unsigned char*>(malloc(data_size));

    if (!copied_data) {
      LOG_ERROR("[TEXTURE_DATA] Failed to allocate memory for uncompressed embedded texture");
      return texture_data; // Return invalid texture data
    }

    memcpy(copied_data, ai_texture->pcData, data_size);

    texture_data.data = copied_data;
    texture_data.width = ai_texture->mWidth;
    texture_data.height = ai_texture->mHeight;
    texture_data.format = GL_BGRA; // Assimp uses BGRA format for uncompressed data
    texture_data.on_destroy = OnDestroy::FREE; // Allocated with malloc

    LOG_TRACE("[TEXTURE_DATA] Copied uncompressed embedded texture {}x{} (ARGB32, format: BGRA)",
              ai_texture->mWidth, ai_texture->mHeight);
  }

  return texture_data;
}

TextureData TextureManager::create_solid_color_data(float r, float g, float b) {
  TextureData texture_data;

  // Create a 1x1 texture with the specified color
  unsigned char* color_data = static_cast<unsigned char*>(malloc(3)); // RGB = 3 bytes
  if (!color_data) {
    LOG_ERROR("[TEXTURE_DATA] Failed to allocate memory for solid color texture");
    return texture_data; // Return invalid texture data
  }

  const auto encode_linear_to_srgb = [](float linear) {
    float clamped = std::clamp(linear, 0.0f, 1.0f);
    float srgb = std::pow(clamped, 1.0f / 2.2f);
    srgb = std::clamp(srgb, 0.0f, 1.0f);
    return static_cast<unsigned char>(srgb * 255.0f + 0.5f);
  };

  color_data[0] = encode_linear_to_srgb(r);
  color_data[1] = encode_linear_to_srgb(g);
  color_data[2] = encode_linear_to_srgb(b);

  texture_data.data = color_data;
  texture_data.width = 1;
  texture_data.height = 1;
  texture_data.format = GL_RGB;
  texture_data.on_destroy = OnDestroy::FREE; // Allocated with malloc

  LOG_TRACE("[TEXTURE_DATA] Created solid color data: linear RGB({}, {}, {})", r, g, b);

  return texture_data;
}

unsigned int TextureManager::create_opengl_texture(const TextureData& data, const TextureParameters& params) {
  if (!data.is_valid()) {
    LOG_WARN("[OPENGL_TEXTURE] Cannot create OpenGL texture from invalid TextureData");
    return 0;
  }

  unsigned int texture_id;
  glGenTextures(1, &texture_id);
  glBindTexture(GL_TEXTURE_2D, texture_id);

  // Set texture parameters
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, params.wrap_s);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, params.wrap_t);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, params.min_filter);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, params.mag_filter);

  // Upload texture data
  glTexImage2D(GL_TEXTURE_2D, 0, data.format, data.width, data.height, 0, data.format, GL_UNSIGNED_BYTE, data.data);

  // Generate mipmaps if requested
  if (params.generate_mipmaps) {
    glGenerateMipmap(GL_TEXTURE_2D);
  }

  LOG_TRACE("[OPENGL_TEXTURE] Created OpenGL texture ID {} ({}x{}, format: {}, mipmaps: {})",
            texture_id, data.width, data.height, data.format, params.generate_mipmaps);

  return texture_id;
}

bool TextureManager::initialize_preview_system() {
  if (preview_initialized_) {
    return true;
  }

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
    LOG_ERROR("FRAMEBUFFER:: Framebuffer is not complete!");
    return false;
  }

  glBindFramebuffer(GL_FRAMEBUFFER, 0);

  // Set up OpenGL state for 3D rendering (shared with thumbnail generation)
  setup_3d_rendering_state();

  preview_initialized_ = true;
  LOG_INFO("3D preview initialized successfully!");
  return true;
}

void TextureManager::cleanup_preview_system() {
  if (preview_initialized_) {
    glDeleteTextures(1, &preview_texture_);
    glDeleteTextures(1, &preview_depth_texture_);
    glDeleteFramebuffers(1, &preview_framebuffer_);
    preview_initialized_ = false;
  }
}

void TextureManager::queue_texture_cleanup(const std::string& file_path) {
  std::lock_guard<std::mutex> lock(cleanup_mutex_);
  cleanup_queue_.emplace(file_path);
  LOG_TRACE("[TEXTURE] Queued cleanup for: {}", file_path);
}

// TODO: Should we cache textures by relative path, so we safe memory 
// and don't need to pass this here
void TextureManager::process_cleanup_queue(const std::string& assets_root_directory) {
  std::lock_guard<std::mutex> lock(cleanup_mutex_);

  while (!cleanup_queue_.empty()) {
    const std::string file_path = cleanup_queue_.front();

    // Remove from texture cache if present
    auto cache_it = texture_cache_.find(file_path);
    if (cache_it != texture_cache_.end()) {
      // Only delete the texture if it's owned by this cache entry (not a shared default icon)
      // default_texture_id > 0 means this entry is using a shared type icon that must not be deleted
      if (cache_it->second.texture_id != 0 && cache_it->second.default_texture_id == 0) {
        LOG_TRACE("[TEXTURE] Processing cleanup for: {} (texture_id: {})",
          file_path, cache_it->second.texture_id);
        glDeleteTextures(1, &cache_it->second.texture_id);
      }
      else if (cache_it->second.default_texture_id > 0) {
        LOG_TRACE("[TEXTURE] Processing cleanup for: {} (keeping default_texture_id: {})",
          file_path, cache_it->second.default_texture_id);
      }
      texture_cache_.erase(cache_it);
    }

    // Delete thumbnail if present for any asset path
    std::string relative = get_relative_path(file_path, assets_root_directory);
    std::filesystem::path thumbnail_path = get_thumbnail_path(relative);
    if (std::filesystem::exists(thumbnail_path)) {
      try {
        std::filesystem::remove(thumbnail_path);
        LOG_TRACE("[TEXTURE] Deleted thumbnail for removed asset: {}", thumbnail_path.string());
      }
      catch (const std::filesystem::filesystem_error& e) {
        LOG_WARN("[TEXTURE] Failed to delete thumbnail {}: {}", thumbnail_path.string(), e.what());
      }
    }

    cleanup_queue_.pop();
  }
}

void TextureManager::clear_texture_cache() {
  std::lock_guard<std::mutex> lock(cleanup_mutex_);

  // Clean up all cached textures (but preserve type icons and default texture)
  for (auto& entry : texture_cache_) {
    // Only delete owned textures, not shared default icons
    if (entry.second.texture_id != 0 && entry.second.default_texture_id == 0) {
      glDeleteTextures(1, &entry.second.texture_id);
    }
  }
  texture_cache_.clear();
}

void TextureManager::print_texture_cache(const std::string& assets_root_directory) const {
  std::lock_guard<std::mutex> lock(cleanup_mutex_);

  LOG_INFO("====== TEXTURE CACHE DUMP ======");
  LOG_INFO("Total entries: {}", texture_cache_.size());

  if (texture_cache_.empty()) {
    LOG_INFO("Cache is empty");
    LOG_INFO("================================");
    return;
  }

  // Count different types of entries
  int loaded_count = 0;
  int use_default_count = 0;
  int failed_count = 0;
  int retry_count_total = 0;

  for (const auto& [path, entry] : texture_cache_) {
    if (entry.loaded) {
      loaded_count++;
    }
    else if (entry.default_texture_id > 0) {
      use_default_count++;
    }
    else {
      failed_count++;
    }
    retry_count_total += entry.retry_count;
  }

  LOG_INFO("Status breakdown:");
  LOG_INFO("  Loaded successfully: {}", loaded_count);
  LOG_INFO("  Using default icon: {}", use_default_count);
  LOG_INFO("  Failed/In progress: {}", failed_count);
  LOG_INFO("  Total retry attempts: {}", retry_count_total);
  LOG_INFO("");

  // Print individual entries
  int entry_num = 1;
  for (const auto& [path, entry] : texture_cache_) {
    std::string filename = std::filesystem::path(path).filename().string();
    std::string status;

    if (entry.loaded) {
      status = "LOADED";
    }
    else if (entry.default_texture_id > 0) {
      status = "DEFAULT";
    }
    else {
      status = "PENDING";
    }

    LOG_INFO("{}. {} [{}]", entry_num++, filename, status);
    LOG_INFO("   Path: {}", get_relative_path(path, assets_root_directory));
    // Show both owned and default texture IDs for debugging
    if (entry.default_texture_id > 0) {
      LOG_INFO("   Using default_texture_id: {}, Size: {}x{}, Retries: {}",
        entry.default_texture_id, entry.width, entry.height, entry.retry_count);
      LOG_INFO("   (owned texture_id: {})", entry.texture_id);
    }
    else {
      LOG_INFO("   TextureID: {}, Size: {}x{}, Retries: {}",
        entry.texture_id, entry.width, entry.height, entry.retry_count);
    }

    if (!entry.file_path.empty() && entry.file_path != path) {
      std::string cache_filename = std::filesystem::path(entry.file_path).filename().string();
      LOG_INFO("   Cache file: {}", cache_filename);
    }

    LOG_INFO("");
  }

  LOG_INFO("================================");
}
