#ifdef _WIN32
#include <windows.h>
#endif
#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <queue>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "theme.h"

#include "database.h"
#include "file_watcher.h"
#include "index.h"
#include "utils.h"
#include "3d.h"

// Include stb_image for PNG loading
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

// Include NanoSVG for SVG support
#define NANOSVG_IMPLEMENTATION
#include "nanosvg.h"
#define NANOSVGRAST_IMPLEMENTATION
#include "nanosvgrast.h"

// Constants
constexpr int WINDOW_WIDTH = 1920;
constexpr int WINDOW_HEIGHT = 1080;
constexpr float SEARCH_BOX_WIDTH = 375.0f;
constexpr float SEARCH_BOX_HEIGHT = 60.0f;
constexpr float THUMBNAIL_SIZE = 180.0f;
constexpr float GRID_SPACING = 30.0f;

// Debug flags
constexpr bool DEBUG_FORCE_DB_CLEAR = false; // Set to true to force database clearing and reindexing
constexpr float TEXT_MARGIN = 20.0f;         // Space below thumbnail for text positioning
constexpr float TEXT_HEIGHT = 20.0f;         // Height reserved for text
constexpr float ICON_SCALE = 0.5f;           // Icon occupies 50% of the thumbnail area

// Preview panel layout constants
constexpr float PREVIEW_RIGHT_MARGIN = 40.0f;     // Margin from window right edge
constexpr float PREVIEW_INTERNAL_PADDING = 30.0f; // Internal padding within preview panel

// Temporary color definitions while migrating to Theme:: namespace
constexpr ImVec4 COLOR_HEADER_TEXT = ImVec4(0.2f, 0.7f, 0.9f, 1.0f);
constexpr ImVec4 COLOR_LABEL_TEXT = ImVec4(0.2f, 0.2f, 0.8f, 1.0f);
constexpr ImVec4 COLOR_SECONDARY_TEXT = ImVec4(0.6f, 0.6f, 0.6f, 1.0f);
constexpr ImVec4 COLOR_DISABLED_TEXT = ImVec4(0.5f, 0.5f, 0.5f, 1.0f);
constexpr ImVec4 COLOR_WARNING_TEXT = ImVec4(0.9f, 0.7f, 0.2f, 1.0f);
constexpr ImVec4 COLOR_TRANSPARENT = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
constexpr ImVec4 COLOR_SEMI_TRANSPARENT = ImVec4(0.0f, 0.0f, 0.0f, 0.3f);
constexpr ImU32 COLOR_WHITE = IM_COL32(255, 255, 255, 255);
constexpr ImU32 COLOR_TRANSPARENT_32 = IM_COL32(0, 0, 0, 0);
constexpr ImU32 COLOR_BORDER_GRAY = IM_COL32(150, 150, 150, 255);

// Global variables for search and UI state
// static bool show_search_results = false;  // Unused variable
// static unsigned int thumbnail_texture = 0; // Unused variable

// Texture cache for loaded images
struct TextureCacheEntry {
  unsigned int texture_id;
  std::string file_path;
  int width;
  int height;

  TextureCacheEntry() : texture_id(0), width(0), height(0) {}
};

// Global variables
std::vector<FileInfo> g_assets;
std::atomic<bool> g_assets_updated(false);
std::atomic<bool> g_initial_scan_complete(false);
std::atomic<bool> g_initial_scan_in_progress(false);
std::atomic<float> g_scan_progress(0.0f);
std::atomic<size_t> g_files_processed(0);
std::atomic<size_t> g_total_files_to_process(0);
AssetDatabase g_database;
FileWatcher g_file_watcher;
unsigned int g_default_texture = 0;

// Thread-safe event queue for file watcher events
std::queue<FileEvent> g_pending_file_events;
std::mutex g_events_mutex;

// Selection state

// Type-specific textures
std::unordered_map<AssetType, unsigned int> g_texture_icons;

// Texture cache
std::unordered_map<std::string, TextureCacheEntry> g_texture_cache;

bool load_roboto_font(ImGuiIO& io) {
  // Load embedded Roboto font from external/fonts directory
  ImFont* font = io.Fonts->AddFontFromFileTTF("external/fonts/Roboto-Regular.ttf", 24.0f);
  if (font) {
    std::cout << "Roboto font loaded successfully!\n";
    return true;
  }

  // If embedded font fails to load, log error and use default font
  std::cerr << "Failed to load embedded Roboto font. Check that external/fonts/Roboto-Regular.ttf exists.\n";
  return false;
}

