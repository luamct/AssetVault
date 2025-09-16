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

// Global callback state (needed for callback functions)
EventProcessor* g_event_processor = nullptr;

// File event callback function (runs on background thread)
// Queues events for unified processing
void on_file_event(const FileEvent& event) {
  LOG_TRACE("[NEW_EVENT] type = {}, asset = {}", FileWatcher::file_event_type_to_string(event.type), event.path);
  g_event_processor->queue_event(event);
}

// Perform initial scan and generate events for EventProcessor
void scan_for_changes(AssetDatabase& database, std::map<std::string, Asset>& assets, std::mutex& assets_mutex, EventProcessor* event_processor) {
  namespace fs = std::filesystem;
  auto scan_start = std::chrono::high_resolution_clock::now();

  LOG_INFO("Starting scan for changes...");

  // Get current database state
  std::vector<Asset> db_assets = database.get_all_assets();
  LOG_INFO("Database contains {} assets", db_assets.size());
  std::unordered_map<std::string, Asset> db_map;
  for (const auto& asset : db_assets) {
    db_map[asset.path] = asset;
  }

  // Phase 1: Get filesystem paths (fast scan)
  std::unordered_set<std::string> current_files;
  try {
    fs::path root(Config::ASSET_ROOT_DIRECTORY);
    if (!fs::exists(root) || !fs::is_directory(root)) {
      LOG_ERROR("Path does not exist or is not a directory: {}", Config::ASSET_ROOT_DIRECTORY);
      return;
    }

    LOG_INFO("Scanning directory: {}", Config::ASSET_ROOT_DIRECTORY);

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
    std::lock_guard<std::mutex> lock(assets_mutex);
    // Load existing database assets into assets map
    for (const auto& asset : db_assets) {
      assets[asset.path] = asset;
    }
    LOG_INFO("Loaded {} existing assets from database", assets.size());
  }


  // Then queue any detected changes to update from that baseline
  if (!events_to_queue.empty()) {
    auto queue_start = std::chrono::high_resolution_clock::now();
    event_processor->queue_events(events_to_queue);
    auto queue_end = std::chrono::high_resolution_clock::now();
    auto queue_duration = std::chrono::duration_cast<std::chrono::milliseconds>(queue_end - queue_start);

    LOG_INFO("Published {} events to EventProcessor in {}ms", events_to_queue.size(), queue_duration.count());
  }
  else {
    LOG_INFO("No changes detected");
  }
}

