#pragma once

#include <chrono>
#include <exception>
#include <filesystem>
#include <mutex>
#include <queue>
#include <string>
#include <unordered_map>
#include <vector>
#include <memory>
#include <glm/glm.hpp>
#include "imgui.h"

// Only include OpenGL headers if available
#ifdef GLAD_GL_VERSION_3_3
#include <glad/glad.h>
#else
// Forward declare OpenGL types for tests
typedef unsigned int GLenum;
typedef unsigned int GLuint;
#define GL_RGB 0x1907
#define GL_RGBA 0x1908
#define GL_RED 0x1903
#define GL_BGRA 0x80E1
#define GL_CLAMP_TO_EDGE 0x812F
#define GL_REPEAT 0x2901
#define GL_NEAREST 0x2600
#define GL_LINEAR 0x2601
#define GL_LINEAR_MIPMAP_LINEAR 0x2703
#endif

#include "asset.h" // For AssetType and Asset
#include "animation.h" // For Animation2D

// Forward declarations
struct GLFWwindow;
struct aiTexture;
struct SpriteAtlas {
  ImTextureID texture_id = 0;
  ImVec2 atlas_size = ImVec2(1.0f, 1.0f);
};

// Exception class for thumbnail generation failures
class ThumbnailGenerationException : public std::exception {
protected:
    std::string message_;
public:
    explicit ThumbnailGenerationException(const std::string& message) : message_(message) {}
    const char* what() const noexcept override { return message_.c_str(); }
};

// Texture parameters configuration
struct TextureParameters {
    GLenum wrap_s;
    GLenum wrap_t;
    GLenum min_filter;
    GLenum mag_filter;
    bool generate_mipmaps;

    // Preset configurations
    static TextureParameters ui_texture() {
        return {GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE, GL_NEAREST, GL_NEAREST, false};
    }

    static TextureParameters model_texture() {
        return {GL_REPEAT, GL_REPEAT, GL_LINEAR_MIPMAP_LINEAR, GL_LINEAR, true};
    }

    static TextureParameters solid_color() {
        return {GL_REPEAT, GL_REPEAT, GL_LINEAR, GL_LINEAR, false};
    }
};

// Memory cleanup strategy for TextureData
enum class OnDestroy {
    NONE,       // Don't free (data is managed elsewhere)
    FREE,       // Use free() - allocated with malloc/calloc
    STBI_FREE   // Use stbi_image_free() - allocated by stb_image
};

// Intermediate texture data representation (separates data loading from OpenGL creation)
struct TextureData {
    unsigned char* data;
    int width;
    int height;
    GLenum format;
    OnDestroy on_destroy; // How to free memory on destruction

    // Constructor
    TextureData() : data(nullptr), width(0), height(0), format(GL_RGB), on_destroy(OnDestroy::STBI_FREE) {}

    // Move constructor
    TextureData(TextureData&& other) noexcept
        : data(other.data), width(other.width), height(other.height),
          format(other.format), on_destroy(other.on_destroy) {
        other.data = nullptr; // Transfer ownership
    }

    // Move assignment
    TextureData& operator=(TextureData&& other) noexcept {
        if (this != &other) {
            cleanup();
            data = other.data;
            width = other.width;
            height = other.height;
            format = other.format;
            on_destroy = other.on_destroy;
            other.data = nullptr; // Transfer ownership
        }
        return *this;
    }

    // Disable copy operations
    TextureData(const TextureData&) = delete;
    TextureData& operator=(const TextureData&) = delete;

    // Destructor with RAII cleanup
    ~TextureData() {
        cleanup();
    }

    bool is_valid() const {
        return data != nullptr && width > 0 && height > 0;
    }

private:
    void cleanup(); // Implementation in .cpp file
};

// Texture cache entry structure
struct TextureCacheEntry {
  unsigned int texture_id;          // The owned texture ID for this specific asset (deleted during cleanup)
  unsigned int default_texture_id;  // Reference to shared default/type icon texture (never deleted during cleanup)
  std::string file_path;             // TODO: Is this actually used. Maybe remove?
  int width;
  int height;
  int retry_count;                   // Current retry attempts
  bool loaded;                       // Whether texture is successfully loaded

  TextureCacheEntry() : texture_id(0), default_texture_id(0), width(0), height(0), retry_count(0), loaded(false) {}

  /**
   * Returns the texture ID for rendering: default_texture_id if set, otherwise texture_id.
   * Prevents shared type icons from being deleted during asset cleanup.
   */
  unsigned int get_texture_id() const {
    return (default_texture_id > 0) ? default_texture_id : texture_id;
  }
};

class TextureManager {
public:
  // Constructor and destructor
  TextureManager();
  virtual ~TextureManager();

  // Initialization and cleanup
  bool initialize();
  void cleanup();

