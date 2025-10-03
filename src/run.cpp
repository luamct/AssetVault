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
#include "ui.h"
#include "services.h"

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

  Theme::load_roboto_font(io);
  Theme::setup_light_fun_theme();

  ImGui_ImplGlfw_InitForOpenGL(window, true);
  ImGui_ImplOpenGL3_Init("#version 330");

  return &io;
}

// Perform initial scan and generate events for EventProcessor
void scan_for_changes(const std::string& root_path, const std::vector<Asset>& db_assets, SafeAssets& safe_assets) {
  if (root_path.empty()) {
    LOG_WARN("scan_for_changes called with empty root path; skipping");
    return;
  }
  auto scan_start = std::chrono::high_resolution_clock::now();

  LOG_INFO("Starting scan for changes...");

  LOG_INFO("Database contains {} assets", db_assets.size());
  std::unordered_map<std::string, Asset> db_map;
  for (const auto& asset : db_assets) {
    db_map[asset.path] = asset;
  }

  // Phase 1: Get filesystem paths (fast scan)
  std::unordered_set<std::string> current_files;
  try {
    fs::path root(root_path);
    if (!fs::exists(root) || !fs::is_directory(root)) {
      LOG_ERROR("Path does not exist or is not a directory: {}", root_path);
      return;
    }

    LOG_INFO("Scanning directory: {}", root_path);

    // Single pass: Get all file paths (with early filtering for ignored asset types)
    for (const auto& entry : fs::recursive_directory_iterator(root)) {
      try {
        // Early filtering: skip directories and ignored asset types to reduce processing
        if (entry.is_directory() || should_skip_asset(entry.path().extension().string())) {
          continue;
        }
        current_files.insert(entry.path().generic_u8string());
      }
      catch (const fs::filesystem_error& e) {
        LOG_WARN("Could not access {}: {}", entry.path().u8string(), e.what());
        continue;
      }
    }

    LOG_INFO("Found {} files and directories", current_files.size());
  }
  catch (const fs::filesystem_error& e) {
    LOG_ERROR("Error scanning directory: {}", e.what());
    return;
  }

  // Track what events need to be generated
  std::vector<FileEvent> events_to_queue;

  // Get current timestamp for all events
  auto current_time = std::chrono::system_clock::now();

  // Compare filesystem state with database state - only check for new files
  for (const auto& path : current_files) {
    auto db_it = db_map.find(path);
    if (db_it == db_map.end()) {
      // File not in database - create a Created event
      FileEvent event(FileEventType::Created, path);
      event.timestamp = current_time;
      events_to_queue.push_back(event);
    }
  }

  LOG_INFO("Now looking for deleted files");

  // Find files in database that no longer exist on filesystem
  for (const auto& db_asset : db_assets) {
    if (current_files.find(db_asset.path) == current_files.end()) {
      // File no longer exists - create a Deleted event
      FileEvent event(FileEventType::Deleted, db_asset.path);
      event.timestamp = current_time;
      events_to_queue.push_back(event);
    }
  }

  auto scan_end = std::chrono::high_resolution_clock::now();
  auto scan_duration = std::chrono::duration_cast<std::chrono::milliseconds>(scan_end - scan_start);

  LOG_INFO("Filesystem scan completed in {}ms", scan_duration.count());
  LOG_INFO("Found {} changes to process", events_to_queue.size());

  // Load existing assets from database
  {
    auto [lock, assets] = safe_assets.write();
    // Load existing database assets into assets map
    for (const auto& asset : db_assets) {
      assets[asset.path] = asset;
    }
    LOG_INFO("Loaded {} existing assets from database", assets.size());
  }

  // Then queue any detected changes to update from that baseline
  if (!events_to_queue.empty()) {
    auto queue_start = std::chrono::high_resolution_clock::now();
    Services::event_processor().queue_events(events_to_queue);
    auto queue_end = std::chrono::high_resolution_clock::now();
    auto queue_duration = std::chrono::duration_cast<std::chrono::milliseconds>(queue_end - queue_start);

    LOG_INFO("Published {} events to EventProcessor in {}ms", events_to_queue.size(), queue_duration.count());
  }
  else {
    LOG_INFO("No changes detected");
  }
}

