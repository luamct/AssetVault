#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include "theme.h"
#include "event_processor.h"
#include "database.h"
#include "file_watcher.h"
#include "asset.h"
#include "texture_manager.h"
#include "utils.h"
#include "3d.h"
#include "config.h"
#include "audio_manager.h"
#include "search.h"
#include "logger.h"
#include "ui/ui.h"
#include "services.h"
#include "drag_drop.h"

namespace fs = std::filesystem;

// File event callback function (runs on background thread)
// Queues events for unified processing
void on_file_event(const FileEvent& event) {
  LOG_TRACE("[NEW_EVENT] type = {}, asset = {}", FileWatcher::file_event_type_to_string(event.type), event.path);
  Services::event_processor().queue_event(event);
}

// Initialize ImGui UI system
static ImGuiIO* initialize_imgui(GLFWwindow* window) {
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
  io.IniFilename = nullptr;  // Disable imgui.ini file

  Theme::load_fonts(io);
  Theme::setup_light_fun_theme();

  ImGui_ImplGlfw_InitForOpenGL(window, true);
  ImGui_ImplOpenGL3_Init("#version 330");

  return &io;
}

int run(std::atomic<bool>* shutdown_requested) {
  // Check if running in headless mode (via TESTING env var)
  bool headless_mode = std::getenv("TESTING") != nullptr;

  Logger::initialize(LogLevel::Info);
  LOG_INFO("AssetInventory application starting {}", headless_mode ? " (headless mode)" : "...");

  ensure_executable_working_directory();

  // Initialize application directories (create cache, thumbnail, and data directories)
  Config::initialize_directories();

  // Local variables
  SafeAssets safe_assets;
  AssetDatabase database;
  FileWatcher file_watcher;
  TextureManager texture_manager;
  AudioManager audio_manager;
  UIState ui_state;
  Model current_model;  // 3D model preview state
  Camera3D camera;      // 3D camera state for preview controls
  SearchIndex search_index;  // Search index for fast lookups

  // Initialize GLFW
  LOG_INFO("Initializing GLFW{}...", headless_mode ? " (headless)" : "");
  if (!glfwInit()) {
    LOG_ERROR("Failed to initialize GLFW");
    return -1;
  }

  // Set OpenGL 4.1 Core Profile (works on all modern platforms)
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
  glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);

  // Window configuration based on mode
  glfwWindowHint(GLFW_VISIBLE, headless_mode ? GLFW_FALSE : GLFW_TRUE);
  int window_width = headless_mode ? 1 : Config::WINDOW_WIDTH;
  int window_height = headless_mode ? 1 : Config::WINDOW_HEIGHT;

  GLFWwindow* window = glfwCreateWindow(window_width, window_height, "Asset Inventory", nullptr, nullptr);
  if (!window) {
    LOG_ERROR("Failed to create GLFW window");
    glfwTerminate();
    return -1;
  }

  glfwMakeContextCurrent(window);
  if (!headless_mode) {
    glfwSwapInterval(1); // Enable vsync only for visible windows
  }

  // Initialize GLAD
  if (!gladLoadGLLoader((GLADloadproc) glfwGetProcAddress)) {
    LOG_ERROR("Failed to initialize GLAD");
    glfwTerminate();
    return -1;
  }

  // Create shared OpenGL context for background thumbnail generation
  glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);  // Hidden window
  GLFWwindow* thumbnail_context = glfwCreateWindow(1, 1, "", nullptr, window);
  if (!thumbnail_context) {
    LOG_ERROR("Failed to create thumbnail generation context");
    glfwDestroyWindow(window);
    glfwTerminate();
    return -1;
  }
  LOG_INFO("Created shared OpenGL context for background thumbnail generation");

  // Switch back to main context
  glfwMakeContextCurrent(window);

  // Initialize EventProcessor (needs thumbnail_context created above)
  EventProcessor event_processor(safe_assets, ui_state.update_needed, ui_state.assets_directory, thumbnail_context);

  // Initialize drag-and-drop manager (platform-specific)
  DragDropManager* drag_drop_manager = create_drag_drop_manager();
  if (!drag_drop_manager || !drag_drop_manager->initialize(window)) {
    LOG_ERROR("Failed to initialize drag-and-drop manager");
    return -1;
  }

  // Register core services for global access
  Services::provide(&database, &search_index, &event_processor, &file_watcher, &texture_manager, &audio_manager, drag_drop_manager);
  LOG_INFO("Core services registered");

  // Start all services (includes database init, search index build, scanning, and file watcher)
  if (!Services::start(on_file_event, &safe_assets)) {
    LOG_ERROR("Failed to start services");
    return -1;
  }

  // Load assets directory from config
  database.try_get_config_value(Config::CONFIG_KEY_ASSETS_DIRECTORY, ui_state.assets_directory);

  // If the saved directory no longer exists, reset it (behave as if never set)
  if (!ui_state.assets_directory.empty()) {
    std::error_code ec;
    if (!std::filesystem::exists(ui_state.assets_directory, ec) ||
        !std::filesystem::is_directory(ui_state.assets_directory, ec)) {
      LOG_WARN("Saved assets directory no longer exists: {}. Resetting to unset state.", ui_state.assets_directory);
      ui_state.assets_directory.clear();
      // Clear from database as well
      database.upsert_config_value(Config::CONFIG_KEY_ASSETS_DIRECTORY, "");
    }
  }

  // Main loop
  if (headless_mode) {
    // Headless mode: just wait for shutdown signal
    // Background systems (EventProcessor, FileWatcher, Database) continue running
    LOG_INFO("Entering headless mode - background systems active, waiting for shutdown signal");
    while (!shutdown_requested || !shutdown_requested->load()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    LOG_INFO("Headless mode: shutdown signal received");
  }
  else {
    // Initialize Dear ImGui (skip in headless mode)
    ImGuiIO* io_ptr = initialize_imgui(window);

    // UI mode: full rendering loop
    double last_time = glfwGetTime();
    LOG_INFO("Entering main rendering loop");

    // Main loop - check both window close and shutdown request
    while (!glfwWindowShouldClose(window) && (!shutdown_requested || !shutdown_requested->load())) {
      double current_time = glfwGetTime();
      io_ptr->DeltaTime = (float) (current_time - last_time);
      last_time = current_time;

      glfwPollEvents();

    // Development shortcut: ESC to close app
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
      glfwSetWindowShouldClose(window, GLFW_TRUE);
    }

    // Check if search needs to be updated due to asset changes
    if (ui_state.update_needed.exchange(false)) {
      // Re-apply current search filter to include updated assets
      filter_assets(ui_state, safe_assets);

      // Removes texture cache entries and thumbnails for deleted assets
      texture_manager.process_cleanup_queue(ui_state.assets_directory);
    }

    // Process pending debounced search
    if (ui_state.pending_search) {
      auto now = std::chrono::steady_clock::now();
      auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - ui_state.last_keypress_time).count();

      if (elapsed >= Config::SEARCH_DEBOUNCE_MS) {
        // Execute the search
        filter_assets(ui_state, safe_assets);
        ui_state.last_buffer = ui_state.buffer;
        ui_state.pending_search = false;
      }
    }

    // Start the Dear ImGui frame
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    // Handle keyboard input
    ImGuiIO& input_io = ImGui::GetIO();

    // Spacebar pause/unpause for audio assets
    if (ImGui::IsKeyPressed(ImGuiKey_Space) && !input_io.WantTextInput) {
      if (ui_state.selected_asset.has_value()) {
        const Asset& sel = *ui_state.selected_asset;
        if (sel.type == AssetType::Audio && Services::audio_manager().has_audio_loaded()) {
          if (Services::audio_manager().is_playing()) {
            Services::audio_manager().pause();
          } else {
            Services::audio_manager().play();
          }
        }
      }
    }

    // P key to print texture cache
    if (ImGui::IsKeyPressed(ImGuiKey_P) && !input_io.WantTextInput) {
      texture_manager.print_texture_cache(ui_state.assets_directory);
      search_index.debug_print_tokens();
    }

    // Create main window that fits perfectly to viewport
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    const float WINDOW_FRAME_MARGIN = 20.0f;
    ImVec2 window_pos = ImVec2(
      viewport->Pos.x + WINDOW_FRAME_MARGIN,
      viewport->Pos.y + WINDOW_FRAME_MARGIN);
    ImVec2 window_size = ImVec2(
      std::max(0.0f, viewport->Size.x - WINDOW_FRAME_MARGIN * 2.0f),
      std::max(0.0f, viewport->Size.y - WINDOW_FRAME_MARGIN * 2.0f));
    ImGui::SetNextWindowPos(window_pos);
    ImGui::SetNextWindowSize(window_size);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::Begin(
      "Asset Inventory", nullptr,
      ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus |
      ImGuiWindowFlags_NoNavFocus);
    ImGui::PopStyleVar(3);

    // Calculate panel sizes using actual window content area
    float content_width = ImGui::GetContentRegionAvail().x;
    float content_height = ImGui::GetContentRegionAvail().y;
    float WINDOW_MARGIN = 6.0f;
    float spacing_y = ImGui::GetStyle().ItemSpacing.y;
    float left_width = content_width * 0.75f - WINDOW_MARGIN;
    float right_width = content_width * 0.25f - WINDOW_MARGIN;
    float top_height = Config::SEARCH_PANEL_HEIGHT;
    float bottom_height = content_height - top_height - spacing_y;
    if (bottom_height < 0.0f) {
      bottom_height = 0.0f;
    }

    float progress_height = 35.0f;
    float preview_folder_available = std::max(0.0f,
      content_height - progress_height - spacing_y * 2.0f);
    float preview_height = preview_folder_available * 0.7f;
    float folder_tree_height = preview_folder_available - preview_height;

    // Left column (search + grid)
    ImGui::BeginGroup();
    render_search_panel(ui_state, safe_assets, left_width, top_height);
    render_asset_grid(ui_state, texture_manager, safe_assets, left_width, bottom_height);
    ImGui::EndGroup();

    ImGui::SameLine();

    // Right column (preview + progress)
    ImGui::BeginGroup();
    render_preview_panel(ui_state, texture_manager, current_model, camera, right_width, preview_height);
    render_folder_tree_panel(ui_state, right_width, folder_tree_height);
    render_progress_panel(ui_state, safe_assets, right_width, progress_height);
    ImGui::EndGroup();

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
    }  // End of UI mode main loop
  }  // End of headless/UI conditional

  // Cleanup services
  Services::audio_manager().cleanup();
  texture_manager.cleanup();

  // Cleanup 3D preview resources
  cleanup_model(current_model);

  // Cleanup ImGui (only if initialized)
  if (!headless_mode) {
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
  }

  // Stop all services
  Services::stop();

  // Cleanup drag-and-drop manager
  if (drag_drop_manager) {
    delete drag_drop_manager;
    drag_drop_manager = nullptr;
  }

  // Destroy windows
  if (thumbnail_context) {
    glfwDestroyWindow(thumbnail_context);
    thumbnail_context = nullptr;
  }
  if (window) {
    glfwDestroyWindow(window);
    window = nullptr;
  }

  glfwTerminate();

  return 0;
}
