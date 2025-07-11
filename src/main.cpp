#ifdef _WIN32
#include <windows.h>
#endif
#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <unordered_map>

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "theme.h"

#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif

#include "database.h"
#include "file_watcher.h"
#include "index.h"
#include "utils.h"

// Include stb_image for PNG loading
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

// Constants
constexpr int WINDOW_WIDTH = 1920;
constexpr int WINDOW_HEIGHT = 1080;
constexpr float SEARCH_BOX_WIDTH = 375.0f;
constexpr float SEARCH_BOX_HEIGHT = 60.0f;
constexpr float THUMBNAIL_SIZE = 180.0f;
constexpr float GRID_SPACING = 30.0f;
constexpr float TEXT_MARGIN = 20.0f; // Space below thumbnail for text positioning
constexpr float TEXT_HEIGHT = 20.0f; // Height reserved for text
constexpr float ICON_SCALE = 0.5f;   // Icon occupies 50% of the thumbnail area

// Preview panel layout constants
constexpr float PREVIEW_RIGHT_MARGIN = 40.0f;     // Margin from window right edge
constexpr float PREVIEW_INTERNAL_PADDING = 30.0f; // Internal padding within preview panel

// Color constants
constexpr ImU32 BACKGROUND_COLOR = IM_COL32(242, 247, 255, 255);         // Light blue-gray background
constexpr ImU32 FALLBACK_THUMBNAIL_COLOR = IM_COL32(242, 247, 255, 255); // Same as background

// Global variables for search and UI state
static char search_buffer[256] = "";
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
std::vector<FileInfo> g_filtered_assets;
std::atomic<bool> g_assets_updated(false);
AssetDatabase g_database;
FileWatcher g_file_watcher;
unsigned int g_default_texture = 0;

// Selection state
int g_selected_asset_index = -1; // -1 means no selection

// Type-specific textures
std::unordered_map<AssetType, unsigned int> g_texture_icons;

// Texture cache
std::unordered_map<std::string, TextureCacheEntry> g_texture_cache;

// 3D preview viewport variables
unsigned int g_preview_shader = 0;
unsigned int g_preview_vao = 0;
unsigned int g_preview_vbo = 0;
unsigned int g_preview_framebuffer = 0;
unsigned int g_preview_texture = 0;
unsigned int g_preview_depth_texture = 0;
bool g_preview_initialized = false;

// Shader sources for 3D preview
const char* preview_vertex_shader_source = R"(
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aColor;
out vec3 ourColor;
void main()
{
    gl_Position = vec4(aPos, 1.0);
    ourColor = aColor;
}
)";

const char* preview_fragment_shader_source = R"(
#version 330 core
out vec4 FragColor;
in vec3 ourColor;
void main()
{
    FragColor = vec4(ourColor, 1.0);
}
)";

