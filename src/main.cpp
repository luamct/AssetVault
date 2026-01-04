#include <glad/glad.h>
#ifdef _WIN32
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#define GLFW_EXPOSE_NATIVE_WIN32
#include <windows.h>
#endif
#include <GLFW/glfw3.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <string>
#include <utility>
#include <thread>

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
#include "fonts.h"
#include "ui/ui.h"
#include "services.h"
#include "drag_drop.h"

namespace fs = std::filesystem;

namespace {
  constexpr int WINDOW_WIDTH = 1960;
  constexpr int WINDOW_HEIGHT = 1080;
  constexpr float SEARCH_PANEL_HEIGHT = 65.0f;
  constexpr int SEARCH_DEBOUNCE_MS = 250;

  // Side panel vertical sizing
  constexpr float PREVIEW_RATIO = 0.62f;  // Initial guess until the preview panel measures its content.
  constexpr float PROGRESS_RATIO = 0.05f;
  constexpr float GAP_RATIO = 0.01f;

  // File event callback function (runs on background thread)
  // Queues events for unified processing
  void on_file_event(const FileEvent& event) {
    LOG_TRACE("[NEW_EVENT] type = {}, asset = {}", FileWatcher::file_event_type_to_string(event.type), event.path);
    Services::event_processor().queue_event(event);
  }

  void on_content_scale_changed(GLFWwindow* window, float x_scale, float y_scale) {
    void* user_ptr = glfwGetWindowUserPointer(window);
    if (!user_ptr) {
      return;
    }
    UIState* ui_state = static_cast<UIState*>(user_ptr);
    ui_state->pending_dpi_scale = std::max(x_scale, y_scale);
    ui_state->dpi_scale_dirty = true;
  }

  void apply_ui_scale(ImGuiIO& io, UIState& ui_state, float dpi_scale) {
    ui_state.dpi_scale = std::clamp(dpi_scale, 0.5f, 3.0f);
    float font_raster_scale = ui_state.dpi_scale;

#ifdef __APPLE__
    // GLFW reports DisplaySize in points on macOS. Keep logical UI sizing stable, while
    // rasterizing fonts at higher resolution for crisp rendering.
    ui_state.ui_scale = 1.0f;
    io.FontGlobalScale = 1.0f / ui_state.dpi_scale;
#else
    // GLFW reports DisplaySize in pixels on most other platforms. Scale UI with DPI.
    ui_state.ui_scale = ui_state.dpi_scale;
    io.FontGlobalScale = 1.0f;
    font_raster_scale = ui_state.ui_scale;
#endif

    io.Fonts->Clear();
    Fonts::load_fonts(io, font_raster_scale);

    ImGui::GetStyle() = ImGuiStyle();  // reset to default
    Theme::setup_light_fun_theme();    // reapply theme colors/spacing
    ImGui::GetStyle().ScaleAllSizes(ui_state.ui_scale);
  }

}  // namespace

// Initialize ImGui UI system
static ImGuiIO* initialize_imgui(GLFWwindow* window, UIState& ui_state) {
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
  io.IniFilename = nullptr;  // Disable imgui.ini file

  float content_scale_x = 1.0f;
  float content_scale_y = 1.0f;
  glfwGetWindowContentScale(window, &content_scale_x, &content_scale_y);
  const float dpi_scale = std::max(content_scale_x, content_scale_y);
  ui_state.pending_dpi_scale = dpi_scale;
  ui_state.dpi_scale_dirty = false;
  apply_ui_scale(io, ui_state, dpi_scale);

  ImGui_ImplGlfw_InitForOpenGL(window, true);
  ImGui_ImplOpenGL3_Init("#version 330");

  return &io;
}