  SpriteAtlas get_ui_elements_atlas() const;

  // Asset texture management
  unsigned int load_texture(const char* filename);
  unsigned int load_texture(const char* filename, int* out_width, int* out_height);
  unsigned int load_packaged_texture(const char* asset_path);
  const TextureCacheEntry& get_asset_texture(const Asset& asset);
  const std::string& u8_path(const Asset& asset);
  void load_type_textures();
  void cleanup_texture_cache(const std::string& path);
  bool get_texture_dimensions(const std::string& file_path, int& width, int& height);

  // New unified texture loading system
  TextureData load_texture_data_from_file(const std::string& filepath);
  TextureData load_texture_data_from_memory(const unsigned char* data, int size, const std::string& source_info = "memory");
  TextureData load_texture_data_from_assimp(const aiTexture* ai_texture);
  TextureData create_solid_color_data(float r, float g, float b);
  unsigned int create_opengl_texture(const TextureData& data, const TextureParameters& params);

  // Embedded texture loading (replaces function from 3d.cpp)
  unsigned int load_embedded_texture(const aiTexture* ai_texture);

  // 3D model texture management
  unsigned int load_texture_for_model(const std::string& filepath);
  unsigned int create_material_texture(const glm::vec3& diffuse, const glm::vec3& emissive,
    float emissive_intensity);


  // 3D model thumbnail generation
  virtual void generate_3d_model_thumbnail(const std::string& model_path, const std::filesystem::path& thumbnail_path);

  // SVG thumbnail generation
  virtual void generate_svg_thumbnail(const std::filesystem::path& svg_path, const std::filesystem::path& thumbnail_path);

  // Generates a font thumbnail by rasterizing a sample string via stb_truetype and writing a PNG.
  virtual void generate_font_thumbnail(const std::filesystem::path& font_path, const std::filesystem::path& thumbnail_path);

  // Animated GIF loading (on-demand, for preview panel)
  // Return a cached animation if available, otherwise load it and store a weak reference.
  std::shared_ptr<Animation2D> get_or_load_animated_gif(const std::string& filepath);

  // Texture cache cleanup (thread-safe)
  virtual void queue_texture_cleanup(const std::string& file_path);
  void process_cleanup_queue(const std::string& assets_root_directory);
  void clear_texture_cache(); // Clear all cached textures (for path encoding changes)

  // 3D preview system
  bool initialize_preview_system();
  void cleanup_preview_system();
  bool is_preview_initialized() const { return preview_initialized_; }

  // Getters for preview textures (needed by main.cpp for ImGui rendering)
  unsigned int get_preview_texture() const { return preview_texture_; }
  unsigned int get_preview_depth_texture() const { return preview_depth_texture_; }
  unsigned int get_preview_framebuffer() const { return preview_framebuffer_; }

  // Audio control & grid UI icons
  unsigned int get_play_icon() const { return play_icon_; }
  unsigned int get_pause_icon() const { return pause_icon_; }
  unsigned int get_speaker_icon() const { return speaker_icon_; }
  unsigned int get_zoom_in_icon() const { return zoom_in_icon_; }
  unsigned int get_zoom_out_icon() const { return zoom_out_icon_; }
  unsigned int get_settings_icon() const { return settings_icon_; }
  unsigned int get_folder_icon() const { return folder_icon_; }

  // Debug utilities
  void print_texture_cache(const std::string& assets_root_directory) const;

private:
  // Asset thumbnails and icons
  unsigned int default_texture_;
  std::unordered_map<AssetType, unsigned int> type_icons_;
  std::unordered_map<std::string, TextureCacheEntry> texture_cache_;
  std::unordered_map<std::string, std::weak_ptr<Animation2D>> animation_cache_;

  // 3D Preview system
  unsigned int preview_texture_;
  unsigned int preview_depth_texture_;
  unsigned int preview_framebuffer_;
  bool preview_initialized_;

  // Cleanup queue for thread-safe texture cache updates and thumbnail deletion
  std::queue<std::string> cleanup_queue_;
  mutable std::mutex cleanup_mutex_;
  mutable std::mutex animation_mutex_;
  

  // Audio control & grid UI icons
  unsigned int play_icon_;
  unsigned int pause_icon_;
  unsigned int speaker_icon_;
  unsigned int zoom_in_icon_;
  unsigned int zoom_out_icon_;
  unsigned int settings_icon_;
  unsigned int folder_icon_;
  unsigned int ui_elements_texture_;
  int ui_elements_width_;
  int ui_elements_height_;

  bool load_ui_elements_atlas();


  // Helper methods
  void cleanup_all_textures();
  // Internal loader that builds Animation2D for a GIF file path.
  std::shared_ptr<Animation2D> load_animated_gif_internal(const std::string& filepath);
};