int main() {
#ifdef _WIN32
  // Set console to UTF-8 mode for proper Unicode display
  SetConsoleOutputCP(CP_UTF8);
  SetConsoleCP(CP_UTF8);
  LOG_INFO("Console set to UTF-8 mode for Unicode support");
#endif

  // Initialize logging system
  Logger::initialize(LogLevel::Debug);
  LOG_INFO("AssetInventory application starting...");

  // Initialize application directories (create cache, thumbnail, and data directories)
  Config::initialize_directories();

  // Local variables
  std::map<std::string, Asset> assets;
  std::mutex assets_mutex;  // Mutex to guard access to the assets map
  AssetDatabase database;
  FileWatcher file_watcher;
  TextureManager texture_manager;
  AudioManager audio_manager;
  SearchState search_state;
  Model current_model;  // 3D model preview state
  Camera3D camera;      // 3D camera state for preview controls
  SearchIndex search_index(&database);  // Search index for fast lookups

  // Initialize database
  if (!database.initialize(Config::DATABASE_PATH)) {
    LOG_ERROR("Failed to initialize database");
    return -1;
  }

  // Debug: Force clear database if flag is set
  if (Config::DEBUG_FORCE_DB_CLEAR) {
    LOG_WARN("Forcing database clear for testing...");
    database.clear_all_assets();
  }

  // Debug: Force clear thumbnails if flag is set
  if (Config::DEBUG_FORCE_THUMBNAIL_CLEAR) {
    LOG_WARN("DEBUG_FORCE_THUMBNAIL_CLEAR is enabled - deleting all thumbnails for debugging...");

    // Use proper cross-platform thumbnail directory
    std::filesystem::path thumbnail_dir = Config::get_thumbnail_directory();
    LOG_INFO("Using thumbnail directory: {}", thumbnail_dir.string());

    try {
      if (std::filesystem::exists(thumbnail_dir)) {
        std::filesystem::remove_all(thumbnail_dir);
        LOG_INFO("All thumbnails deleted successfully from: {}", thumbnail_dir.string());
      }
      else {
        LOG_INFO("Thumbnails directory does not exist yet: {}", thumbnail_dir.string());
      }
    }
    catch (const std::filesystem::filesystem_error& e) {
      LOG_ERROR("Failed to delete thumbnails: {}", e.what());
    }
  }

  // Initialize search index from database
  LOG_INFO("Initializing search index...");
  if (!search_index.load_from_database()) {
    LOG_ERROR("Failed to initialize search index");
    return -1;
  }
  LOG_INFO("Search index initialized with {} tokens", search_index.get_token_count());


  // Initialize GLFW
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

  // Create window
  GLFWwindow* window = glfwCreateWindow(Config::WINDOW_WIDTH, Config::WINDOW_HEIGHT, "Asset Inventory", nullptr, nullptr);
  if (!window) {
    LOG_ERROR("Failed to create GLFW window");
    glfwTerminate();
    return -1;
  }

  glfwMakeContextCurrent(window);
  glfwSwapInterval(1); // Enable vsync

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

  // Initialize Dear ImGui
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;

  // Disable imgui.ini file - we'll handle window positioning in code
  io.IniFilename = nullptr;

  // Load Roboto font
  Theme::load_roboto_font(io);

  // Setup light and fun theme
  Theme::setup_light_fun_theme();

  // Setup Platform/Renderer backends
  ImGui_ImplGlfw_InitForOpenGL(window, true);
  ImGui_ImplOpenGL3_Init("#version 330");

  // Initialize texture manager
  if (!texture_manager.initialize()) {
    LOG_ERROR("Failed to initialize texture manager");
    return -1;
  }

  // Initialize unified event processor for both initial scan and runtime events
  // Pass thumbnail context to EventProcessor constructor for proper OpenGL setup
  g_event_processor = new EventProcessor(database, assets, assets_mutex, search_state.update_needed, texture_manager, search_index, thumbnail_context);
  if (!g_event_processor->start()) {
    LOG_ERROR("Failed to start EventProcessor");
    return -1;
  }

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

  scan_for_changes(database, assets, assets_mutex, g_event_processor);

  // Start file watcher after initial scan
  LOG_INFO("Starting file watcher...");

  if (!file_watcher.start_watching(Config::ASSET_ROOT_DIRECTORY, on_file_event, &assets, &assets_mutex)) {
    LOG_ERROR("Failed to start file watcher");
    return -1;
  }

  // Main loop
  double last_time = glfwGetTime();
  LOG_INFO("Entering main rendering loop");
  while (!glfwWindowShouldClose(window)) {
    double current_time = glfwGetTime();
    io.DeltaTime = (float) (current_time - last_time);
    last_time = current_time;

    glfwPollEvents();

    // Development shortcut: ESC to close app
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
      glfwSetWindowShouldClose(window, GLFW_TRUE);
    }

    // Check if search needs to be updated due to asset changes FIRST
    if (search_state.update_needed.exchange(false)) {
      // Re-apply current search filter to include updated assets
      filter_assets(search_state, assets, assets_mutex, search_index);

      // Removes texture cache entries and thumbnails for deleted assets
      texture_manager.process_cleanup_queue();
    }

    // Process pending debounced search
    if (search_state.pending_search) {
      auto now = std::chrono::steady_clock::now();
      auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - search_state.last_keypress_time).count();

      if (elapsed >= Config::SEARCH_DEBOUNCE_MS) {
        // Execute the search
        filter_assets(search_state, assets, assets_mutex, search_index);
        search_state.last_buffer = search_state.buffer;
        search_state.pending_search = false;
      }
    }

    // Start the Dear ImGui frame
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    // Handle keyboard input
    ImGuiIO& io = ImGui::GetIO();

    // Spacebar pause/unpause for audio assets
    if (ImGui::IsKeyPressed(ImGuiKey_Space) && !io.WantTextInput) {
      if (search_state.selected_asset.has_value()) {
        const Asset& sel = *search_state.selected_asset;
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
    if (ImGui::IsKeyPressed(ImGuiKey_P) && !io.WantTextInput) {
      texture_manager.print_texture_cache();
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
    float window_width = ImGui::GetContentRegionAvail().x;
    float window_height = ImGui::GetContentRegionAvail().y;
    float WINDOW_MARGIN = 6.0f;
    float left_width = window_width * 0.75f - WINDOW_MARGIN;
    float right_width = window_width * 0.25f - WINDOW_MARGIN;
    float top_height = window_height * 0.20f - WINDOW_MARGIN;
    float bottom_height = window_height * 0.80f - WINDOW_MARGIN;

    // ============ TOP LEFT: Search Box ============
    render_search_panel(search_state, assets, assets_mutex, search_index, left_width, top_height);

    // ============ TOP RIGHT: Progress and Messages ============
    ImGui::SameLine();
    render_progress_panel(g_event_processor, right_width, top_height);

    // ============ BOTTOM LEFT: Search Results ============
    render_asset_grid(search_state, texture_manager, assets, left_width, bottom_height);

    // ============ BOTTOM RIGHT: Preview Panel ============
    ImGui::SameLine();
    render_preview_panel(search_state, texture_manager, audio_manager, current_model, camera, right_width, bottom_height);

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

  // Cleanup audio manager
  audio_manager.cleanup();

  // Cleanup texture manager
  texture_manager.cleanup();

  // Cleanup 3D preview resources
  cleanup_model(current_model);

  // Cleanup
  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();

  // Stop file watcher and close database
  file_watcher.stop_watching();

  // Cleanup event processor
  g_event_processor->stop();
  delete g_event_processor;

  database.close();

  // Destroy shared thumbnail context
  glfwDestroyWindow(thumbnail_context);
  glfwDestroyWindow(window);
  glfwTerminate();

  return 0;
}