int run(std::atomic<bool>* shutdown_requested) {
  // Check if running in headless mode (via TESTING env var)
  bool headless_mode = std::getenv("TESTING") != nullptr;

  Logger::initialize(LogLevel::Info);
  LOG_INFO("AssetVault application starting {}", headless_mode ? " (headless mode)" : "...");
  LOG_INFO("Log file: {}", Logger::get_log_file_path().u8string());

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
#ifdef _WIN32
  SetProcessDPIAware();
#endif
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
  int window_width = headless_mode ? 1 : WINDOW_WIDTH;
  int window_height = headless_mode ? 1 : WINDOW_HEIGHT;

  GLFWwindow* window = glfwCreateWindow(window_width, window_height, "Asset Vault", nullptr, nullptr);
  if (!window) {
    LOG_ERROR("Failed to create GLFW window");
    glfwTerminate();
    return -1;
  }
  glfwSetWindowUserPointer(window, &ui_state);
  glfwSetWindowContentScaleCallback(window, on_content_scale_changed);

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
  EventProcessor event_processor(safe_assets, ui_state.event_batch_finished, ui_state.assets_directory, thumbnail_context);

  // Initialize drag-and-drop manager (platform-specific)
  DragDropManager* drag_drop_manager = create_drag_drop_manager();
  if (!drag_drop_manager || !drag_drop_manager->initialize(window)) {
    LOG_ERROR("Failed to initialize drag-and-drop manager");
    return -1;
  }

  // Register core services for global access
  Services::provide(&database, &search_index, &event_processor, &file_watcher,
    &texture_manager, &audio_manager, drag_drop_manager);
  LOG_INFO("Core services registered");

  // Start all services (includes database init, search index build, scanning, and file watcher)
  if (!Services::start(on_file_event, &safe_assets)) {
    LOG_ERROR("Failed to start services");
    return -1;
  }

  ui_state.assets_directory = Config::assets_directory();

  // If the saved directory no longer exists, reset it (behave as if never set)
  if (!ui_state.assets_directory.empty()) {
    std::error_code ec;
    if (!std::filesystem::exists(ui_state.assets_directory, ec) ||
      !std::filesystem::is_directory(ui_state.assets_directory, ec)) {
      LOG_WARN("Saved assets directory no longer exists: {}. Resetting to unset state.", ui_state.assets_directory);
      ui_state.assets_directory.clear();
      Config::set_assets_directory("");
    }
  }

  // Restore persisted UI preferences
  ui_state.grid_zoom_level = static_cast<ZoomLevel>(Config::grid_zoom_level());
  ui_state.preview_projection = Config::preview_projection();
  camera.projection = (ui_state.preview_projection == Config::CONFIG_VALUE_PROJECTION_PERSPECTIVE) ?
    CameraProjection::Perspective : CameraProjection::Orthographic;

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
    ImGuiIO* io_ptr = initialize_imgui(window, ui_state);

    // UI mode: full rendering loop
    double last_time = glfwGetTime();
    LOG_INFO("Entering main rendering loop");

    // Main loop - check both window close and shutdown request
    while (!glfwWindowShouldClose(window) && (!shutdown_requested || !shutdown_requested->load())) {
      double current_time = glfwGetTime();
      io_ptr->DeltaTime = (float) (current_time - last_time);
      last_time = current_time;

      glfwPollEvents();
      if (ui_state.dpi_scale_dirty) {
        ui_state.dpi_scale_dirty = false;
        apply_ui_scale(*io_ptr, ui_state, ui_state.pending_dpi_scale);
      }

      // Reset folder tree if a processing batch finished
      if (ui_state.event_batch_finished.exchange(false)) {
        reset_folder_tree_state(ui_state);
        ui_state.filters_changed = true;
        ui_state.preserve_loaded_range = true;
      }

      // Apply pending filter changes (tree selections, batch refreshes, etc.)
      if (ui_state.filters_changed.exchange(false)) {
        bool preserve_loaded_range = std::exchange(ui_state.preserve_loaded_range, false);
        // Re-apply current search filter to include updated assets
        filter_assets(ui_state, safe_assets, preserve_loaded_range);

        // Removes texture cache entries and thumbnails for deleted assets
        texture_manager.process_cleanup_queue(ui_state.assets_directory);
      }

      // Process pending debounced search
      if (ui_state.pending_search) {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
          now - ui_state.last_keypress_time).count();

        if (elapsed >= SEARCH_DEBOUNCE_MS) {
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

      if (ImGui::IsKeyPressed(ImGuiKey_Escape) && !input_io.WantTextInput) {
        if (ui_state.assets_directory_modal_open) {
          ui_state.close_assets_directory_modal_requested = true;
        }
        else if (ui_state.settings_modal_open) {
          ui_state.close_settings_modal_requested = true;
        }
        else {
          glfwSetWindowShouldClose(window, GLFW_TRUE);
        }
      }

      // Spacebar pause/unpause for audio assets
      if (ImGui::IsKeyPressed(ImGuiKey_Space) && !input_io.WantTextInput) {
        if (ui_state.selected_asset.has_value()) {
          const Asset& sel = *ui_state.selected_asset;
          if (sel.type == AssetType::Audio && Services::audio_manager().has_audio_loaded()) {
            if (Services::audio_manager().is_playing()) {
              Services::audio_manager().pause();
            }
            else {
              Services::audio_manager().play();
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
      const float ui_scale = ui_state.ui_scale;
      const float WINDOW_FRAME_MARGIN = 20.0f * ui_scale;
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
        "Asset Vault", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoNavFocus);
      ImGui::PopStyleVar(3);

      // Calculate panel sizes using actual window content area
      float content_width = ImGui::GetContentRegionAvail().x;
      float content_height = ImGui::GetContentRegionAvail().y;
      float WINDOW_MARGIN = 6.0f * ui_scale;
      float spacing_y = ImGui::GetStyle().ItemSpacing.y;
      float left_width = content_width * 0.75f - WINDOW_MARGIN;
      float right_width = content_width * 0.25f - WINDOW_MARGIN;
      float top_height = SEARCH_PANEL_HEIGHT * ui_scale;
      float bottom_height = content_height - top_height - spacing_y;
      if (bottom_height < 0.0f) {
        bottom_height = 0.0f;
      }

      float clamped_height = std::max(0.0f, content_height);
      float progress_height = clamped_height * PROGRESS_RATIO;
      float vertical_gap = clamped_height * GAP_RATIO;
      float preview_height = (ui_state.preview_panel_height > 0.0f)
        ? ui_state.preview_panel_height
        : clamped_height * PREVIEW_RATIO;
      if (!ui_state.selected_asset.has_value()) {
        preview_height = std::max(preview_height, clamped_height * 0.5f);
      }
      float folder_tree_height = std::max(0.0f, clamped_height - preview_height - progress_height - vertical_gap * 2.0f);

      // Left column (search + grid)
      ImGui::BeginGroup();
      render_search_panel(ui_state, safe_assets, texture_manager, left_width, top_height);
      render_asset_grid(ui_state, texture_manager, safe_assets, left_width, bottom_height);
      ImGui::EndGroup();

      ImGui::SameLine();

      // Right column (preview + progress)
      float original_item_spacing_x = ImGui::GetStyle().ItemSpacing.x;
      ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(original_item_spacing_x, 0.0f));
      ImGui::BeginGroup();
      render_preview_panel(ui_state, texture_manager, current_model, camera, right_width, preview_height);
      ImGui::Dummy(ImVec2(0.0f, vertical_gap));
      render_folder_tree_panel(ui_state, texture_manager, right_width, folder_tree_height);
      ImGui::Dummy(ImVec2(0.0f, vertical_gap));
      render_progress_panel(ui_state, safe_assets, texture_manager, right_width, progress_height);
      ImGui::EndGroup();
      ImGui::PopStyleVar();

      ImGui::End();

      // Rendering
      ImGui::Render();
      int display_w, display_h;
      glfwGetFramebufferSize(window, &display_w, &display_h);
      glViewport(0, 0, display_w, display_h);
      glClearColor(
        Theme::BACKGROUND_CHARCOAL.x, Theme::BACKGROUND_CHARCOAL.y, Theme::BACKGROUND_CHARCOAL.z,
        Theme::BACKGROUND_CHARCOAL.w);
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

#if defined(ASSET_INVENTORY_ENTRYPOINT) && !defined(ASSET_VAULT_ENTRYPOINT)
#define ASSET_VAULT_ENTRYPOINT ASSET_INVENTORY_ENTRYPOINT
#endif

#ifndef ASSET_VAULT_ENTRYPOINT
#ifdef _WIN32
#define ASSET_VAULT_ENTRYPOINT wWinMain
#else
#define ASSET_VAULT_ENTRYPOINT main
#endif
#endif

#ifdef _WIN32
int APIENTRY ASSET_VAULT_ENTRYPOINT(HINSTANCE, HINSTANCE, PWSTR, int) {
  return run(nullptr);
}
#else
int ASSET_VAULT_ENTRYPOINT() {
  return run(nullptr);
}
#endif
