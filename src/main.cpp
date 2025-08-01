#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <iostream>
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

// Global callback state (needed for callback functions)
EventProcessor* g_event_processor = nullptr;

// Function to render clickable path segments that add path filters when clicked
void render_clickable_path(const std::string& full_path, SearchState& search_state) {
  // Get relative path from assets folder
  std::string relative_path = get_relative_asset_path(full_path);
  
  // Split path into segments
  std::vector<std::string> segments;
  std::stringstream ss(relative_path);
  std::string segment;
  
  while (std::getline(ss, segment, '/')) {
    if (!segment.empty()) {
      segments.push_back(segment);
    }
  }
  
  // Render each segment as a clickable button
  for (size_t i = 0; i < segments.size(); ++i) {
    if (i > 0) {
      ImGui::SameLine();
      ImGui::TextColored(Theme::TEXT_SECONDARY, " / ");
      ImGui::SameLine();
    }
    
    // Build the path up to this segment
    std::string path_to_segment;
    for (size_t j = 0; j <= i; ++j) {
      if (j > 0) path_to_segment += "/";
      path_to_segment += segments[j];
    }
    
    // Create unique button ID
    std::string button_id = "##path_" + std::to_string(i) + "_" + segments[i];
    
    // Check if this path is already in the filter
    bool is_active = std::find(search_state.internal_path_filters.begin(), 
                               search_state.internal_path_filters.end(), 
                               path_to_segment) != search_state.internal_path_filters.end();
    
    // Style the button differently if it's active
    if (is_active) {
      ImGui::PushStyleColor(ImGuiCol_Button, Theme::ACCENT_BLUE_1);
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Theme::ACCENT_BLUE_2);
      ImGui::PushStyleColor(ImGuiCol_Text, Theme::TEXT_DARK);
    } else {
      ImGui::PushStyleColor(ImGuiCol_Button, Theme::FRAME_LIGHT_BLUE_1);
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Theme::BACKGROUND_LIGHT_BLUE_1);
      ImGui::PushStyleColor(ImGuiCol_Text, Theme::ACCENT_BLUE_1);
    }
    
    // Render clickable segment
    if (ImGui::SmallButton((segments[i] + button_id).c_str())) {
      // Toggle the path filter
      auto it = std::find(search_state.internal_path_filters.begin(), 
                          search_state.internal_path_filters.end(), 
                          path_to_segment);
      
      if (it != search_state.internal_path_filters.end()) {
        // Remove if already present
        search_state.internal_path_filters.erase(it);
      } else {
        // Add if not present
        search_state.internal_path_filters.push_back(path_to_segment);
      }
      
      // Trigger search update
      search_state.update_needed = true;
    }
    
    ImGui::PopStyleColor(3);
    
    // Show tooltip with the full path segment
    if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip("Click to filter by: %s", path_to_segment.c_str());
    }
  }
}

// Custom slider component for audio seek bar
bool AudioSeekBar(const char* id, float* value, float min_value, float max_value, float width, float height = 4.0f) {
  ImVec2 cursor_pos = ImGui::GetCursorScreenPos();

  // Calculate dimensions
  const float handle_radius = height * 2.0f; // Circle handle is ~4x the line height
  const ImVec2 size(width, handle_radius * 2.0f);

  // Create invisible button for interaction
  ImGui::InvisibleButton(id, size);
  bool hovered = ImGui::IsItemHovered();
  bool active = ImGui::IsItemActive();

  // Calculate value based on mouse position when dragging
  bool value_changed = false;
  if (active) {
    ImVec2 mouse_pos = ImGui::GetMousePos();
    float mouse_x = mouse_pos.x - cursor_pos.x;
    float new_value = (mouse_x / width) * (max_value - min_value) + min_value;
    if (new_value < min_value) new_value = min_value;
    if (new_value > max_value) new_value = max_value;
    if (*value != new_value) {
      *value = new_value;
      value_changed = true;
    }
  }

  // Calculate current position
  float position_ratio = (max_value > min_value) ? (*value - min_value) / (max_value - min_value) : 0.0f;
  if (position_ratio < 0.0f) position_ratio = 0.0f;
  if (position_ratio > 1.0f) position_ratio = 1.0f;
  float handle_x = cursor_pos.x + position_ratio * width;

  // Colors
  const ImU32 line_color_played = ImGui::GetColorU32(ImVec4(0.3f, 0.3f, 0.3f, 1.0f));    // Darker line (played portion - before handle)
  const ImU32 line_color_unplayed = ImGui::GetColorU32(ImVec4(0.7f, 0.7f, 0.7f, 1.0f));  // Lighter line (unplayed portion - after handle)
  const ImU32 handle_color = hovered || active ?
    ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 1.0f, 1.0f)) :    // White when hovered/active
    ImGui::GetColorU32(ImVec4(0.9f, 0.9f, 0.9f, 1.0f));     // Light gray normally

  // Draw the seek bar
  ImDrawList* draw_list = ImGui::GetWindowDrawList();

  // Line center Y position
  float line_y = cursor_pos.y + size.y * 0.5f;

  // Draw played portion (left of handle) - darker
  if (position_ratio > 0.0f) {
    draw_list->AddRectFilled(
      ImVec2(cursor_pos.x, line_y - height * 0.5f),
      ImVec2(handle_x, line_y + height * 0.5f),
      line_color_played,
      height * 0.5f
    );
  }

  // Draw unplayed portion (right of handle) - lighter
  if (position_ratio < 1.0f) {
    draw_list->AddRectFilled(
      ImVec2(handle_x, line_y - height * 0.5f),
      ImVec2(cursor_pos.x + width, line_y + height * 0.5f),
      line_color_unplayed,
      height * 0.5f
    );
  }

  // Draw circular handle
  draw_list->AddCircleFilled(
    ImVec2(handle_x, line_y),
    handle_radius,
    handle_color,
    16  // Number of segments for smooth circle
  );

  return value_changed;
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