// Function to load texture from file
unsigned int load_texture(const char* filename) {
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

// Function to load SVG texture from file
unsigned int load_svg_texture(
  const char* filename, int target_width = 512, int target_height = 512, int* out_width = nullptr,
  int* out_height = nullptr) {
  std::cout << "Loading SVG: " << filename << std::endl;

  // Parse SVG directly from file like the nanosvg examples do
  NSVGimage* image = nsvgParseFromFile(filename, "px", 96.0f);
  if (!image) {
    std::cerr << "Failed to parse SVG: " << filename << '\n';
    return 0;
  }

  std::cout << "SVG parsed successfully. Original size: " << image->width << "x" << image->height << std::endl;

  // Store original dimensions if requested
  if (out_width)
    *out_width = static_cast<int>(image->width);
  if (out_height)
    *out_height = static_cast<int>(image->height);

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

// Function to load type-specific textures
void load_type_textures() {
  const std::unordered_map<AssetType, const char*> texture_paths = {
      {AssetType::Texture, "images/texture.png"}, {AssetType::Model, "images/model.png"}, {AssetType::Sound, "images/sound.png"}, {AssetType::Font, "images/font.png"}, {AssetType::Shader, "images/document.png"}, {AssetType::Document, "images/document.png"}, {AssetType::Archive, "images/document.png"}, {AssetType::Directory, "images/folder.png"}, {AssetType::Auxiliary, "images/unknown.png"}, {AssetType::Unknown, "images/unknown.png"} };

  for (const auto& [type, path] : texture_paths) {
    unsigned int texture_id = load_texture(path);
    g_texture_icons[type] = texture_id;
    if (texture_id == 0) {
      std::cerr << "Failed to load type texture: " << path << '\n';
    }
  }
}

// Function to calculate aspect-ratio-preserving dimensions with upscaling limit
ImVec2 calculate_thumbnail_size(
  int original_width, int original_height, float max_width, float max_height, float max_upscale_factor = 3.0f) {
  float aspect_ratio = static_cast<float>(original_width) / static_cast<float>(original_height);

  float calculated_width = max_width;
  float calculated_height = max_width / aspect_ratio;
  if (calculated_height > max_height) {
    calculated_height = max_height;
    calculated_width = max_height * aspect_ratio;
  }

  // Limit upscaling to the specified factor
  float width_scale = calculated_width / original_width;
  float height_scale = calculated_height / original_height;
  if (width_scale > max_upscale_factor || height_scale > max_upscale_factor) {
    float scale_factor = std::min(max_upscale_factor, std::min(width_scale, height_scale));
    calculated_width = original_width * scale_factor;
    calculated_height = original_height * scale_factor;
  }

  return ImVec2(calculated_width, calculated_height);
}

// Function to get or load texture for an asset
unsigned int get_asset_texture(const FileInfo& asset) {
  // For non-texture assets, return type-specific icon
  if (asset.type != AssetType::Texture) {
    auto it = g_texture_icons.find(asset.type);
    if (it != g_texture_icons.end()) {
      return it->second;
    }
    return g_default_texture;
  }

  // Check if texture is already cached
  auto it = g_texture_cache.find(asset.full_path);
  if (it != g_texture_cache.end()) {
    return it->second.texture_id;
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
  TextureCacheEntry& entry = g_texture_cache[asset.full_path];
  entry.texture_id = texture_id;
  entry.file_path = asset.full_path;
  entry.width = width;
  entry.height = height;

  return texture_id;
}

// Function to get cached texture dimensions
bool get_texture_dimensions(const std::string& file_path, int& width, int& height) {
  auto it = g_texture_cache.find(file_path);
  if (it != g_texture_cache.end()) {
    width = it->second.width;
    height = it->second.height;
    return true;
  }
  return false;
}

// Function to check if asset matches search terms
bool asset_matches_search(const FileInfo& asset, const std::string& search_query) {
  if (search_query.empty()) {
    return true; // Show all assets when search is empty
  }

  std::string query_lower = to_lowercase(search_query);
  std::string name_lower = to_lowercase(asset.name);
  std::string extension_lower = to_lowercase(asset.extension);
  std::string path_lower = to_lowercase(asset.full_path);

  // Split search query into terms (space-separated)
  std::vector<std::string> search_terms;
  std::stringstream ss(query_lower);
  std::string search_term;
  while (ss >> search_term) {
    if (!search_term.empty()) {
      search_terms.push_back(search_term);
    }
  }

  // All terms must match (AND logic)
  for (const auto& term : search_terms) {
    bool term_matches = name_lower.find(term) != std::string::npos || extension_lower.find(term) != std::string::npos ||
      path_lower.find(term) != std::string::npos;

    if (!term_matches) {
      return false;
    }
  }

  return true;
}

// Search state structure
struct SearchState {
  bool initial_filter_applied = false;

  char buffer[256] = "";
  std::string last_buffer = "";

  // UI state
  std::vector<FileInfo> filtered_assets;
  int selected_asset_index = -1; // -1 means no selection

  // Model preview state
  Model current_model;
};

// Function to filter assets based on search query
void filter_assets(const std::string& search_query, SearchState& search_state) {
  auto start_time = std::chrono::high_resolution_clock::now();

  search_state.filtered_assets.clear();
  search_state.selected_asset_index = -1; // Clear selection when search results change

  constexpr size_t MAX_RESULTS = 1000; // Limit results to prevent UI blocking
  size_t total_assets = g_assets.size();
  size_t filtered_count = 0;

  for (const auto& asset : g_assets) {
    // Skip auxiliary files - they should never appear in search results
    if (asset.type == AssetType::Auxiliary) {
      continue;
    }

    if (asset_matches_search(asset, search_query)) {
      search_state.filtered_assets.push_back(asset);
      filtered_count++;

      // Stop at maximum results to prevent UI blocking
      if (search_state.filtered_assets.size() >= MAX_RESULTS) {
        break;
      }
    }
  }

  auto end_time = std::chrono::high_resolution_clock::now();
  auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
  double duration_ms = duration.count() / 1000.0;

  std::cout << "DEBUG SEARCH: \"" << search_query << "\" | Results: " << filtered_count << "/" << total_assets;
  if (search_state.filtered_assets.size() >= MAX_RESULTS) {
    std::cout << " | Time: " << std::fixed << std::setprecision(2) << duration_ms << "ms [TRUNCATED]";
  }
  else {
    std::cout << " | Time: " << std::fixed << std::setprecision(2) << duration_ms << "ms";
  }
  std::cout << std::endl;
}

// Wrapper function to call the reindexing from index.cpp
void reindex() {
  reindex_new_or_modified(
    g_database, g_assets, g_assets_updated, g_initial_scan_complete, g_initial_scan_in_progress, g_scan_progress,
    g_files_processed, g_total_files_to_process);
}

// Helper function to clean up texture cache entry for a specific path
void cleanup_texture_cache(const std::string& path) {
  auto cache_it = g_texture_cache.find(path);
  if (cache_it != g_texture_cache.end()) {
    if (cache_it->second.texture_id != 0) {
      glDeleteTextures(1, &cache_it->second.texture_id);
    }
    g_texture_cache.erase(cache_it);
  }
}

// File event callback function (runs on background thread)
// Only queues events - all processing happens on main thread
void on_file_event(const FileEvent& event) {
  std::lock_guard<std::mutex> lock(g_events_mutex);
  g_pending_file_events.push(event);
}

// Process pending file events on main thread (thread-safe)
// Uses unified AssetIndexer for consistent processing
void process_pending_file_events() {
  std::queue<FileEvent> events_to_process;

  // Quickly extract all pending events under lock
  {
    std::lock_guard<std::mutex> lock(g_events_mutex);
    events_to_process.swap(g_pending_file_events);
  }

  // Create indexer for consistent processing (same as initial scan)
  static AssetIndexer indexer("assets");

  // Process events without holding the lock
  bool assets_changed = false;
  while (!events_to_process.empty()) {
    FileEvent event = events_to_process.front();
    events_to_process.pop();

    switch (event.type) {
    case FileEventType::Created:
      std::cout << "Created event: " << event.path << std::endl;
    case FileEventType::Modified:
    {
      std::cout << "Modified event: " << event.path << std::endl;
      try {
        // Clear texture cache for modified files so they can be reloaded
        if (std::filesystem::is_regular_file(event.path)) {
          cleanup_texture_cache(event.path);
        }

        // Use unified indexer with event timestamp
        FileInfo file_info = indexer.process_file(event.path, event.timestamp);

        // Save to database with consistent logic
        if (indexer.save_to_database(g_database, file_info)) {
          assets_changed = true;
        }
      }
      catch (const std::exception& e) {
        std::cerr << "Error processing file event for " << event.path << ": " << e.what() << std::endl;
      }
      break;
    }
    case FileEventType::Deleted:
    {
      std::cout << "Deleted event: " << event.path << std::endl;
      // Clean up texture cache for deleted file (must be done on main thread)
      cleanup_texture_cache(event.path);

      // Use indexer's delete helper for consistent logic
      if (indexer.delete_from_database(g_database, event.path)) {
        assets_changed = true;
      }
      break;
    }
    case FileEventType::Renamed:
    {
      std::cout << "Renamed event: " << event.path << std::endl;
      // Clean up texture cache for old path (must be done on main thread)
      cleanup_texture_cache(event.old_path);

      try {
        // Delete old entry
        indexer.delete_from_database(g_database, event.old_path);

        // Create new entry using unified indexer
        FileInfo file_info = indexer.process_file(event.path, event.timestamp);
        if (indexer.save_to_database(g_database, file_info)) {
          assets_changed = true;
        }
      }
      catch (const std::exception& e) {
        std::cerr << "Error processing rename event from " << event.old_path << " to " << event.path << ": " << e.what() << std::endl;
      }
      break;
    }
    default:
      break;
    }
  }

  // Only set flag once after processing all events
  if (assets_changed) {
    g_assets_updated = true;
  }
}

int main() {
  // Initialize database
  std::cout << "Initializing database...\n";
  if (!g_database.initialize("db/assets.db")) {
    std::cerr << "Failed to initialize database\n";
    return -1;
  }

  // Debug: Force clear database if flag is set
  if (DEBUG_FORCE_DB_CLEAR) {
    std::cout << "DEBUG: Forcing database clear for testing...\n";
    g_database.clear_all_assets();
  }

  // Smart scanning - no longer clearing database on startup
  std::cout << "Using smart incremental scanning...\n";

  // Start background initial scan (non-blocking)
  std::thread scan_thread(reindex);
  scan_thread.detach(); // Let it run independently

  // Initialize GLFW
  if (!glfwInit()) {
    std::cerr << "Failed to initialize GLFW\n";
    return -1;
  }

  // Create window
  GLFWwindow* window = glfwCreateWindow(WINDOW_WIDTH, WINDOW_HEIGHT, "Asset Inventory", nullptr, nullptr);
  if (!window) {
    std::cerr << "Failed to create GLFW window\n";
    glfwTerminate();
    return -1;
  }

  glfwMakeContextCurrent(window);
  glfwSwapInterval(1); // Enable vsync

  // Initialize GLAD
  if (!gladLoadGLLoader((GLADloadproc) glfwGetProcAddress)) {
    std::cerr << "Failed to initialize GLAD\n";
    glfwTerminate();
    return -1;
  }

  // Initialize 3D preview
  if (!initialize_3d_preview()) {
    std::cerr << "Failed to initialize 3D preview\n";
    glfwTerminate();
    return -1;
  }

  // Initialize Dear ImGui
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;

  // Disable imgui.ini file - we'll handle window positioning in code
  io.IniFilename = nullptr;

  // Ensure proper input handling for cursor blinking
  io.ConfigInputTextCursorBlink = true;

  // Load Roboto font
  load_roboto_font(io);

  // Setup light and fun theme
  Theme::setup_light_fun_theme();

  // Setup Platform/Renderer backends
  ImGui_ImplGlfw_InitForOpenGL(window, true);
  ImGui_ImplOpenGL3_Init("#version 330");

  // Load default texture (generic icon)
  g_default_texture = load_texture("images/texture.png");
  if (g_default_texture == 0) {
    std::cerr << "Warning: Could not load default texture\n";
  }

  // Load type-specific textures
  load_type_textures();

  // Search state
  SearchState search_state;

  // Main loop
  double last_time = glfwGetTime();
  while (!glfwWindowShouldClose(window)) {

    double current_time = glfwGetTime();
    io.DeltaTime = (float) (current_time - last_time);
    last_time = current_time;

    glfwPollEvents();

    // Start file watcher after initial scan completes
    if (g_initial_scan_complete && !g_file_watcher.is_watching()) {
      std::cout << "Starting file watcher...\n";
      if (g_file_watcher.start_watching("assets", on_file_event)) {
        std::cout << "File watcher started successfully\n";
      }
      else {
        std::cerr << "Failed to start file watcher\n";
      }
    }

    // Render 3D preview to framebuffer BEFORE starting ImGui frame
    if (g_preview_initialized) {
      // Calculate the size for the right panel (same as 2D previews)
      float right_panel_width = (ImGui::GetIO().DisplaySize.x * 0.25f) - PREVIEW_RIGHT_MARGIN;
      float avail_width = right_panel_width - PREVIEW_INTERNAL_PADDING;
      float avail_height = avail_width; // Square aspect ratio

      int fb_width = static_cast<int>(avail_width);
      int fb_height = static_cast<int>(avail_height);

      // Render the 3D preview
      render_3d_preview(fb_width, fb_height, search_state.current_model);
    }

    // Start the Dear ImGui frame
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    // Process any pending file events from background threads
    process_pending_file_events();

    // Check if assets were updated and refresh the list
    if (g_assets_updated.exchange(false)) {
      g_assets = g_database.get_all_assets();
      // Re-apply current search filter to include new assets
      filter_assets(search_state.buffer, search_state);
    }

    // Apply initial filter when we first have assets
    if (!search_state.initial_filter_applied && !g_assets.empty()) {
      filter_assets(search_state.buffer, search_state);
      search_state.last_buffer = search_state.buffer;
      search_state.initial_filter_applied = true;
    }

    // Create main window
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
    ImGui::Begin(
      "Asset Inventory", nullptr,
      ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
      ImGuiWindowFlags_NoCollapse);

    // Calculate panel sizes
    float window_width = ImGui::GetWindowSize().x;
    float window_height = ImGui::GetWindowSize().y;
    float left_width = window_width * 0.75f;
    float right_width = window_width * 0.25f - PREVIEW_RIGHT_MARGIN;
    float top_height = window_height * 0.15f;
    float bottom_height = window_height * 0.85f - 20.0f; // Account for some padding

    // ============ TOP LEFT: Search Box ============
    ImGui::BeginChild("SearchRegion", ImVec2(left_width, top_height), true);

    // Get the actual usable content area (accounts for child window borders/padding)
    ImVec2 content_region = ImGui::GetContentRegionAvail();

    // Calculate centered position within content region
    float content_search_x = (content_region.x - SEARCH_BOX_WIDTH) * 0.5f;
    float content_search_y = (content_region.y - SEARCH_BOX_HEIGHT) * 0.5f;

    // Ensure we have a minimum Y position
    if (content_search_y < 5.0f) {
      content_search_y = 5.0f;
    }

    // Get screen position for drawing (content area start + our offset)
    ImVec2 content_start = ImGui::GetCursorScreenPos();
    ImVec2 capsule_min(content_start.x + content_search_x, content_start.y + content_search_y);
    ImVec2 capsule_max(capsule_min.x + SEARCH_BOX_WIDTH, capsule_min.y + SEARCH_BOX_HEIGHT);

    // Draw capsule background
    ImGui::GetWindowDrawList()->AddRectFilled(
      capsule_min, capsule_max,
      COLOR_WHITE, // White background
      25.0f        // Rounded corners
    );

    // Position text input - ImGui text inputs are positioned by their center, not top-left
    float text_input_x = content_search_x + 40;                       // 40px padding from left edge of capsule
    float text_input_y = content_search_y + SEARCH_BOX_HEIGHT * 0.5f; // Center of capsule

    ImGui::SetCursorPos(ImVec2(text_input_x, text_input_y));
    ImGui::PushItemWidth(SEARCH_BOX_WIDTH - 40); // Leave 20px padding on each side

    // Remove borders from text input
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, COLOR_TRANSPARENT_32); // Transparent background

    ImGui::InputText("##Search", search_state.buffer, sizeof(search_state.buffer), ImGuiInputTextFlags_EnterReturnsTrue);

    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
    ImGui::PopItemWidth();

    // Only filter if search terms have changed
    if (std::string(search_state.buffer) != search_state.last_buffer) {
      filter_assets(search_state.buffer, search_state);
      search_state.last_buffer = search_state.buffer;
    }

    ImGui::EndChild();

    // ============ TOP RIGHT: Progress and Messages ============
    ImGui::SameLine();
    ImGui::BeginChild("ProgressRegion", ImVec2(right_width, top_height), true);

    // Progress bar for scanning (only show when actually scanning)
    if (g_initial_scan_in_progress) {
      ImGui::TextColored(COLOR_HEADER_TEXT, "Indexing Assets");

      // Progress bar data
      float progress = g_scan_progress.load();
      size_t processed = g_files_processed.load();
      size_t total = g_total_files_to_process.load();

      // Draw progress bar without text overlay
      ImGui::ProgressBar(progress, ImVec2(-1.0f, 0.0f), "");

      // Overlay centered text on the progress bar
      char progress_text[64];
      snprintf(progress_text, sizeof(progress_text), "%zu/%zu", processed, total);

      ImVec2 text_size = ImGui::CalcTextSize(progress_text);
      ImVec2 progress_bar_screen_pos = ImGui::GetItemRectMin();
      ImVec2 progress_bar_screen_size = ImGui::GetItemRectSize();

      // Center text on progress bar
      ImVec2 text_pos = ImVec2(
        progress_bar_screen_pos.x + (progress_bar_screen_size.x - text_size.x) * 0.5f,
        progress_bar_screen_pos.y + (progress_bar_screen_size.y - text_size.y) * 0.5f);

      ImGui::GetWindowDrawList()->AddText(text_pos, COLOR_WHITE, progress_text);
    }
    // No "Ready" text - keep panel empty when not indexing

    ImGui::EndChild();

    // ============ BOTTOM LEFT: Search Results ============
    ImGui::BeginChild("AssetGrid", ImVec2(left_width, bottom_height), true);

    // Calculate grid layout upfront since all items have the same size
    float available_width = left_width - 20.0f;                     // Account for padding
    float item_height = THUMBNAIL_SIZE + TEXT_MARGIN + TEXT_HEIGHT; // Full item height including text
    // Add GRID_SPACING to available width since we don't need spacing after the
    // last item
    int columns = static_cast<int>((available_width + GRID_SPACING) / (THUMBNAIL_SIZE + GRID_SPACING));
    if (columns < 1)
      columns = 1;

    // Display filtered assets in a proper grid
    for (size_t i = 0; i < search_state.filtered_assets.size(); i++) {
      // Calculate grid position
      int row = static_cast<int>(i) / columns;
      int col = static_cast<int>(i) % columns;

      // Calculate absolute position for this grid item
      float x_pos = col * (THUMBNAIL_SIZE + GRID_SPACING);
      float y_pos = row * (item_height + GRID_SPACING);

      // Set cursor to the calculated position
      ImGui::SetCursorPos(ImVec2(x_pos, y_pos));

      ImGui::BeginGroup();

      // Get texture for this asset
      unsigned int asset_texture = get_asset_texture(search_state.filtered_assets[i]);

      // Calculate display size based on asset type
      ImVec2 display_size(THUMBNAIL_SIZE, THUMBNAIL_SIZE);
      bool is_texture = (search_state.filtered_assets[i].type == AssetType::Texture && asset_texture != 0);
      if (is_texture) {
        int width, height;
        if (get_texture_dimensions(search_state.filtered_assets[i].full_path, width, height)) {
          display_size =
            calculate_thumbnail_size(width, height, THUMBNAIL_SIZE, THUMBNAIL_SIZE, 3.0f); // 3x upscaling for grid
        }
      }
      else {
        // For type icons, use a fixed fraction of the thumbnail size
        display_size = ImVec2(THUMBNAIL_SIZE * ICON_SCALE, THUMBNAIL_SIZE * ICON_SCALE);
      }

      // Create a fixed-size container for consistent layout
      ImVec2 container_size(THUMBNAIL_SIZE,
        THUMBNAIL_SIZE + TEXT_MARGIN + TEXT_HEIGHT); // Thumbnail + text area
      ImVec2 container_pos = ImGui::GetCursorScreenPos();

      // Draw background for the container (same as app background)
      ImGui::GetWindowDrawList()->AddRectFilled(
        container_pos, ImVec2(container_pos.x + container_size.x, container_pos.y + container_size.y),
        Theme::ToImU32(Theme::BACKGROUND_LIGHT_BLUE_1));

      // Center the image/icon in the thumbnail area
      float image_x_offset = (THUMBNAIL_SIZE - display_size.x) * 0.5f;
      float image_y_offset = (THUMBNAIL_SIZE - display_size.y) * 0.5f;
      ImVec2 image_pos(container_pos.x + image_x_offset, container_pos.y + image_y_offset);

      ImGui::PushStyleColor(ImGuiCol_Button, COLOR_TRANSPARENT);
      ImGui::PushStyleColor(ImGuiCol_ButtonActive, COLOR_TRANSPARENT);
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered, COLOR_SEMI_TRANSPARENT);

      // Display thumbnail image
      if (asset_texture != 0) {
        ImGui::SetCursorScreenPos(image_pos);
        if (ImGui::ImageButton(
          ("##Thumbnail" + std::to_string(i)).c_str(), (ImTextureID) (intptr_t) asset_texture, display_size)) {
          search_state.selected_asset_index = static_cast<int>(i);
          std::cout << "Selected: " << search_state.filtered_assets[i].name << '\n';
        }
      }
      else {
        // Fallback: colored button if texture failed to load
        ImGui::SetCursorScreenPos(image_pos);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8.0f);
        if (ImGui::Button(("##Thumbnail" + std::to_string(i)).c_str(), display_size)) {
          search_state.selected_asset_index = static_cast<int>(i);
          std::cout << "Selected: " << search_state.filtered_assets[i].name << '\n';
        }
        ImGui::PopStyleVar();

        // Add a background to simulate thumbnail (same as app background)
        ImGui::GetWindowDrawList()->AddRectFilled(
          ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), Theme::ToImU32(Theme::BACKGROUND_LIGHT_BLUE_1));
      }

      ImGui::PopStyleColor(3);

      // Position text at the bottom of the container
      ImGui::SetCursorScreenPos(ImVec2(container_pos.x, container_pos.y + THUMBNAIL_SIZE + TEXT_MARGIN));

      // Asset name below thumbnail
      std::string truncated_name = truncate_filename(search_state.filtered_assets[i].name);
      ImGui::SetCursorPosX(
        ImGui::GetCursorPosX() + (THUMBNAIL_SIZE - ImGui::CalcTextSize(truncated_name.c_str()).x) * 0.5f);
      ImGui::TextWrapped("%s", truncated_name.c_str());

      ImGui::EndGroup();
    }

    // Show message if no assets found
    if (search_state.filtered_assets.empty()) {
      if (g_initial_scan_in_progress) {
        ImGui::TextColored(COLOR_HEADER_TEXT, "Scanning assets...");
        ImGui::TextColored(COLOR_SECONDARY_TEXT, "Please wait while we index your assets directory.");
      }
      else if (g_assets.empty()) {
        ImGui::TextColored(COLOR_DISABLED_TEXT, "No assets found. Add files to the 'assets' directory.");
      }
      else {
        ImGui::TextColored(COLOR_DISABLED_TEXT, "No assets match your search.");
      }
    }
    else if (search_state.filtered_assets.size() >= 1000) {
      // Show truncation message
      ImGui::Spacing();
      ImGui::TextColored(COLOR_WARNING_TEXT, "Showing first 1000 results. Use search to narrow down.");
    }

    ImGui::EndChild();

    // ============ BOTTOM RIGHT: Preview Panel ============
    ImGui::SameLine();
    ImGui::BeginChild("AssetPreview", ImVec2(right_width, bottom_height), true);

    // Use fixed panel dimensions for stable calculations
    float avail_width = right_width - PREVIEW_INTERNAL_PADDING; // Account for ImGui padding and margins
    float avail_height = avail_width;                           // Square aspect ratio for preview area

    if (search_state.selected_asset_index >= 0) {
      const FileInfo& selected_asset = search_state.filtered_assets[search_state.selected_asset_index];

      // Check if selected asset is a model
      if (selected_asset.type == AssetType::Model && g_preview_initialized) {
        // Load the model if it's different from the currently loaded one
        if (selected_asset.full_path != search_state.current_model.path) {
          std::cout << "=== Loading Model in Main ===" << std::endl;
          std::cout << "Selected asset: " << selected_asset.full_path << std::endl;
          Model model;
          if (load_model(selected_asset.full_path, model)) {
            set_current_model(search_state.current_model, model);
            std::cout << "Model loaded successfully in main" << std::endl;
          }
          else {
            std::cout << "Failed to load model in main" << std::endl;
          }
          std::cout << "===========================" << std::endl;
        }

        // Get the current model for displaying info
        const Model& current_model = get_current_model(search_state.current_model);

        // 3D Preview Viewport for models
        ImVec2 viewport_size(avail_width, avail_height);

        // Center the viewport in the panel (same logic as 2D previews)
        ImVec2 container_pos = ImGui::GetCursorScreenPos();
        float image_x_offset = (avail_width - viewport_size.x) * 0.5f;
        float image_y_offset = (avail_height - viewport_size.y) * 0.5f;
        ImVec2 image_pos(container_pos.x + image_x_offset, container_pos.y + image_y_offset);
        ImGui::SetCursorScreenPos(image_pos);

        // Draw border around the viewport
        ImVec2 border_min = image_pos;
        ImVec2 border_max(border_min.x + viewport_size.x, border_min.y + viewport_size.y);
        ImGui::GetWindowDrawList()->AddRect(border_min, border_max, COLOR_BORDER_GRAY, 8.0f, 0, 1.0f);

        // Display the 3D viewport
        ImGui::Image((ImTextureID) (intptr_t) g_preview_texture, viewport_size);

        // Restore cursor for info below
        ImGui::SetCursorScreenPos(container_pos);
        ImGui::Dummy(ImVec2(0, avail_height + 10));

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // 3D Model information
        ImGui::TextColored(COLOR_LABEL_TEXT, "Path: ");
        ImGui::SameLine();
        ImGui::TextWrapped("%s", format_display_path(selected_asset.full_path).c_str());

        ImGui::TextColored(COLOR_LABEL_TEXT, "Extension: ");
        ImGui::SameLine();
        ImGui::Text("%s", selected_asset.extension.c_str());

        ImGui::TextColored(COLOR_LABEL_TEXT, "Type: ");
        ImGui::SameLine();
        ImGui::Text("%s", get_asset_type_string(selected_asset.type).c_str());

        ImGui::TextColored(COLOR_LABEL_TEXT, "Size: ");
        ImGui::SameLine();
        ImGui::Text("%s", format_file_size(selected_asset.size).c_str());

        // Display vertex and face counts from the loaded model
        if (current_model.loaded) {
          int vertex_count =
            static_cast<int>(current_model.vertices.size() / 8);             // 8 floats per vertex (3 pos + 3 normal + 2 tex)
          int face_count = static_cast<int>(current_model.indices.size() / 3); // 3 indices per triangle

          ImGui::TextColored(COLOR_LABEL_TEXT, "Vertices: ");
          ImGui::SameLine();
          ImGui::Text("%d", vertex_count);

          ImGui::TextColored(COLOR_LABEL_TEXT, "Faces: ");
          ImGui::SameLine();
          ImGui::Text("%d", face_count);
        }

        // Format and display last modified time
        auto time_t = std::chrono::system_clock::to_time_t(selected_asset.last_modified);
        std::tm tm_buf;
        localtime_s(&tm_buf, &time_t);
        std::stringstream ss;
        ss << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S");
        ImGui::TextColored(COLOR_LABEL_TEXT, "Modified: ");
        ImGui::SameLine();
        ImGui::Text("%s", ss.str().c_str());
      }
      else {
        // 2D Preview for non-model assets
        unsigned int preview_texture = get_asset_texture(selected_asset);
        if (preview_texture != 0) {
          ImVec2 preview_size(avail_width, avail_height);

          if (selected_asset.type == AssetType::Texture) {
            int width, height;
            if (get_texture_dimensions(selected_asset.full_path, width, height)) {
              preview_size = calculate_thumbnail_size(width, height, avail_width, avail_height, 100.0);
            }
          }
          else {
            // For type icons, use ICON_SCALE * min(available_width, available_height)
            float icon_dim = ICON_SCALE * std::min(avail_width, avail_height);
            preview_size = ImVec2(icon_dim, icon_dim);
          }

          // Center the preview image in the panel (same logic as grid)
          ImVec2 container_pos = ImGui::GetCursorScreenPos();
          float image_x_offset = (avail_width - preview_size.x) * 0.5f;
          float image_y_offset = (avail_height - preview_size.y) * 0.5f;
          ImVec2 image_pos(container_pos.x + image_x_offset, container_pos.y + image_y_offset);
          ImGui::SetCursorScreenPos(image_pos);

          // Draw border around the image
          ImVec2 border_min = image_pos;
          ImVec2 border_max(border_min.x + preview_size.x, border_min.y + preview_size.y);
          ImGui::GetWindowDrawList()->AddRect(border_min, border_max, COLOR_BORDER_GRAY, 8.0f, 0, 1.0f);

          ImGui::Image((ImTextureID) (intptr_t) preview_texture, preview_size);

          // Restore cursor for info below
          ImGui::SetCursorScreenPos(container_pos);
          ImGui::Dummy(ImVec2(0, avail_height + 10));
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // Asset information
        ImGui::TextColored(COLOR_LABEL_TEXT, "Name: ");
        ImGui::SameLine();
        ImGui::Text("%s", selected_asset.name.c_str());

        ImGui::TextColored(COLOR_LABEL_TEXT, "Type: ");
        ImGui::SameLine();
        ImGui::Text("%s", get_asset_type_string(selected_asset.type).c_str());

        ImGui::TextColored(COLOR_LABEL_TEXT, "Size: ");
        ImGui::SameLine();
        ImGui::Text("%s", format_file_size(selected_asset.size).c_str());

        ImGui::TextColored(COLOR_LABEL_TEXT, "Extension: ");
        ImGui::SameLine();
        ImGui::Text("%s", selected_asset.extension.c_str());

        // Display dimensions for texture assets
        if (selected_asset.type == AssetType::Texture) {
          int width, height;
          if (get_texture_dimensions(selected_asset.full_path, width, height)) {
            ImGui::TextColored(COLOR_LABEL_TEXT, "Dimensions: ");
            ImGui::SameLine();
            ImGui::Text("%dx%d", width, height);
          }
        }

        ImGui::TextColored(COLOR_LABEL_TEXT, "Path: ");
        ImGui::SameLine();
        ImGui::TextWrapped("%s", format_display_path(selected_asset.full_path).c_str());

        // Format and display last modified time
        auto time_t = std::chrono::system_clock::to_time_t(selected_asset.last_modified);
        std::tm tm_buf;
        localtime_s(&tm_buf, &time_t);
        std::stringstream ss;
        ss << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S");
        ImGui::TextColored(COLOR_LABEL_TEXT, "Modified: ");
        ImGui::SameLine();
        ImGui::Text("%s", ss.str().c_str());
      }
    }
    else {
      ImGui::TextColored(COLOR_DISABLED_TEXT, "No asset selected");
      ImGui::TextColored(COLOR_DISABLED_TEXT, "Click on an asset to preview");
    }

    ImGui::EndChild();

    ImGui::End();

    // Rendering
    ImGui::Render();
    int display_w, display_h;
    glfwGetFramebufferSize(window, &display_w, &display_h);
    glViewport(0, 0, display_w, display_h);
    glClearColor(
      Theme::BACKGROUND_LIGHT_BLUE_1.x, Theme::BACKGROUND_LIGHT_BLUE_1.y, Theme::BACKGROUND_LIGHT_BLUE_1.z,
      Theme::BACKGROUND_LIGHT_BLUE_1.w);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    glfwSwapBuffers(window);
  }

  // Cleanup textures
  for (auto& entry : g_texture_cache) {
    if (entry.second.texture_id != 0) {
      glDeleteTextures(1, &entry.second.texture_id);
    }
  }
  g_texture_cache.clear();

  if (g_default_texture != 0) {
    glDeleteTextures(1, &g_default_texture);
  }

  // Cleanup type-specific textures
  for (auto& [type, texture_id] : g_texture_icons) {
    if (texture_id != 0) {
      glDeleteTextures(1, &texture_id);
    }
  }

  // Cleanup 3D preview resources
  cleanup_model(search_state.current_model);
  cleanup_3d_preview();

  // Cleanup
  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();

  // Stop file watcher and close database
  g_file_watcher.stop_watching();
  g_database.close();

  glfwDestroyWindow(window);
  glfwTerminate();

  return 0;
}
