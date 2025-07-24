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
#include <unordered_set>
#include <vector>

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

// Include stb_image for PNG loading
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

// Include NanoSVG for SVG support
#define NANOSVG_IMPLEMENTATION
#include "nanosvg.h"
#define NANOSVGRAST_IMPLEMENTATION
#include "nanosvgrast.h"

// All constants now defined in config.h

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

// Global variables
std::vector<FileInfo> g_assets;
std::atomic<bool> g_assets_updated(false);
std::atomic<bool> g_initial_scan_complete(false);
AssetDatabase g_database;
FileWatcher g_file_watcher;
TextureManager g_texture_manager;

// Unified event processor for both initial scan and runtime events
EventProcessor* g_event_processor = nullptr;
std::atomic<bool> g_search_update_needed(false);

bool load_roboto_font(ImGuiIO& io) {
  // Load embedded Roboto font from external/fonts directory
  ImFont* font = io.Fonts->AddFontFromFileTTF(Config::FONT_PATH, Config::FONT_SIZE);
  if (font) {
    std::cout << "Roboto font loaded successfully!\n";
    return true;
  }

  // If embedded font fails to load, log error and use default font
  std::cerr << "Failed to load embedded Roboto font. Check that " << Config::FONT_PATH << " exists.\n";
  return false;
}

