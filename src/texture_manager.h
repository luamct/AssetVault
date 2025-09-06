#pragma once

#include <filesystem>
#include <mutex>
#include <queue>
#include <string>
#include <unordered_map>
#include <vector>
#include <glm/glm.hpp>

#include "asset.h" // For AssetType and Asset

// Texture cache entry structure
struct TextureCacheEntry {
  unsigned int texture_id;
  std::string file_path;  // TODO: Is this actually used. Maybe remove?
  int width;
  int height;
  int retry_count;        // Current retry attempts
  bool loaded;           // Whether texture is successfully loaded
  bool use_default;      // Whether to use default icon (failed/animation-only)

  TextureCacheEntry() : texture_id(0), width(0), height(0), retry_count(0), loaded(false), use_default(false) {}
};

class TextureManager {
public:
  // Constructor and destructor
  TextureManager();
  ~TextureManager();

  // Initialization and cleanup
  bool initialize();
  void cleanup();

  // Asset texture management
  unsigned int load_texture(const char* filename);
  unsigned int load_texture(const char* filename, int* out_width, int* out_height);
  unsigned int load_svg_texture(const char* filename, int target_width, int target_height, int* actual_width = nullptr, int* actual_height = nullptr);
  TextureCacheEntry get_asset_texture(const Asset& asset);
  const std::string& u8_path(const Asset& asset);
  void load_type_textures();
  void cleanup_texture_cache(const std::string& path);
  bool get_texture_dimensions(const std::string& file_path, int& width, int& height);

  // 3D model texture management
  unsigned int load_texture_for_model(const std::string& filepath);
  unsigned int create_solid_color_texture(float r, float g, float b);
  unsigned int create_material_texture(const glm::vec3& diffuse, const glm::vec3& emissive, float emissive_intensity);


  // Thumbnail generation result types
  enum class ThumbnailResult {
    SUCCESS,              // Thumbnail generated successfully
    NO_GEOMETRY,          // Animation-only file (no renderable geometry)
    MISSING_TEXTURES,     // Model has missing texture files
    OPENGL_ERROR,         // OpenGL error during rendering
    FILE_NOT_FOUND,       // Model file doesn't exist
    ASSIMP_ERROR,         // Assimp failed to load model
    OTHER_ERROR           // Other unspecified error
  };

  // 3D model thumbnail generation
  ThumbnailResult generate_3d_model_thumbnail(const std::string& model_path, const std::string& relative_path);

  // Texture cache invalidation (thread-safe)
  void queue_texture_invalidation(const std::string& file_path);
  void process_invalidation_queue();
  void clear_texture_cache(); // Clear all cached textures (for path encoding changes)

  // 3D preview system
  bool initialize_preview_system();
  void cleanup_preview_system();
  bool is_preview_initialized() const { return preview_initialized_; }

  // Getters for preview textures (needed by main.cpp for ImGui rendering)
  unsigned int get_preview_texture() const { return preview_texture_; }
  unsigned int get_preview_depth_texture() const { return preview_depth_texture_; }
  unsigned int get_preview_framebuffer() const { return preview_framebuffer_; }
  unsigned int get_preview_shader() const { return preview_shader_; }

  // Audio control icons
  unsigned int get_play_icon() const { return play_icon_; }
  unsigned int get_pause_icon() const { return pause_icon_; }
  unsigned int get_speaker_icon() const { return speaker_icon_; }

private:
  // Asset thumbnails and icons
  unsigned int default_texture_;
  std::unordered_map<AssetType, unsigned int> type_icons_;
  std::unordered_map<std::string, TextureCacheEntry> texture_cache_;

  // 3D Preview system
  unsigned int preview_texture_;
  unsigned int preview_depth_texture_;
  unsigned int preview_framebuffer_;
  unsigned int preview_shader_;
  bool preview_initialized_;

  // Invalidation queue for thread-safe texture cache updates
  std::queue<std::string> invalidation_queue_;
  mutable std::mutex invalidation_mutex_;

  // Audio control icons
  unsigned int play_icon_;
  unsigned int pause_icon_;
  unsigned int speaker_icon_;

  // Helper methods
  void cleanup_all_textures();
};