// Fancy text input box with rounded corners and shadow
bool fancy_text_input(const char* label, char* buffer, size_t buffer_size, float width,
  float padding_x = 20.0f, float padding_y = 16.0f,
  float corner_radius = 25.0f, ImGuiInputTextFlags flags = 0) {

  ImGui::PushItemWidth(width);

  // Calculate the actual height of the text input box
  float font_height = ImGui::GetFontSize();
  float actual_input_height = font_height + (padding_y * 2.0f);

  // Style the text input with rounded corners
  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, corner_radius);
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(padding_x, padding_y));
  ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(1.0f, 1.0f, 1.0f, 1.0f)); // White background
  ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.98f, 0.98f, 0.98f, 1.0f)); // Slightly darker on hover
  ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0.95f, 0.95f, 0.95f, 1.0f)); // Even darker when active

  // Draw shadow effect behind the text input
  ImVec2 shadow_offset(2.0f, 2.0f);
  ImVec2 input_pos = ImGui::GetCursorScreenPos();
  ImVec2 shadow_min(input_pos.x + shadow_offset.x, input_pos.y + shadow_offset.y);
  ImVec2 shadow_max(shadow_min.x + width, shadow_min.y + actual_input_height);

  ImGui::GetWindowDrawList()->AddRectFilled(
    shadow_min, shadow_max,
    ImGui::GetColorU32(ImVec4(0.0f, 0.0f, 0.0f, 0.12f)),
    corner_radius
  );

  bool result = ImGui::InputText(label, buffer, buffer_size, flags);

  ImGui::PopStyleColor(3);
  ImGui::PopStyleVar(2);
  ImGui::PopItemWidth();

  return result;
}

// Custom toggle button drawing function
bool draw_type_toggle_button(const char* label, bool& toggle_state, float x_pos, float y_pos,
  float button_width, float button_height) {
  ImVec2 button_min(x_pos, y_pos);
  ImVec2 button_max(button_min.x + button_width, button_min.y + button_height);

  // Check if mouse is hovering over button
  ImVec2 mouse_pos = ImGui::GetMousePos();
  bool is_hovered = (mouse_pos.x >= button_min.x && mouse_pos.x <= button_max.x &&
    mouse_pos.y >= button_min.y && mouse_pos.y <= button_max.y);

  // Choose colors based on state
  ImVec4 bg_color = toggle_state ? Theme::TOGGLE_ON_BG :
    (is_hovered ? Theme::TOGGLE_HOVER_BG : Theme::TOGGLE_OFF_BG);
  ImVec4 border_color = toggle_state ? Theme::TOGGLE_ON_BORDER : Theme::TOGGLE_OFF_BORDER;
  ImVec4 text_color = toggle_state ? Theme::TOGGLE_ON_TEXT : Theme::TOGGLE_OFF_TEXT;

  // Draw button background
  ImGui::GetWindowDrawList()->AddRectFilled(
    button_min, button_max,
    Theme::ToImU32(bg_color),
    8.0f  // Rounded corners
  );

  // Draw button border
  ImGui::GetWindowDrawList()->AddRect(
    button_min, button_max,
    Theme::ToImU32(border_color),
    8.0f,  // Rounded corners
    0,     // No corner flags
    2.0f   // Border thickness
  );

  // Draw text centered in button
  ImVec2 text_size = ImGui::CalcTextSize(label);
  ImVec2 text_pos(
    button_min.x + (button_width - text_size.x) * 0.5f,
    button_min.y + (button_height - text_size.y) * 0.5f
  );

  ImGui::GetWindowDrawList()->AddText(
    text_pos,
    Theme::ToImU32(text_color),
    label
  );

  // Handle click detection
  bool clicked = false;
  if (is_hovered && ImGui::IsMouseClicked(0)) {
    toggle_state = !toggle_state;
    clicked = true;
  }

  return clicked;
}

// File event callback function (runs on background thread)
// Queues events for unified processing
void on_file_event(const FileEvent& event) {
  g_event_processor->queue_event(event);
}