// Function to calculate aspect-ratio-preserving dimensions with upscaling limit
ImVec2 calculate_thumbnail_size(
  int original_width, int original_height, float max_width, float max_height, float max_upscale_factor = Config::MAX_THUMBNAIL_UPSCALE_FACTOR) {
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
void filter_assets(SearchState& search_state) {
  auto start_time = std::chrono::high_resolution_clock::now();

  search_state.filtered_assets.clear();
  search_state.selected_asset_index = -1; // Clear selection when search results change

  constexpr size_t MAX_RESULTS = Config::MAX_SEARCH_RESULTS; // Limit results to prevent UI blocking
  size_t total_assets = 0;
  size_t filtered_count = 0;

  // Lock assets during filtering to prevent race conditions
  if (g_event_processor) {
    std::lock_guard<std::mutex> lock(g_event_processor->get_assets_mutex());

    total_assets = g_assets.size();
    for (const auto& asset : g_assets) {
      // Skip auxiliary files - they should never appear in search results
      if (asset.type == AssetType::Auxiliary) {
        continue;
      }

      if (asset_matches_search(asset, search_state.buffer)) {
        search_state.filtered_assets.push_back(asset);
        filtered_count++;

        // Stop at maximum results to prevent UI blocking
        if (search_state.filtered_assets.size() >= MAX_RESULTS) {
          break;
        }
      }
    }
  }
  else {
    // Fallback for when event processor isn't initialized yet
    total_assets = g_assets.size();
    for (const auto& asset : g_assets) {
      // Skip auxiliary files - they should never appear in search results
      if (asset.type == AssetType::Auxiliary) {
        continue;
      }

      if (asset_matches_search(asset, search_state.buffer)) {
        search_state.filtered_assets.push_back(asset);
        filtered_count++;

        // Stop at maximum results to prevent UI blocking
        if (search_state.filtered_assets.size() >= MAX_RESULTS) {
          break;
        }
      }
    }
  }
}


// File event callback function (runs on background thread)
// Queues events for unified processing
void on_file_event(const FileEvent& event) {
  if (g_event_processor) {
    g_event_processor->queue_event(event);
  }
}

// Note: File event processing now handled by EventProcessor in background thread

// Perform initial scan and generate events for EventProcessor
void perform_initial_scan() {
    namespace fs = std::filesystem;
    auto scan_start = std::chrono::high_resolution_clock::now();
    
    std::cout << "Starting smart incremental asset scanning...\n";
    
    // Get current database state
    std::vector<FileInfo> db_assets = g_database.get_all_assets();
    std::cout << "Database contains " << db_assets.size() << " assets\n";
    std::unordered_map<std::string, FileInfo> db_map;
    for (const auto& asset : db_assets) {
        db_map[asset.full_path] = asset;
    }
    
    // Phase 1: Get filesystem paths (fast scan)
    std::unordered_set<std::string> current_files;
    try {
        fs::path root(Config::ASSET_ROOT_DIRECTORY);
        if (!fs::exists(root) || !fs::is_directory(root)) {
            std::cerr << "Error: Path does not exist or is not a directory: " << Config::ASSET_ROOT_DIRECTORY << "\n";
            g_initial_scan_complete = true;
            return;
        }
        
        std::cout << "Scanning directory: " << Config::ASSET_ROOT_DIRECTORY << "\n";
        
        // Single pass: Get all file paths
        for (const auto& entry : fs::recursive_directory_iterator(root)) {
            try {
                std::string path_str = entry.path().string();
                current_files.insert(path_str);
            }
            catch (const fs::filesystem_error& e) {
                std::cerr << "Warning: Could not access " << entry.path().string() << ": " << e.what() << '\n';
                continue;
            }
        }
        
        std::cout << "Found " << current_files.size() << " files and directories\n";
    }
    catch (const fs::filesystem_error& e) {
        std::cerr << "Error scanning directory: " << e.what() << '\n';
        g_initial_scan_complete = true;
        return;
    }
    
    // Track what events need to be generated
    std::vector<FileEvent> events_to_queue;
    std::unordered_set<std::string> found_paths;
    
    // Get current timestamp for all events
    auto current_time = std::chrono::system_clock::now();
    
    // Compare filesystem state with database state
    for (const auto& path : current_files) {
        found_paths.insert(path);
        
        auto db_it = db_map.find(path);
        if (db_it == db_map.end()) {
            // File not in database - create a Created event
            FileEventType event_type = fs::is_directory(path) ? FileEventType::DirectoryCreated : FileEventType::Created;
            FileEvent event(event_type, path);
            event.timestamp = current_time;
            events_to_queue.push_back(event);
        }
        else {
            // File exists in database - check if modified
            const FileInfo& db_asset = db_it->second;
            
            // Skip timestamp comparison for directories
            if (fs::is_directory(path)) {
                continue;
            }
            
            // Get the max of creation and modification time for current file
            uint32_t current_timestamp = EventProcessor::get_file_timestamp_for_comparison(path);
            
            // Direct integer comparison
            if (current_timestamp > db_asset.created_or_modified_seconds) {
                // File has been modified - create a Modified event
                FileEvent event(FileEventType::Modified, path);
                event.timestamp = current_time;
                events_to_queue.push_back(event);
            }
        }
    }
    
    // Find files in database that no longer exist on filesystem
    for (const auto& db_asset : db_assets) {
        if (found_paths.find(db_asset.full_path) == found_paths.end()) {
            // File no longer exists - create a Deleted event
            FileEventType event_type = db_asset.is_directory ? FileEventType::DirectoryDeleted : FileEventType::Deleted;
            FileEvent event(event_type, db_asset.full_path);
            event.timestamp = current_time;
            events_to_queue.push_back(event);
        }
    }
    
    auto scan_end = std::chrono::high_resolution_clock::now();
    auto scan_duration = std::chrono::duration_cast<std::chrono::milliseconds>(scan_end - scan_start);
    
    std::cout << "Filesystem scan completed in " << scan_duration.count() << "ms\n";
    std::cout << "Found " << events_to_queue.size() << " changes to process\n";
    
    // Queue all events to the EventProcessor
    if (!events_to_queue.empty()) {
        auto queue_start = std::chrono::high_resolution_clock::now();
        g_event_processor->queue_events(events_to_queue);
        auto queue_end = std::chrono::high_resolution_clock::now();
        auto queue_duration = std::chrono::duration_cast<std::chrono::milliseconds>(queue_end - queue_start);
        
        std::cout << "Published " << events_to_queue.size() << " events to EventProcessor in " 
                  << queue_duration.count() << "ms\n";
    }
    else {
        // No events to process, but we still need to load existing assets from database
        std::cout << "No changes detected, loading existing assets from database\n";
        if (g_event_processor) {
            std::lock_guard<std::mutex> lock(g_event_processor->get_assets_mutex());
            g_assets = db_assets; // Load existing database assets into g_assets
        }
        else {
            g_assets = db_assets; // Fallback if event processor not available
        }
        std::cout << "Loaded " << g_assets.size() << " existing assets\n";
    }
    
    // Mark initial scan as complete
    g_initial_scan_complete = true;
}

int main() {
  // Initialize database
  std::cout << "Initializing database...\n";
  if (!g_database.initialize(Config::DATABASE_PATH)) {
    std::cerr << "Failed to initialize database\n";
    return -1;
  }

  // Debug: Force clear database if flag is set
  if (Config::DEBUG_FORCE_DB_CLEAR) {
    std::cout << "DEBUG: Forcing database clear for testing...\n";
    g_database.clear_all_assets();
  }

  // Initialize unified event processor for both initial scan and runtime events
  g_event_processor = new EventProcessor(g_database, g_assets, g_search_update_needed, g_texture_manager, Config::EVENT_PROCESSOR_BATCH_SIZE);
  if (!g_event_processor->start()) {
    std::cerr << "Failed to start EventProcessor\n";
    return -1;
  }

  // Perform initial scan synchronously before starting UI
  // This ensures all events are queued before the main loop begins
  perform_initial_scan();

  // Initialize GLFW
  if (!glfwInit()) {
    std::cerr << "Failed to initialize GLFW\n";
    return -1;
  }

  // Create window
  GLFWwindow* window = glfwCreateWindow(Config::WINDOW_WIDTH, Config::WINDOW_HEIGHT, "Asset Inventory", nullptr, nullptr);
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

  // 3D preview initialization now handled by TextureManager

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

  // Initialize texture manager
  if (!g_texture_manager.initialize()) {
    std::cerr << "Failed to initialize texture manager\n";
    return -1;
  }

  // Initialize 3D preview system
  if (!g_texture_manager.initialize_preview_system()) {
    std::cerr << "Warning: Failed to initialize 3D preview system\n";
  }

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
      if (g_file_watcher.start_watching(Config::ASSET_ROOT_DIRECTORY, on_file_event)) {
        std::cout << "File watcher started successfully\n";
      }
      else {
        std::cerr << "Failed to start file watcher\n";
      }
    }

    // Render 3D preview to framebuffer BEFORE starting ImGui frame
    if (g_texture_manager.is_preview_initialized()) {
      // Calculate the size for the right panel (same as 2D previews)
      float right_panel_width = (ImGui::GetIO().DisplaySize.x * 0.25f) - Config::PREVIEW_RIGHT_MARGIN;
      float avail_width = right_panel_width - Config::PREVIEW_INTERNAL_PADDING;
      float avail_height = avail_width; // Square aspect ratio

      int fb_width = static_cast<int>(avail_width);
      int fb_height = static_cast<int>(avail_height);

      // Render the 3D preview
      render_3d_preview(fb_width, fb_height, search_state.current_model, g_texture_manager);
    }

    // Process texture invalidation queue (thread-safe, once per frame)
    g_texture_manager.process_invalidation_queue();

    // Start the Dear ImGui frame
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    // Check if search needs to be updated due to asset changes
    if (g_search_update_needed.exchange(false)) {
      // Re-apply current search filter to include updated assets
      filter_assets(search_state);
    }

    // Keep old asset reload for initial scan compatibility (for now)
    if (g_assets_updated.exchange(false)) {
      g_assets = g_database.get_all_assets();
      filter_assets(search_state);
    }

    // Apply initial filter when we first have assets
    if (!search_state.initial_filter_applied && !g_assets.empty()) {
      filter_assets(search_state);
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
    float right_width = window_width * 0.25f - Config::PREVIEW_RIGHT_MARGIN;
    float top_height = window_height * 0.15f;
    float bottom_height = window_height * 0.85f - 20.0f; // Account for some padding

    // ============ TOP LEFT: Search Box ============
    ImGui::BeginChild("SearchRegion", ImVec2(left_width, top_height), true);

    // Get the actual usable content area (accounts for child window borders/padding)
    ImVec2 content_region = ImGui::GetContentRegionAvail();

    // Calculate centered position within content region
    float content_search_x = (content_region.x - Config::SEARCH_BOX_WIDTH) * 0.5f;
    float content_search_y = (content_region.y - Config::SEARCH_BOX_HEIGHT) * 0.5f;

    // Ensure we have a minimum Y position
    if (content_search_y < 5.0f) {
      content_search_y = 5.0f;
    }

    // Get screen position for drawing (content area start + our offset)
    ImVec2 content_start = ImGui::GetCursorScreenPos();
    ImVec2 capsule_min(content_start.x + content_search_x, content_start.y + content_search_y);
    ImVec2 capsule_max(capsule_min.x + Config::SEARCH_BOX_WIDTH, capsule_min.y + Config::SEARCH_BOX_HEIGHT);

    // Draw capsule background
    ImGui::GetWindowDrawList()->AddRectFilled(
      capsule_min, capsule_max,
      COLOR_WHITE, // White background
      25.0f        // Rounded corners
    );

    // Position text input - ImGui text inputs are positioned by their center, not top-left
    float text_input_x = content_search_x + 40;                       // 40px padding from left edge of capsule
    float text_input_y = content_search_y + Config::SEARCH_BOX_HEIGHT * 0.5f; // Center of capsule

    ImGui::SetCursorPos(ImVec2(text_input_x, text_input_y));
    ImGui::PushItemWidth(Config::SEARCH_BOX_WIDTH - 40); // Leave 20px padding on each side

    // Remove borders from text input
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, COLOR_TRANSPARENT_32); // Transparent background

    ImGui::InputText("##Search", search_state.buffer, sizeof(search_state.buffer), ImGuiInputTextFlags_EnterReturnsTrue);

    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
    ImGui::PopItemWidth();

    // Only filter if search terms have changed
    if (std::string(search_state.buffer) != search_state.last_buffer) {
      filter_assets(search_state);
      search_state.last_buffer = search_state.buffer;
    }

    ImGui::EndChild();

    // ============ TOP RIGHT: Progress and Messages ============
    ImGui::SameLine();
    ImGui::BeginChild("ProgressRegion", ImVec2(right_width, top_height), true);

    // Unified progress bar for all asset processing
    bool show_progress = (g_event_processor && g_event_processor->has_pending_work());

    if (show_progress) {
      ImGui::TextColored(COLOR_HEADER_TEXT, "Processing Assets");

      // Progress bar data from event processor
      float progress = g_event_processor->get_progress();
      size_t processed = g_event_processor->get_total_processed();
      size_t total = g_event_processor->get_total_queued();

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
    float item_height = Config::THUMBNAIL_SIZE + Config::TEXT_MARGIN + Config::TEXT_HEIGHT; // Full item height including text
    // Add GRID_SPACING to available width since we don't need spacing after the
    // last item
    int columns = static_cast<int>((available_width + Config::GRID_SPACING) / (Config::THUMBNAIL_SIZE + Config::GRID_SPACING));
    if (columns < 1)
      columns = 1;

    // Display filtered assets in a proper grid
    for (size_t i = 0; i < search_state.filtered_assets.size(); i++) {
      // Calculate grid position
      int row = static_cast<int>(i) / columns;
      int col = static_cast<int>(i) % columns;

      // Calculate absolute position for this grid item
      float x_pos = col * (Config::THUMBNAIL_SIZE + Config::GRID_SPACING);
      float y_pos = row * (item_height + Config::GRID_SPACING);

      // Set cursor to the calculated position
      ImGui::SetCursorPos(ImVec2(x_pos, y_pos));

      ImGui::BeginGroup();

      // Get texture for this asset
      unsigned int asset_texture = g_texture_manager.get_asset_texture(search_state.filtered_assets[i]);

      // Calculate display size based on asset type
      ImVec2 display_size(Config::THUMBNAIL_SIZE, Config::THUMBNAIL_SIZE);
      bool is_texture = (search_state.filtered_assets[i].type == AssetType::Texture && asset_texture != 0);
      if (is_texture) {
        int width, height;
        if (g_texture_manager.get_texture_dimensions(search_state.filtered_assets[i].full_path, width, height)) {
          display_size =
            calculate_thumbnail_size(width, height, Config::THUMBNAIL_SIZE, Config::THUMBNAIL_SIZE, Config::MAX_THUMBNAIL_UPSCALE_FACTOR); // upscaling for grid
        }
      }
      else {
        // For type icons, use a fixed fraction of the thumbnail size
        display_size = ImVec2(Config::THUMBNAIL_SIZE * Config::ICON_SCALE, Config::THUMBNAIL_SIZE * Config::ICON_SCALE);
      }

      // Create a fixed-size container for consistent layout
      ImVec2 container_size(Config::THUMBNAIL_SIZE,
        Config::THUMBNAIL_SIZE + Config::TEXT_MARGIN + Config::TEXT_HEIGHT); // Thumbnail + text area
      ImVec2 container_pos = ImGui::GetCursorScreenPos();

      // Draw background for the container (same as app background)
      ImGui::GetWindowDrawList()->AddRectFilled(
        container_pos, ImVec2(container_pos.x + container_size.x, container_pos.y + container_size.y),
        Theme::ToImU32(Theme::BACKGROUND_LIGHT_BLUE_1));

      // Center the image/icon in the thumbnail area
      float image_x_offset = (Config::THUMBNAIL_SIZE - display_size.x) * 0.5f;
      float image_y_offset = (Config::THUMBNAIL_SIZE - display_size.y) * 0.5f;
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
      ImGui::SetCursorScreenPos(ImVec2(container_pos.x, container_pos.y + Config::THUMBNAIL_SIZE + Config::TEXT_MARGIN));

      // Asset name below thumbnail
      std::string truncated_name = truncate_filename(search_state.filtered_assets[i].name);
      ImGui::SetCursorPosX(
        ImGui::GetCursorPosX() + (Config::THUMBNAIL_SIZE - ImGui::CalcTextSize(truncated_name.c_str()).x) * 0.5f);
      ImGui::TextWrapped("%s", truncated_name.c_str());

      ImGui::EndGroup();
    }

    // Show message if no assets found
    if (search_state.filtered_assets.empty()) {
      if (g_assets.empty()) {
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
    float avail_width = right_width - Config::PREVIEW_INTERNAL_PADDING; // Account for ImGui padding and margins
    float avail_height = avail_width;                           // Square aspect ratio for preview area

    if (search_state.selected_asset_index >= 0) {
      const FileInfo& selected_asset = search_state.filtered_assets[search_state.selected_asset_index];

      // Check if selected asset is a model
      if (selected_asset.type == AssetType::Model && g_texture_manager.is_preview_initialized()) {
        // Load the model if it's different from the currently loaded one
        if (selected_asset.full_path != search_state.current_model.path) {
          std::cout << "=== Loading Model in Main ===" << std::endl;
          std::cout << "Selected asset: " << selected_asset.full_path << std::endl;
          Model model;
          if (load_model(selected_asset.full_path, model, g_texture_manager)) {
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
        ImGui::Image((ImTextureID) (intptr_t) g_texture_manager.get_preview_texture(), viewport_size);

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
        unsigned int preview_texture = g_texture_manager.get_asset_texture(selected_asset);
        if (preview_texture != 0) {
          ImVec2 preview_size(avail_width, avail_height);

          if (selected_asset.type == AssetType::Texture) {
            int width, height;
            if (g_texture_manager.get_texture_dimensions(selected_asset.full_path, width, height)) {
              preview_size = calculate_thumbnail_size(width, height, avail_width, avail_height, Config::MAX_PREVIEW_UPSCALE_FACTOR);
            }
          }
          else {
            // For type icons, use ICON_SCALE * min(available_width, available_height)
            float icon_dim = Config::ICON_SCALE * std::min(avail_width, avail_height);
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
          if (g_texture_manager.get_texture_dimensions(selected_asset.full_path, width, height)) {
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

  // Cleanup texture manager
  g_texture_manager.cleanup();

  // Cleanup 3D preview resources
  cleanup_model(search_state.current_model);

  // Cleanup
  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();

  // Stop file watcher and close database
  g_file_watcher.stop_watching();

  // Cleanup event processor
  if (g_event_processor) {
    g_event_processor->stop();
    delete g_event_processor;
    g_event_processor = nullptr;
  }

  g_database.close();

  glfwDestroyWindow(window);
  glfwTerminate();

  return 0;
}