// Initialize 3D preview viewport
bool initialize_3d_preview() {
  if (g_preview_initialized)
    return true;

  // Build and compile shader program
  unsigned int vertexShader = glCreateShader(GL_VERTEX_SHADER);
  glShaderSource(vertexShader, 1, &preview_vertex_shader_source, NULL);
  glCompileShader(vertexShader);

  // Check for shader compile errors
  int success;
  char infoLog[512];
  glGetShaderiv(vertexShader, GL_COMPILE_STATUS, &success);
  if (!success) {
    glGetShaderInfoLog(vertexShader, 512, NULL, infoLog);
    std::cout << "ERROR::SHADER::VERTEX::COMPILATION_FAILED\n" << infoLog << std::endl;
    return false;
  }

  unsigned int fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
  glShaderSource(fragmentShader, 1, &preview_fragment_shader_source, NULL);
  glCompileShader(fragmentShader);

  glGetShaderiv(fragmentShader, GL_COMPILE_STATUS, &success);
  if (!success) {
    glGetShaderInfoLog(fragmentShader, 512, NULL, infoLog);
    std::cout << "ERROR::SHADER::FRAGMENT::COMPILATION_FAILED\n" << infoLog << std::endl;
    return false;
  }

  // Link shaders
  g_preview_shader = glCreateProgram();
  glAttachShader(g_preview_shader, vertexShader);
  glAttachShader(g_preview_shader, fragmentShader);
  glLinkProgram(g_preview_shader);

  glGetProgramiv(g_preview_shader, GL_LINK_STATUS, &success);
  if (!success) {
    glGetProgramInfoLog(g_preview_shader, 512, NULL, infoLog);
    std::cout << "ERROR::SHADER::PROGRAM::LINKING_FAILED\n" << infoLog << std::endl;
    return false;
  }

  glDeleteShader(vertexShader);
  glDeleteShader(fragmentShader);

  // Set up vertex data and configure vertex attributes
  float vertices[] = {
      // positions        // colors
      -0.5f, -0.5f, 0.0f, 1.0f, 0.0f, 0.0f, // Red
      0.5f,  -0.5f, 0.0f, 0.0f, 1.0f, 0.0f, // Green
      0.0f,  0.5f,  0.0f, 0.0f, 0.0f, 1.0f  // Blue
  };

  glGenVertexArrays(1, &g_preview_vao);
  glGenBuffers(1, &g_preview_vbo);

  glBindVertexArray(g_preview_vao);
  glBindBuffer(GL_ARRAY_BUFFER, g_preview_vbo);
  glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

  // Position attribute
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
  glEnableVertexAttribArray(0);
  // Color attribute
  glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
  glEnableVertexAttribArray(1);

  // Create framebuffer
  glGenFramebuffers(1, &g_preview_framebuffer);
  glGenTextures(1, &g_preview_texture);
  glGenTextures(1, &g_preview_depth_texture);

  // Set up framebuffer
  glBindFramebuffer(GL_FRAMEBUFFER, g_preview_framebuffer);

  // Color texture
  glBindTexture(GL_TEXTURE_2D, g_preview_texture);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 800, 800, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, g_preview_texture, 0);

  // Depth texture
  glBindTexture(GL_TEXTURE_2D, g_preview_depth_texture);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH24_STENCIL8, 800, 800, 0, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, nullptr);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, g_preview_depth_texture, 0);

  if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
    std::cout << "Preview framebuffer is not complete!" << std::endl;
    return false;
  }

  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  g_preview_initialized = true;
  return true;
}

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

// Function to load type-specific textures
void load_type_textures() {
  const std::unordered_map<AssetType, const char*> texture_paths = {
      {AssetType::Texture, "images/texture.png"},  {AssetType::Model, "images/model.png"},
      {AssetType::Sound, "images/sound.png"},      {AssetType::Font, "images/font.png"},
      {AssetType::Shader, "images/document.png"},  {AssetType::Document, "images/document.png"},
      {AssetType::Archive, "images/document.png"}, {AssetType::Directory, "images/folder.png"},
      {AssetType::Unknown, "images/document.png"}};

  for (const auto& [type, path] : texture_paths) {
    unsigned int texture_id = load_texture(path);
    g_texture_icons[type] = texture_id;
    if (texture_id == 0) {
      std::cerr << "Failed to load type texture: " << path << '\n';
    }
  }
}