// Perform initial scan and generate events for EventProcessor
void scan_for_changes(AssetDatabase& database, std::vector<Asset>& assets, EventProcessor* event_processor) {
  namespace fs = std::filesystem;
  auto scan_start = std::chrono::high_resolution_clock::now();

  LOG_INFO("Starting scan for changes...");

  // Get current database state
  std::vector<Asset> db_assets = database.get_all_assets();
  LOG_INFO("Database contains {} assets", db_assets.size());
  std::unordered_map<std::filesystem::path, Asset> db_map;
  for (const auto& asset : db_assets) {
    db_map[asset.full_path] = asset;
  }

  // Phase 1: Get filesystem paths (fast scan)
  std::unordered_set<std::filesystem::path> current_files;
  try {
    fs::path root(Config::ASSET_ROOT_DIRECTORY);
    if (!fs::exists(root) || !fs::is_directory(root)) {
      LOG_ERROR("Path does not exist or is not a directory: {}", Config::ASSET_ROOT_DIRECTORY);
      return;
    }

    LOG_INFO("Scanning directory: {}", Config::ASSET_ROOT_DIRECTORY);

    // Single pass: Get all file paths
    for (const auto& entry : fs::recursive_directory_iterator(root)) {
      try {
        current_files.insert(entry.path());
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
      FileEventType event_type = fs::is_directory(path) ? FileEventType::DirectoryCreated : FileEventType::Created;
      FileEvent event(event_type, path);
      event.timestamp = current_time;
      events_to_queue.push_back(event);
    }
    // Skip expensive timestamp checks - file modifications will be caught by runtime file watcher
  }

  LOG_INFO("Now looking for deleted files");

  // Find files in database that no longer exist on filesystem
  for (const auto& db_asset : db_assets) {
    if (current_files.find(db_asset.full_path) == current_files.end()) {
      // File no longer exists - create a Deleted event
      FileEventType event_type = db_asset.is_directory ? FileEventType::DirectoryDeleted : FileEventType::Deleted;
      FileEvent event(event_type, db_asset.full_path);
      event.timestamp = current_time;
      events_to_queue.push_back(event);
    }
  }

  auto scan_end = std::chrono::high_resolution_clock::now();
  auto scan_duration = std::chrono::duration_cast<std::chrono::milliseconds>(scan_end - scan_start);

  LOG_INFO("Filesystem scan completed in {}ms", scan_duration.count());
  LOG_INFO("Found {} changes to process", events_to_queue.size());

  // Always load existing assets from database first
  {
    std::lock_guard<std::mutex> lock(event_processor->get_assets_mutex());
    assets = db_assets; // Load existing database assets into assets
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

  // Local variables
  std::vector<Asset> assets;
  AssetDatabase database;
  FileWatcher file_watcher;
  TextureManager texture_manager;
  AudioManager audio_manager;
  SearchState search_state;

  // Initialize database
  LOG_INFO("Initializing database...");
  if (!database.initialize(Config::DATABASE_PATH)) {
    LOG_ERROR("Failed to initialize database");
    return -1;
  }
  LOG_INFO("Database opened successfully: {}", Config::DATABASE_PATH);

  // Debug: Force clear database if flag is set
  if (Config::DEBUG_FORCE_DB_CLEAR) {
    LOG_INFO("DEBUG: Forcing database clear for testing...");
    database.clear_all_assets();
  }

  // Initialize unified event processor for both initial scan and runtime events
  g_event_processor = new EventProcessor(database, assets, search_state.update_needed, texture_manager, Config::EVENT_PROCESSOR_BATCH_SIZE);
  if (!g_event_processor->start()) {
    LOG_ERROR("Failed to start EventProcessor");
    return -1;
  }
  LOG_INFO("EventProcessor started successfully");

  // Initialize GLFW
  if (!glfwInit()) {
    LOG_ERROR("Failed to initialize GLFW");
    return -1;
  }

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

  scan_for_changes(database, assets, g_event_processor);

  // Start file watcher after initial scan
  LOG_INFO("Starting file watcher...");
  if (file_watcher.start_watching(Config::ASSET_ROOT_DIRECTORY, on_file_event)) {
    LOG_INFO("File watcher started successfully");
  }
  else {
    LOG_ERROR("Failed to start file watcher");
  }

  // Main loop
  double last_time = glfwGetTime();
  while (!glfwWindowShouldClose(window)) {
    double current_time = glfwGetTime();
    io.DeltaTime = (float) (current_time - last_time);
    last_time = current_time;

    glfwPollEvents();

    // Render 3D preview to framebuffer BEFORE starting ImGui frame
    {
      // Calculate the size for the right panel (same as 2D previews)
      float right_panel_width = (ImGui::GetIO().DisplaySize.x * 0.25f) - Config::PREVIEW_RIGHT_MARGIN;
      float avail_width = right_panel_width - Config::PREVIEW_INTERNAL_PADDING;
      float avail_height = avail_width; // Square aspect ratio

      int fb_width = static_cast<int>(avail_width);
      int fb_height = static_cast<int>(avail_height);

      // Render the 3D preview
      render_3d_preview(fb_width, fb_height, search_state.current_model, texture_manager);
    }

    // Process texture invalidation queue (thread-safe, once per frame)
    texture_manager.process_invalidation_queue();

    // Process pending debounced search
    if (search_state.pending_search) {
      auto now = std::chrono::steady_clock::now();
      auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - search_state.last_keypress_time).count();

      if (elapsed >= Config::SEARCH_DEBOUNCE_MS) {
        // Execute the search
        filter_assets(search_state, assets, g_event_processor->get_assets_mutex());
        search_state.last_buffer = search_state.buffer;
        search_state.pending_search = false;
      }
    }

    // Start the Dear ImGui frame
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    // Check if search needs to be updated due to asset changes
    if (search_state.update_needed.exchange(false)) {
      // Re-apply current search filter to include updated assets
      filter_assets(search_state, assets, g_event_processor->get_assets_mutex());
    }

    // Create main window
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
    ImGui::Begin(
      "Asset Inventory", nullptr,
      ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoCollapse);

    // Calculate panel sizes
    float window_width = ImGui::GetWindowSize().x;
    float window_height = ImGui::GetWindowSize().y;
    float left_width = window_width * 0.75f;
    float right_width = window_width * 0.25f - Config::PREVIEW_RIGHT_MARGIN;
    float top_height = window_height * 0.20f;
    float bottom_height = window_height * 0.80f - 20.0f; // Account for some padding

    // ============ TOP LEFT: Search Box ============
    ImGui::BeginChild("SearchRegion", ImVec2(left_width, top_height), true);

    // Get the actual usable content area (accounts for child window borders/padding)
    ImVec2 content_region = ImGui::GetContentRegionAvail();

    // Calculate centered position within content region - move search box up
    float content_search_x = (content_region.x - Config::SEARCH_BOX_WIDTH) * 0.5f;
    float content_search_y = (content_region.y - Config::SEARCH_BOX_HEIGHT) * 0.3f;

    // Get screen position for drawing (content area start + our offset)
    ImVec2 content_start = ImGui::GetCursorScreenPos();

    // Position and draw the fancy search text input
    ImGui::SetCursorPos(ImVec2(content_search_x, content_search_y));
    bool enter_pressed = fancy_text_input("##Search", search_state.buffer, sizeof(search_state.buffer), Config::SEARCH_BOX_WIDTH, 20.0f, 16.0f, 25.0f, ImGuiInputTextFlags_EnterReturnsTrue);

    // Handle search input
    std::string current_input(search_state.buffer);

    if (enter_pressed) {
      // Immediate search on Enter key
      filter_assets(search_state, assets, g_event_processor->get_assets_mutex());
      search_state.last_buffer = current_input;
      search_state.input_tracking = current_input;
      search_state.pending_search = false;
    }
    else if (current_input != search_state.input_tracking) {
      // Debounced search: only mark as pending if input actually changed
      search_state.input_tracking = current_input;
      search_state.last_keypress_time = std::chrono::steady_clock::now();
      search_state.pending_search = true;
    }

    // ============ TYPE FILTER TOGGLE BUTTONS ============

    // Position toggle buttons below the search box
    float toggles_y = content_search_y + Config::SEARCH_BOX_HEIGHT + 30.0f; // 30px gap below search box
    float toggle_button_height = 35.0f;
    float toggle_spacing = 20.0f;

    // Individual button widths - tweak these as needed
    float button_width_2d = 48.0f;      // "2D" is short
    float button_width_3d = 48.0f;      // "3D" is short
    float button_width_audio = 84.0f;   // "Audio" is longer
    float button_width_shader = 96.0f;  // "Shader" is longer
    float button_width_font = 72.0f;    // "Font" is medium

    // Calculate centered starting position for all toggle buttons
    float total_toggle_width = button_width_2d + button_width_3d + button_width_audio +
      button_width_shader + button_width_font + (toggle_spacing * 4);
    float toggles_start_x = content_search_x + (Config::SEARCH_BOX_WIDTH - total_toggle_width) * 0.5f;

    // Draw all toggle buttons using the dedicated function
    bool any_toggle_changed = false;
    float current_x = toggles_start_x;

    any_toggle_changed |= draw_type_toggle_button("2D", search_state.type_filter_2d,
      content_start.x + current_x, content_start.y + toggles_y,
      button_width_2d, toggle_button_height);
    current_x += button_width_2d + toggle_spacing;

    any_toggle_changed |= draw_type_toggle_button("3D", search_state.type_filter_3d,
      content_start.x + current_x, content_start.y + toggles_y,
      button_width_3d, toggle_button_height);
    current_x += button_width_3d + toggle_spacing;

    any_toggle_changed |= draw_type_toggle_button("Audio", search_state.type_filter_audio,
      content_start.x + current_x, content_start.y + toggles_y,
      button_width_audio, toggle_button_height);
    current_x += button_width_audio + toggle_spacing;

    any_toggle_changed |= draw_type_toggle_button("Shader", search_state.type_filter_shader,
      content_start.x + current_x, content_start.y + toggles_y,
      button_width_shader, toggle_button_height);
    current_x += button_width_shader + toggle_spacing;

    any_toggle_changed |= draw_type_toggle_button("Font", search_state.type_filter_font,
      content_start.x + current_x, content_start.y + toggles_y,
      button_width_font, toggle_button_height);

    // If any toggle changed, trigger immediate search
    if (any_toggle_changed) {
      filter_assets(search_state, assets, g_event_processor->get_assets_mutex());
      search_state.pending_search = false;
    }

    ImGui::EndChild();

    // ============ TOP RIGHT: Progress and Messages ============
    ImGui::SameLine();
    ImGui::BeginChild("ProgressRegion", ImVec2(right_width, top_height), true);

    // Unified progress bar for all asset processing
    bool show_progress = (g_event_processor && g_event_processor->has_pending_work());

    if (show_progress) {
      ImGui::TextColored(Theme::TEXT_HEADER, "Processing Assets");

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

      ImGui::GetWindowDrawList()->AddText(text_pos, Theme::ToImU32(Theme::TEXT_DARK), progress_text);
    }

    ImGui::EndChild();

    // ============ BOTTOM LEFT: Search Results ============
    ImGui::BeginChild("AssetGrid", ImVec2(left_width, bottom_height), true);

    // Show total results count if we have results
    if (!search_state.filtered_assets.empty()) {
      ImGui::Text("Showing %d of %zu results", search_state.loaded_end_index, search_state.filtered_assets.size());
      ImGui::Separator();
    }

    // Calculate grid layout upfront since all items have the same size
    float available_width = left_width - 20.0f;                     // Account for padding
    float item_height = Config::THUMBNAIL_SIZE + Config::TEXT_MARGIN + Config::TEXT_HEIGHT; // Full item height including text
    // Add GRID_SPACING to available width since we don't need spacing after the
    // last item
    int columns = static_cast<int>((available_width + Config::GRID_SPACING) / (Config::THUMBNAIL_SIZE + Config::GRID_SPACING));
    if (columns < 1)
      columns = 1;

    // Calculate visible range
    float current_scroll_y = ImGui::GetScrollY();
    float viewport_height = ImGui::GetWindowHeight();

    // Calculate visible range for lazy loading
    float row_height = item_height + Config::GRID_SPACING;
    int first_visible_row = static_cast<int>(current_scroll_y / row_height);
    int last_visible_row = static_cast<int>((current_scroll_y + viewport_height) / row_height);

    // Add margin for smooth scrolling (1 row above/below)
    first_visible_row = std::max(0, first_visible_row - 1);
    last_visible_row = last_visible_row + 1;

    int first_visible_item = first_visible_row * columns;
    int last_visible_item = std::min(search_state.loaded_end_index,
      (last_visible_row + 1) * columns);

    // Check if we need to load more items (when approaching the end of loaded items)
    int load_threshold_row = (search_state.loaded_end_index - SearchState::LOAD_BATCH_SIZE / 2) / columns;
    if (last_visible_row >= load_threshold_row && search_state.loaded_end_index < static_cast<int>(search_state.filtered_assets.size())) {
      // Load more items
      search_state.loaded_end_index = std::min(
        search_state.loaded_end_index + SearchState::LOAD_BATCH_SIZE,
        static_cast<int>(search_state.filtered_assets.size())
      );
    }


    // Reserve space for all loaded items to enable proper scrolling
    int total_loaded_rows = (search_state.loaded_end_index + columns - 1) / columns;
    float total_content_height = total_loaded_rows * row_height;

    // Save current cursor position
    ImVec2 grid_start_pos = ImGui::GetCursorPos();

    // Reserve space for the entire loaded content
    ImGui::Dummy(ImVec2(0, total_content_height));

    // Display filtered assets in a proper grid - only process visible items within loaded range
    for (int i = first_visible_item; i < last_visible_item && i < search_state.loaded_end_index; i++) {
      // Calculate grid position
      int row = static_cast<int>(i) / columns;
      int col = static_cast<int>(i) % columns;

      // Calculate absolute position for this grid item relative to grid start
      float x_pos = grid_start_pos.x + col * (Config::THUMBNAIL_SIZE + Config::GRID_SPACING);
      float y_pos = grid_start_pos.y + row * (item_height + Config::GRID_SPACING);

      // Set cursor to the calculated position
      ImGui::SetCursorPos(ImVec2(x_pos, y_pos));

      ImGui::BeginGroup();

      // Load texture (all items in loop are visible now)
      TextureCacheEntry texture_entry = texture_manager.get_asset_texture(search_state.filtered_assets[i]);

      // Calculate display size based on asset type
      ImVec2 display_size(Config::THUMBNAIL_SIZE, Config::THUMBNAIL_SIZE);

      // Check if this asset has actual thumbnail dimensions (textures or 3D model thumbnails)
      bool has_thumbnail_dimensions = false;
      if (search_state.filtered_assets[i].type == AssetType::_2D || search_state.filtered_assets[i].type == AssetType::_3D) {
        // TextureCacheEntry already contains the dimensions, no need for separate call
        has_thumbnail_dimensions = (texture_entry.width > 0 && texture_entry.height > 0);
      }

      if (has_thumbnail_dimensions) {
        display_size = calculate_thumbnail_size(
          texture_entry.width, texture_entry.height,
          Config::THUMBNAIL_SIZE, Config::THUMBNAIL_SIZE,
          Config::MAX_THUMBNAIL_UPSCALE_FACTOR
        ); // upscaling for grid
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

      ImGui::PushStyleColor(ImGuiCol_Button, Theme::COLOR_TRANSPARENT);
      ImGui::PushStyleColor(ImGuiCol_ButtonActive, Theme::COLOR_TRANSPARENT);
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Theme::COLOR_SEMI_TRANSPARENT);

      // Display thumbnail image
      ImGui::SetCursorScreenPos(image_pos);
      if (ImGui::ImageButton(
        ("##Thumbnail" + std::to_string(i)).c_str(), (ImTextureID) (intptr_t) texture_entry.texture_id, display_size)) {
        search_state.selected_asset_index = static_cast<int>(i);
        LOG_DEBUG("Selected: {}", search_state.filtered_assets[i].name);
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
      if (assets.empty()) {
        ImGui::TextColored(Theme::TEXT_DISABLED_DARK, "No assets found. Add files to the 'assets' directory.");
      }
      else {
        ImGui::TextColored(Theme::TEXT_DISABLED_DARK, "No assets match your search.");
      }
    }


    ImGui::EndChild();

    // ============ BOTTOM RIGHT: Preview Panel ============
    ImGui::SameLine();
    ImGui::BeginChild("AssetPreview", ImVec2(right_width, bottom_height), true);

    // Use fixed panel dimensions for stable calculations
    float avail_width = right_width - Config::PREVIEW_INTERNAL_PADDING; // Account for ImGui padding and margins
    float avail_height = avail_width;                           // Square aspect ratio for preview area

    // Track previously selected asset for cleanup
    static int prev_selected_index = -1;
    static AssetType prev_selected_type = AssetType::Unknown;

    // Handle asset selection changes
    if (search_state.selected_asset_index != prev_selected_index) {
      // If we were playing audio and switched to a different asset, stop and unload
      if (prev_selected_type == AssetType::Audio && audio_manager.has_audio_loaded()) {
        audio_manager.unload_audio();
      }
      prev_selected_index = search_state.selected_asset_index;
      if (search_state.selected_asset_index >= 0) {
        prev_selected_type = search_state.filtered_assets[search_state.selected_asset_index].type;
      }
      else {
        prev_selected_type = AssetType::Unknown;
      }
    }

    if (search_state.selected_asset_index >= 0) {
      const Asset& selected_asset = search_state.filtered_assets[search_state.selected_asset_index];

      // Check if selected asset is a model
      if (selected_asset.type == AssetType::_3D && texture_manager.is_preview_initialized()) {
        // Load the model if it's different from the currently loaded one
        if (selected_asset.full_path.u8string() != search_state.current_model.path) {
          LOG_DEBUG("=== Loading Model in Main ===");
          LOG_DEBUG("Selected asset: {}", selected_asset.full_path.u8string());
          Model model;
          if (load_model(selected_asset.full_path.u8string(), model, texture_manager)) {
            set_current_model(search_state.current_model, model);
            LOG_DEBUG("Model loaded successfully in main");
          }
          else {
            LOG_DEBUG("Failed to load model in main");
          }
          LOG_DEBUG("===========================");
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
        ImGui::GetWindowDrawList()->AddRect(border_min, border_max, Theme::COLOR_BORDER_GRAY_U32, 8.0f, 0, 1.0f);

        // Display the 3D viewport
        ImGui::Image((ImTextureID) (intptr_t) texture_manager.get_preview_texture(), viewport_size);

        // Restore cursor for info below
        ImGui::SetCursorScreenPos(container_pos);
        ImGui::Dummy(ImVec2(0, avail_height + 10));

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // 3D Model information
        ImGui::TextColored(Theme::TEXT_LABEL, "Path: ");
        ImGui::SameLine();
        render_clickable_path(selected_asset.full_path.u8string(), search_state);

        ImGui::TextColored(Theme::TEXT_LABEL, "Extension: ");
        ImGui::SameLine();
        ImGui::Text("%s", selected_asset.extension.c_str());

        ImGui::TextColored(Theme::TEXT_LABEL, "Type: ");
        ImGui::SameLine();
        ImGui::Text("%s", get_asset_type_string(selected_asset.type).c_str());

        ImGui::TextColored(Theme::TEXT_LABEL, "Size: ");
        ImGui::SameLine();
        ImGui::Text("%s", format_file_size(selected_asset.size).c_str());

        // Display vertex and face counts from the loaded model
        if (current_model.loaded) {
          int vertex_count =
            static_cast<int>(current_model.vertices.size() / 8);             // 8 floats per vertex (3 pos + 3 normal + 2 tex)
          int face_count = static_cast<int>(current_model.indices.size() / 3); // 3 indices per triangle

          ImGui::TextColored(Theme::TEXT_LABEL, "Vertices: ");
          ImGui::SameLine();
          ImGui::Text("%d", vertex_count);

          ImGui::TextColored(Theme::TEXT_LABEL, "Faces: ");
          ImGui::SameLine();
          ImGui::Text("%d", face_count);
        }

        // Format and display last modified time
        auto time_t = std::chrono::system_clock::to_time_t(selected_asset.last_modified);
        std::tm tm_buf;
        localtime_s(&tm_buf, &time_t);
        std::stringstream ss;
        ss << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S");
        ImGui::TextColored(Theme::TEXT_LABEL, "Modified: ");
        ImGui::SameLine();
        ImGui::Text("%s", ss.str().c_str());
      }
      else if (selected_asset.type == AssetType::Audio && audio_manager.is_initialized()) {
        // Audio handling for sound assets

        // Load the audio file if it's different from the currently loaded one
        if (selected_asset.full_path.u8string() != audio_manager.get_current_file()) {
          bool loaded = audio_manager.load_audio(selected_asset.full_path.u8string());
          if (loaded) {
            // Set initial volume to match our slider default
            audio_manager.set_volume(0.5f);
            // Auto-play if enabled
            if (search_state.auto_play_audio) {
              audio_manager.play();
            }
          }
        }

        // Display audio icon in preview area
        TextureCacheEntry audio_entry = texture_manager.get_asset_texture(selected_asset);
        if (audio_entry.texture_id != 0) {
          float icon_dim = Config::ICON_SCALE * std::min(avail_width, avail_height);
          ImVec2 icon_size(icon_dim, icon_dim);

          // Center the icon
          ImVec2 container_pos = ImGui::GetCursorScreenPos();
          float image_x_offset = (avail_width - icon_size.x) * 0.5f;
          float image_y_offset = (avail_height - icon_size.y) * 0.5f;
          ImVec2 image_pos(container_pos.x + image_x_offset, container_pos.y + image_y_offset);
          ImGui::SetCursorScreenPos(image_pos);

          ImGui::Image((ImTextureID) (intptr_t) audio_entry.texture_id, icon_size);

          // Restore cursor for controls below
          ImGui::SetCursorScreenPos(container_pos);
          ImGui::Dummy(ImVec2(0, avail_height + 10));
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // Audio controls - single row layout
        if (audio_manager.has_audio_loaded()) {
          float duration = audio_manager.get_duration();
          float position = audio_manager.get_position();
          bool is_playing = audio_manager.is_playing();

          // Format time helper lambda
          auto format_time = [](float seconds) -> std::string {
            int mins = static_cast<int>(seconds) / 60;
            int secs = static_cast<int>(seconds) % 60;
            char buffer[16];
            snprintf(buffer, sizeof(buffer), "%02d:%02d", mins, secs);
            return std::string(buffer);
            };

          // Create a single row with all controls
          ImGui::BeginGroup();

          // 1. Square Play/Pause button with transparent background
          const float button_size = 32.0f;

          // Store the baseline Y position BEFORE drawing the button for proper alignment
          float baseline_y = ImGui::GetCursorPosY();

          unsigned int icon_texture = is_playing ? texture_manager.get_pause_icon() : texture_manager.get_play_icon();

          // Make button background transparent
          ImGui::PushStyleColor(ImGuiCol_Button, Theme::COLOR_TRANSPARENT);
          ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8f, 0.8f, 0.8f, 0.1f)); // Very light hover
          ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.7f, 0.7f, 0.7f, 0.2f));  // Light press

          if (ImGui::ImageButton("##PlayPause", (ImTextureID) (intptr_t) icon_texture, ImVec2(button_size, button_size))) {
            if (is_playing) {
              audio_manager.pause();
            }
            else {
              audio_manager.play();
            }
          }

          ImGui::PopStyleColor(3);

          ImGui::SameLine(0, 8); // 8px spacing

          // 2. Current timestamp
          ImGui::SetCursorPosY(baseline_y + button_size * 0.5f - 6.0);
          ImGui::Text("%s", format_time(position).c_str());

          ImGui::SameLine(0, 16);

          // 3. Custom seek bar - thin line with circle handle
          static bool seeking = false;
          static float seek_position = 0.0f;

          if (!seeking) {
            seek_position = position;
          }

          const float seek_bar_width = 120.0f;
          const float seek_bar_height = 4.0f; // Thin line height

          // Use our custom seek bar - vertically centered
          ImGui::SetCursorPosY(baseline_y + button_size * 0.5f - seek_bar_height); // Center based on handle radius
          bool seek_changed = AudioSeekBar("##CustomSeek", &seek_position, 0.0f, duration, seek_bar_width, seek_bar_height);

          if (seek_changed) {
            seeking = true;
            audio_manager.set_position(seek_position);
          }

          // Reset seeking flag when not actively dragging
          if (seeking && !ImGui::IsItemActive()) {
            seeking = false;
          }

          ImGui::SameLine(0, 12);

          // 4. Total duration
          ImGui::SetCursorPosY(baseline_y + button_size * 0.5f - 6.0f);
          ImGui::Text("%s", format_time(duration).c_str());

          ImGui::SameLine(0, 12);

          // 5. Speaker icon - vertically centered
          const float icon_size = 24.0f;
          ImGui::SetCursorPosY(baseline_y + (button_size - 0.5 * icon_size) * 0.5f);
          ImGui::Image((ImTextureID) (intptr_t) texture_manager.get_speaker_icon(), ImVec2(icon_size, icon_size));

          ImGui::SameLine(0, 6);

          // 6. Volume slider - custom horizontal seek bar style
          static float audio_volume = 0.5f; // Start at 50%
          const float volume_width = 60.0f;  // Small horizontal slider
          const float volume_height = 3.0f;   // Thinner than seek bar

          ImGui::SetCursorPosY(baseline_y + button_size * 0.5f);

          if (AudioSeekBar("##VolumeBar", &audio_volume, 0.0f, 1.0f, volume_width, volume_height)) {
            audio_manager.set_volume(audio_volume);
          }

          // Show percentage on hover
          if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Volume: %d%%", (int) (audio_volume * 100));
          }

          ImGui::EndGroup();

          // Auto-play checkbox below the player
          ImGui::Spacing();
          ImGui::Checkbox("Auto-play", &search_state.auto_play_audio);
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // File info
        ImGui::TextColored(Theme::TEXT_LABEL, "File: ");
        ImGui::SameLine();
        ImGui::Text("%s", selected_asset.name.c_str());

        ImGui::TextColored(Theme::TEXT_LABEL, "Size: ");
        ImGui::SameLine();
        ImGui::Text("%s", format_file_size(selected_asset.size).c_str());

        ImGui::TextColored(Theme::TEXT_LABEL, "Extension: ");
        ImGui::SameLine();
        ImGui::Text("%s", selected_asset.extension.c_str());

        ImGui::TextColored(Theme::TEXT_LABEL, "Path: ");
        ImGui::SameLine();
        render_clickable_path(selected_asset.full_path.u8string(), search_state);

        // Format and display last modified time
        auto time_t = std::chrono::system_clock::to_time_t(selected_asset.last_modified);
        std::tm tm_buf;
        localtime_s(&tm_buf, &time_t);
        std::stringstream ss;
        ss << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S");
        ImGui::TextColored(Theme::TEXT_LABEL, "Modified: ");
        ImGui::SameLine();
        ImGui::Text("%s", ss.str().c_str());
      }
      else {
        // 2D Preview for non-model assets
        TextureCacheEntry preview_entry = texture_manager.get_asset_texture(selected_asset);
        if (preview_entry.texture_id != 0) {
          ImVec2 preview_size(avail_width, avail_height);

          if (selected_asset.type == AssetType::_2D) {
            // TextureCacheEntry already contains dimensions
            if (preview_entry.width > 0 && preview_entry.height > 0) {
              preview_size = calculate_thumbnail_size(preview_entry.width, preview_entry.height, avail_width, avail_height, Config::MAX_PREVIEW_UPSCALE_FACTOR);
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
          ImGui::GetWindowDrawList()->AddRect(border_min, border_max, Theme::COLOR_BORDER_GRAY_U32, 8.0f, 0, 1.0f);

          ImGui::Image((ImTextureID) (intptr_t) preview_entry.texture_id, preview_size);

          // Restore cursor for info below
          ImGui::SetCursorScreenPos(container_pos);
          ImGui::Dummy(ImVec2(0, avail_height + 10));
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // Asset information
        ImGui::TextColored(Theme::TEXT_LABEL, "Name: ");
        ImGui::SameLine();
        ImGui::Text("%s", selected_asset.name.c_str());

        ImGui::TextColored(Theme::TEXT_LABEL, "Type: ");
        ImGui::SameLine();
        ImGui::Text("%s", get_asset_type_string(selected_asset.type).c_str());

        ImGui::TextColored(Theme::TEXT_LABEL, "Size: ");
        ImGui::SameLine();
        ImGui::Text("%s", format_file_size(selected_asset.size).c_str());

        ImGui::TextColored(Theme::TEXT_LABEL, "Extension: ");
        ImGui::SameLine();
        ImGui::Text("%s", selected_asset.extension.c_str());

        // Display dimensions for texture assets
        if (selected_asset.type == AssetType::_2D) {
          int width, height;
          if (texture_manager.get_texture_dimensions(selected_asset.full_path, width, height)) {
            ImGui::TextColored(Theme::TEXT_LABEL, "Dimensions: ");
            ImGui::SameLine();
            ImGui::Text("%dx%d", width, height);
          }
        }

        ImGui::TextColored(Theme::TEXT_LABEL, "Path: ");
        ImGui::SameLine();
        render_clickable_path(selected_asset.full_path.u8string(), search_state);

        // Format and display last modified time
        auto time_t = std::chrono::system_clock::to_time_t(selected_asset.last_modified);
        std::tm tm_buf;
        localtime_s(&tm_buf, &time_t);
        std::stringstream ss;
        ss << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S");
        ImGui::TextColored(Theme::TEXT_LABEL, "Modified: ");
        ImGui::SameLine();
        ImGui::Text("%s", ss.str().c_str());
      }
    }
    else {
      ImGui::TextColored(Theme::TEXT_DISABLED_DARK, "No asset selected");
      ImGui::TextColored(Theme::TEXT_DISABLED_DARK, "Click on an asset to preview");
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

  // Cleanup audio manager
  audio_manager.cleanup();

  // Cleanup texture manager
  texture_manager.cleanup();

  // Cleanup 3D preview resources
  cleanup_model(search_state.current_model);

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

  glfwDestroyWindow(window);
  glfwTerminate();

  return 0;
}