int run(std::atomic<bool>* shutdown_requested) {
  // Check if running in headless mode (via TESTING env var)
  bool headless_mode = std::getenv("TESTING") != nullptr;

  Logger::initialize(LogLevel::Debug);
  LOG_INFO("AssetInventory application starting {}", headless_mode ? " (headless mode)" : "...");

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
  EventProcessor* event_processor = nullptr;  // Initialized after OpenGL context creation

  // Initialize database
  std::string db_path = Config::get_database_path().string();
  LOG_INFO("Using database path: {}", db_path);
  if (!database.initialize(db_path)) {
    LOG_ERROR("Failed to initialize database");
    return -1;
  }

  if (database.try_get_config_value(Config::CONFIG_KEY_ASSETS_DIRECTORY, ui_state.assets_directory)) {
    LOG_INFO("Loaded assets directory from config: {}", ui_state.assets_directory);
  }

  // Debug: Force clear database if flag is set
  if (Config::DEBUG_FORCE_DB_CLEAR) {
    LOG_WARN("Forcing database clear for testing...");
    database.clear_all_assets();
  }

  // Debug: Force clear thumbnails if flag is set
  if (Config::DEBUG_FORCE_THUMBNAIL_CLEAR) {
    clear_all_thumbnails();
  }

  // Get all assets from database for both search index and initial scan
  auto db_assets = database.get_all_assets();
  LOG_INFO("Loaded {} assets from database", db_assets.size());

  // Initialize search index from assets
  if (!search_index.build_from_assets(db_assets)) {
    LOG_ERROR("Failed to initialize search index");
    return -1;
  }

  // Initialize GLFW
  LOG_INFO("Initializing GLFW{}...", headless_mode ? " (headless)" : "");
  if (!glfwInit()) {
    LOG_ERROR("Failed to initialize GLFW");
    return -1;
  }

  // Set OpenGL context hints for cross-platform compatibility
#ifdef __APPLE__
  // macOS requires OpenGL 4.1 core profile
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
  glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#else
  // Windows/Linux can use OpenGL 3.3
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#endif

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

  // Initialize Dear ImGui (skip in headless mode)
  ImGuiIO* io_ptr = headless_mode ? nullptr : initialize_imgui(window);

  // Initialize texture manager
  if (!texture_manager.initialize()) {
    LOG_ERROR("Failed to initialize texture manager");
    return -1;
  }

  // Initialize EventProcessor
  event_processor = new EventProcessor(safe_assets, ui_state.update_needed, ui_state.assets_directory, thumbnail_context);
  if (!event_processor->start()) {
    LOG_ERROR("Failed to start EventProcessor");
    return -1;
  }

  // Register core services for global access
  Services::provide(&database, &search_index, event_processor, &file_watcher, &texture_manager);
  LOG_INFO("Core services registered");

  // Initialize 3D preview system
  if (!texture_manager.initialize_preview_system()) {
    LOG_WARN("Failed to initialize 3D preview system");
    return -1;
  }

  // Initialize audio manager
  if (!audio_manager.initialize()) {
    LOG_WARN("Failed to initialize audio system");
    // Not critical - continue without audio support
  }

  if (!ui_state.assets_directory.empty()) {
    scan_for_changes(ui_state.assets_directory, db_assets, safe_assets);

    if (!file_watcher.start_watching(ui_state.assets_directory, on_file_event, &safe_assets)) {
      LOG_ERROR("Failed to start file watcher");
      return -1;
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
    // UI mode: full rendering loop
    double last_time = glfwGetTime();
    LOG_INFO("Entering main rendering loop");

    // Main loop - check both window close and shutdown request
    while (!glfwWindowShouldClose(window) && (!shutdown_requested || !shutdown_requested->load())) {
      double current_time = glfwGetTime();
      io_ptr->DeltaTime = (float) (current_time - last_time);
      last_time = current_time;

      glfwPollEvents();

    if (ui_state.assets_directory_changed) {
      ui_state.assets_directory_changed = false;
      const std::string new_path = ui_state.assets_path_selected;
      ui_state.assets_directory = new_path;
      
      // Stop file watcher, event processor and clear pending events
      file_watcher.stop_watching();
      Services::event_processor().stop();
      Services::event_processor().clear_queue();

      // Clear assets from memory and database
      {
        auto [lock, assets] = safe_assets.write();
        assets.clear();
      }

      if (!database.clear_all_assets()) {
        LOG_WARN("Failed to clear assets table before reinitializing assets directory");
      }

      search_index.clear();
      clear_ui_state(ui_state);

      if (!database.upsert_config_value(Config::CONFIG_KEY_ASSETS_DIRECTORY, new_path)) {
        LOG_WARN("Failed to persist assets directory configuration: {}", new_path);
      }

      // Update event processor directory and restart
      Services::event_processor().set_assets_directory(ui_state.assets_directory);
      if (!Services::event_processor().start()) {
        LOG_ERROR("Failed to restart event processor after assets directory change");
      }

      scan_for_changes(ui_state.assets_directory, std::vector<Asset>(), safe_assets);

      if (!file_watcher.start_watching(ui_state.assets_directory, on_file_event, &safe_assets)) {
        LOG_ERROR("Failed to start file watcher for path: {}", ui_state.assets_directory);
      }
    }

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
        if (sel.type == AssetType::Audio && audio_manager.has_audio_loaded()) {
          if (audio_manager.is_playing()) {
            audio_manager.pause();
          } else {
            audio_manager.play();
          }
        }
      }
    }

    // P key to print texture cache
    if (ImGui::IsKeyPressed(ImGuiKey_P) && !input_io.WantTextInput) {
      texture_manager.print_texture_cache(ui_state.assets_directory);
    }

    // Create main window that fits perfectly to viewport
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(viewport->Size);
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
    float left_width = content_width * 0.75f - WINDOW_MARGIN;
    float right_width = content_width * 0.25f - WINDOW_MARGIN;
    float top_height = content_height * 0.20f - WINDOW_MARGIN;
    float bottom_height = content_height * 0.80f - WINDOW_MARGIN;

    // ============ TOP LEFT: Search Box ============
    render_search_panel(ui_state, safe_assets, left_width, top_height);

    // ============ TOP RIGHT: Progress and Messages ============
    ImGui::SameLine();
    render_progress_panel(ui_state, right_width, top_height);

    // ============ BOTTOM LEFT: Search Results ============
    render_asset_grid(ui_state, texture_manager, safe_assets, left_width, bottom_height);

    // ============ BOTTOM RIGHT: Preview Panel ============
    ImGui::SameLine();
    render_preview_panel(ui_state, texture_manager, audio_manager, current_model, camera, right_width, bottom_height);

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

  // Cleanup audio manager
  audio_manager.cleanup();

  // Cleanup texture manager
  texture_manager.cleanup();

  // Cleanup 3D preview resources
  cleanup_model(current_model);

  // Cleanup ImGui (only if initialized)
  if (!headless_mode) {
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
  }

  // Stop file watcher and close database
  file_watcher.stop_watching();

  // Cleanup event processor
  if (event_processor) {
    event_processor->stop();
    delete event_processor;
    event_processor = nullptr;
  }

  database.close();

  // Destroy shared thumbnail context
  glfwDestroyWindow(thumbnail_context);
  glfwDestroyWindow(window);
  glfwTerminate();

  return 0;
}