// Function to calculate aspect-ratio-preserving dimensions with upscaling limit
ImVec2 calculate_thumbnail_size(int original_width, int original_height, float max_width, float max_height,
                                float max_upscale_factor = 3.0f) {
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

  // Load the texture using the shared function
  unsigned int texture_id = load_texture(asset.full_path.c_str());
  if (texture_id == 0) {
    std::cerr << "Failed to load texture: " << asset.full_path << '\n';
    return 0;
  }

  // Get texture dimensions
  int width, height, channels;
  unsigned char* data = stbi_load(asset.full_path.c_str(), &width, &height, &channels, 4);
  if (data) {
    stbi_image_free(data);
  } else {
    width = 0;
    height = 0;
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

// Function to filter assets based on search query
void filter_assets(const std::string& search_query) {
  g_filtered_assets.clear();

  for (const auto& asset : g_assets) {
    if (asset_matches_search(asset, search_query)) {
      g_filtered_assets.push_back(asset);
    }
  }
}

// File event callback function
void on_file_event(const FileEvent& event) {
  switch (event.type) {
  case FileEventType::Created:
    // Fall through to Modified case
  case FileEventType::Modified: {
    // Check if it's a file (not directory)
    if (std::filesystem::is_regular_file(event.path)) {
      // Clear texture cache entry for this file so it can be reloaded
      auto cache_it = g_texture_cache.find(event.path);
      if (cache_it != g_texture_cache.end()) {
        // If there's an existing texture, delete it from OpenGL
        if (cache_it->second.texture_id != 0) {
          glDeleteTextures(1, &cache_it->second.texture_id);
        }
        // Remove from cache
        g_texture_cache.erase(cache_it);
      }

      FileInfo file_info;
      std::filesystem::path path(event.path);

      file_info.name = path.filename().string();
      file_info.extension = path.extension().string();
      file_info.full_path = event.path;
      file_info.relative_path = event.path; // For now, use full path
      file_info.size = std::filesystem::file_size(event.path);
      file_info.last_modified = event.timestamp;
      file_info.is_directory = false;
      file_info.type = get_asset_type(file_info.extension);

      // Insert or update in database
      auto existing_asset = g_database.get_asset_by_path(event.path);
      if (existing_asset.full_path.empty()) {
        g_database.insert_asset(file_info);
      } else {
        g_database.update_asset(file_info);
      }
      g_assets_updated = true;
    }
    break;
  }
  case FileEventType::Deleted: {
    // Clear texture cache entry for deleted file
    auto cache_it = g_texture_cache.find(event.path);
    if (cache_it != g_texture_cache.end()) {
      if (cache_it->second.texture_id != 0) {
        glDeleteTextures(1, &cache_it->second.texture_id);
      }
      g_texture_cache.erase(cache_it);
    }

    g_database.delete_asset(event.path);
    g_assets_updated = true;
    break;
  }
  case FileEventType::Renamed: {
    // Clear texture cache entry for old path
    auto cache_it = g_texture_cache.find(event.old_path);
    if (cache_it != g_texture_cache.end()) {
      if (cache_it->second.texture_id != 0) {
        glDeleteTextures(1, &cache_it->second.texture_id);
      }
      g_texture_cache.erase(cache_it);
    }

    // Delete old entry and create new one
    g_database.delete_asset(event.old_path);

    if (std::filesystem::is_regular_file(event.path)) {
      FileInfo file_info;
      std::filesystem::path path(event.path);

      file_info.name = path.filename().string();
      file_info.extension = path.extension().string();
      file_info.full_path = event.path;
      file_info.relative_path = event.path;
      file_info.size = std::filesystem::file_size(event.path);
      file_info.last_modified = event.timestamp;
      file_info.is_directory = false;
      file_info.type = get_asset_type(file_info.extension);

      g_database.insert_asset(file_info);
      g_assets_updated = true;
    }
    break;
  }
  default:
    break;
  }
}

int main() {
  // Initialize database
  std::cout << "Initializing database...\n";
  if (!g_database.initialize("db/assets.db")) {
    std::cerr << "Failed to initialize database\n";
    return -1;
  }

  // Clean database before starting (as requested)
  std::cout << "Cleaning database...\n";
  g_database.clear_all_assets();

  // Create initial scan of assets directory
  std::cout << "Performing initial asset scan...\n";
  std::vector<FileInfo> initial_assets = scan_directory("assets");
  if (!initial_assets.empty()) {
    g_database.insert_assets_batch(initial_assets);
    g_assets = g_database.get_all_assets();
    g_filtered_assets = g_assets; // Initialize filtered assets
  }

  // Start file watching
  std::cout << "Starting file watcher...\n";
  if (!g_file_watcher.start_watching("assets", on_file_event)) {
    std::cerr << "Failed to start file watcher\n";
    return -1;
  }

  std::cout << "File watcher started successfully\n";

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
  if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
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

  // Main loop
  double last_time = glfwGetTime();
  while (!glfwWindowShouldClose(window)) {
    double current_time = glfwGetTime();
    io.DeltaTime = (float)(current_time - last_time);
    last_time = current_time;

    glfwPollEvents();

    // Render 3D preview to framebuffer BEFORE starting ImGui frame
    if (g_preview_initialized) {
      // Calculate the size for the right panel (same as 2D previews)
      float preview_width = (ImGui::GetIO().DisplaySize.x * 0.25f) - PREVIEW_RIGHT_MARGIN;
      float avail_width = preview_width - PREVIEW_INTERNAL_PADDING;
      float avail_height = avail_width; // Square aspect ratio

      // Update framebuffer size if needed
      static int last_fb_width = 0, last_fb_height = 0;
      int fb_width = static_cast<int>(avail_width);
      int fb_height = static_cast<int>(avail_height);

      if (fb_width != last_fb_width || fb_height != last_fb_height) {
        // Recreate framebuffer with new size
        glBindTexture(GL_TEXTURE_2D, g_preview_texture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, fb_width, fb_height, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);
        glBindTexture(GL_TEXTURE_2D, g_preview_depth_texture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH24_STENCIL8, fb_width, fb_height, 0, GL_DEPTH_STENCIL,
                     GL_UNSIGNED_INT_24_8, nullptr);
        last_fb_width = fb_width;
        last_fb_height = fb_height;
      }

      // Render triangle to framebuffer
      glBindFramebuffer(GL_FRAMEBUFFER, g_preview_framebuffer);
      glViewport(0, 0, fb_width, fb_height);
      glClearColor(0.2f, 0.3f, 0.3f, 1.0f);
      glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

      // Draw the triangle
      glUseProgram(g_preview_shader);
      glBindVertexArray(g_preview_vao);
      glDrawArrays(GL_TRIANGLES, 0, 3);

      // Unbind framebuffer
      glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    // Start the Dear ImGui frame
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    // Check if assets were updated and refresh the list
    if (g_assets_updated.exchange(false)) {
      g_assets = g_database.get_all_assets();
      // Re-apply current search filter to include new assets
      filter_assets(search_buffer);
    }

    // Create main window
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
    ImGui::Begin("Asset Inventory", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);

    // Calculate panel sizes (75% for grid, 25% for preview with margin)
    float window_width = ImGui::GetWindowSize().x;
    float grid_width = window_width * 0.75f;
    float preview_width = window_width * 0.25f - PREVIEW_RIGHT_MARGIN; // Add right margin

    // Header
    ImGui::PushFont(io.Fonts->Fonts[0]);
    ImGui::TextColored(ImVec4(0.20f, 0.70f, 0.90f, 1.0f), "Asset Inventory");
    ImGui::PopFont();
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.60f, 0.60f, 0.60f, 1.0f), "v1.0.0");
    ImGui::Separator();
    ImGui::Spacing();

    // Search box - create capsule background first
    float search_x = (ImGui::GetWindowSize().x - SEARCH_BOX_WIDTH) * 0.5f;
    float search_y = ImGui::GetCursorPosY();

    // Draw capsule background
    ImVec2 capsule_min(search_x, search_y);
    ImVec2 capsule_max(search_x + SEARCH_BOX_WIDTH, search_y + SEARCH_BOX_HEIGHT);
    ImGui::GetWindowDrawList()->AddRectFilled(capsule_min, capsule_max,
                                              IM_COL32(255, 255, 255, 255), // White background
                                              25.0f                         // Rounded corners
    );

    // Position text input inside the capsule
    ImGui::SetCursorPos(ImVec2(search_x + 20,
                               search_y + (SEARCH_BOX_HEIGHT - ImGui::GetFontSize()) * 0.5f - 4)); // Move up 2 pixels
    ImGui::PushItemWidth(SEARCH_BOX_WIDTH - 40); // Leave space for padding

    // Remove borders from text input
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(0, 0, 0, 0)); // Transparent background

    ImGui::InputText("##Search", search_buffer, sizeof(search_buffer), ImGuiInputTextFlags_EnterReturnsTrue);

    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
    ImGui::PopItemWidth();

    // Filter assets in real-time as user types
    filter_assets(search_buffer);

    ImGui::Spacing();
    ImGui::Spacing();

    // Left panel - Asset grid
    ImGui::BeginChild("AssetGrid", ImVec2(grid_width, 0), true);

    // Calculate grid layout upfront since all items have the same size
    float available_width = grid_width - 20.0f;                     // Account for padding
    float item_height = THUMBNAIL_SIZE + TEXT_MARGIN + TEXT_HEIGHT; // Full item height including text
    // Add GRID_SPACING to available width since we don't need spacing after the
    // last item
    int columns = static_cast<int>((available_width + GRID_SPACING) / (THUMBNAIL_SIZE + GRID_SPACING));
    if (columns < 1)
      columns = 1;

    // Display filtered assets in a proper grid
    for (size_t i = 0; i < g_filtered_assets.size(); i++) {
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
      unsigned int asset_texture = get_asset_texture(g_filtered_assets[i]);

      // Calculate display size based on asset type
      ImVec2 display_size(THUMBNAIL_SIZE, THUMBNAIL_SIZE);
      bool is_texture = (g_filtered_assets[i].type == AssetType::Texture && asset_texture != 0);
      if (is_texture) {
        int width, height;
        if (get_texture_dimensions(g_filtered_assets[i].full_path, width, height)) {
          display_size =
              calculate_thumbnail_size(width, height, THUMBNAIL_SIZE, THUMBNAIL_SIZE, 3.0f); // 3x upscaling for grid
        }
      } else {
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
          BACKGROUND_COLOR);

      // Center the image/icon in the thumbnail area
      float image_x_offset = (THUMBNAIL_SIZE - display_size.x) * 0.5f;
      float image_y_offset = (THUMBNAIL_SIZE - display_size.y) * 0.5f;
      ImVec2 image_pos(container_pos.x + image_x_offset, container_pos.y + image_y_offset);

      ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.f, 0.f, 0.f, 0.f));
      ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.f, 0.f, 0.f, 0.f));
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.f, 0.f, 0.f, 0.3f));

      // Display thumbnail image
      if (asset_texture != 0) {
        ImGui::SetCursorScreenPos(image_pos);
        if (ImGui::ImageButton(("##Thumbnail" + std::to_string(i)).c_str(), (ImTextureID)(intptr_t)asset_texture,
                               display_size)) {
          g_selected_asset_index = static_cast<int>(i);
          std::cout << "Selected: " << g_filtered_assets[i].name << '\n';
        }
      } else {
        // Fallback: colored button if texture failed to load
        ImGui::SetCursorScreenPos(image_pos);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8.0f);
        if (ImGui::Button(("##Thumbnail" + std::to_string(i)).c_str(), display_size)) {
          g_selected_asset_index = static_cast<int>(i);
          std::cout << "Selected: " << g_filtered_assets[i].name << '\n';
        }
        ImGui::PopStyleVar();

        // Add a background to simulate thumbnail (same as app background)
        ImGui::GetWindowDrawList()->AddRectFilled(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(),
                                                  FALLBACK_THUMBNAIL_COLOR);
      }

      ImGui::PopStyleColor(3);

      // Position text at the bottom of the container
      ImGui::SetCursorScreenPos(ImVec2(container_pos.x, container_pos.y + THUMBNAIL_SIZE + TEXT_MARGIN));

      // Asset name below thumbnail
      std::string truncated_name = truncate_filename(g_filtered_assets[i].name);
      ImGui::SetCursorPosX(ImGui::GetCursorPosX() +
                           (THUMBNAIL_SIZE - ImGui::CalcTextSize(truncated_name.c_str()).x) * 0.5f);
      ImGui::TextWrapped("%s", truncated_name.c_str());

      ImGui::EndGroup();
    }

    // Show message if no assets found
    if (g_filtered_assets.empty()) {
      if (g_assets.empty()) {
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "No assets found. Add files to the 'assets' directory.");
      } else {
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "No assets match your search.");
      }
    }

    ImGui::EndChild();

    // Right panel - Asset preview
    ImGui::SameLine();
    ImGui::BeginChild("AssetPreview", ImVec2(preview_width, 0), true);

    // Use fixed panel dimensions for stable calculations
    float avail_width = preview_width - PREVIEW_INTERNAL_PADDING; // Account for ImGui padding and margins
    float avail_height = avail_width;                             // Square aspect ratio for preview area

    if (g_selected_asset_index >= 0 && g_selected_asset_index < static_cast<int>(g_filtered_assets.size())) {
      const FileInfo& selected_asset = g_filtered_assets[g_selected_asset_index];

      // Check if selected asset is a model
      if (selected_asset.type == AssetType::Model && g_preview_initialized) {
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
        ImGui::GetWindowDrawList()->AddRect(border_min, border_max, IM_COL32(150, 150, 150, 255), 8.0f, 0, 1.0f);

        // Display the 3D viewport
        ImGui::Image((ImTextureID)(intptr_t)g_preview_texture, viewport_size);

        // Restore cursor for info below
        ImGui::SetCursorScreenPos(container_pos);
        ImGui::Dummy(ImVec2(0, avail_height + 10));

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // 3D Viewport Info
        ImGui::TextColored(ImVec4(0.2f, 0.2f, 0.8f, 1.0f), "3D Model Preview");
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Colored triangle rendered with OpenGL");
      } else {
        // 2D Preview for non-model assets
        unsigned int preview_texture = get_asset_texture(selected_asset);
        if (preview_texture != 0) {
          ImVec2 preview_size(avail_width, avail_height);

          if (selected_asset.type == AssetType::Texture) {
            int width, height;
            if (get_texture_dimensions(selected_asset.full_path, width, height)) {
              preview_size =
                  calculate_thumbnail_size(width, height, avail_width, avail_height,
                                           std::numeric_limits<float>::max()); // No upscaling limit for preview
            }
          } else {
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
          ImGui::GetWindowDrawList()->AddRect(border_min, border_max, IM_COL32(150, 150, 150, 255), 8.0f, 0, 1.0f);

          ImGui::Image((ImTextureID)(intptr_t)preview_texture, preview_size);

          // Restore cursor for info below
          ImGui::SetCursorScreenPos(container_pos);
          ImGui::Dummy(ImVec2(0, avail_height + 10));
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // Asset information
        ImGui::TextColored(ImVec4(0.2f, 0.2f, 0.8f, 1.0f), "Name: ");
        ImGui::SameLine();
        ImGui::Text("%s", selected_asset.name.c_str());

        ImGui::TextColored(ImVec4(0.2f, 0.2f, 0.8f, 1.0f), "Type: ");
        ImGui::SameLine();
        ImGui::Text("%s", get_asset_type_string(selected_asset.type).c_str());

        ImGui::TextColored(ImVec4(0.2f, 0.2f, 0.8f, 1.0f), "Size: ");
        ImGui::SameLine();
        ImGui::Text("%s", format_file_size(selected_asset.size).c_str());

        ImGui::TextColored(ImVec4(0.2f, 0.2f, 0.8f, 1.0f), "Extension: ");
        ImGui::SameLine();
        ImGui::Text("%s", selected_asset.extension.c_str());

        // Display dimensions for texture assets
        if (selected_asset.type == AssetType::Texture) {
          int width, height;
          if (get_texture_dimensions(selected_asset.full_path, width, height)) {
            ImGui::TextColored(ImVec4(0.2f, 0.2f, 0.8f, 1.0f), "Dimensions: ");
            ImGui::SameLine();
            ImGui::Text("%dx%d", width, height);
          }
        }

        ImGui::TextColored(ImVec4(0.2f, 0.2f, 0.8f, 1.0f), "Path: ");
        ImGui::SameLine();
        ImGui::Text("%s", format_display_path(selected_asset.full_path).c_str());

        // Format and display last modified time
        auto time_t = std::chrono::system_clock::to_time_t(selected_asset.last_modified);
        std::tm tm_buf;
        localtime_s(&tm_buf, &time_t);
        std::stringstream ss;
        ss << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S");
        ImGui::TextColored(ImVec4(0.2f, 0.2f, 0.8f, 1.0f), "Modified: ");
        ImGui::SameLine();
        ImGui::Text("%s", ss.str().c_str());
      }
    } else {
      ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "No asset selected");
      ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "Click on an asset to preview");
    }

    ImGui::EndChild();

    ImGui::End();

    // Rendering
    ImGui::Render();
    int display_w, display_h;
    glfwGetFramebufferSize(window, &display_w, &display_h);
    glViewport(0, 0, display_w, display_h);
    glClearColor(0.95f, 0.97f, 1.00f, 1.00f);
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
  if (g_preview_initialized) {
    glDeleteVertexArrays(1, &g_preview_vao);
    glDeleteBuffers(1, &g_preview_vbo);
    glDeleteProgram(g_preview_shader);
    glDeleteTextures(1, &g_preview_texture);
    glDeleteTextures(1, &g_preview_depth_texture);
    glDeleteFramebuffers(1, &g_preview_framebuffer);
  }

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
