#include "file_watcher.h"
#include "logger.h"
#include "services.h"
#include "event_processor.h"
#include "asset.h"
#include <filesystem>
#include <chrono>
#include <unordered_map>
#include <unordered_set>

namespace fs = std::filesystem;

// Factory functions
#ifdef _WIN32
std::unique_ptr<FileWatcherImpl> create_windows_file_watcher_impl();
#endif
#ifdef __APPLE__
std::unique_ptr<FileWatcherImpl> create_macos_file_watcher_impl();
#endif

std::unique_ptr<FileWatcherImpl> create_file_watcher_impl() {
#ifdef _WIN32
  LOG_INFO("Using native Windows ReadDirectoryChangesW file watcher");
  return create_windows_file_watcher_impl();
#elif defined(__APPLE__)
  LOG_INFO("Using native macOS FSEvents file watcher");
  return create_macos_file_watcher_impl();
#else
  LOG_ERROR("File watcher not implemented for this platform");
  return nullptr;
#endif
}

FileWatcher::FileWatcher() : p_impl(nullptr) {
  p_impl = create_file_watcher_impl();
  if (!p_impl) {
    LOG_ERROR("No file watcher implementation available");
  }
}

FileWatcher::~FileWatcher() { stop_watching(); }

bool FileWatcher::start_watching(const std::string& path, FileEventCallback callback, SafeAssets* assets) {
  LOG_DEBUG("Starting file watcher at {}", path);
  if (!p_impl) {
    LOG_ERROR("No file watcher implementation available");
    return false;
  }

  watched_path = path;
  is_watching_flag = true;

  return p_impl->start_watching(path, callback, assets);
}

void FileWatcher::stop_watching() {
  if (p_impl) {
    p_impl->stop_watching();
  }
  is_watching_flag = false;
}

bool FileWatcher::is_watching() const { return is_watching_flag && p_impl && p_impl->is_watching(); }

std::string FileWatcher::get_watched_path() const { return watched_path; }

void FileWatcher::set_file_extensions(const std::vector<std::string>& extensions) { file_extensions = extensions; }

void FileWatcher::set_polling_interval(int milliseconds) { polling_interval = milliseconds; }

std::string FileWatcher::file_event_type_to_string(FileEventType file_event_type) {
  return file_event_type == FileEventType::Created ? "Created" :
    file_event_type == FileEventType::Deleted ? "Deleted" : "Other";
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
