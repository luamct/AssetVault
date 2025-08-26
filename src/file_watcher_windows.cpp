#include <windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <mutex>
#include <queue>
#include <thread>
#include <unordered_map>

#include "config.h"
#include "file_watcher.h"
#include "logger.h"
#include "asset.h"

// Structure to track pending file events
struct PendingFileEvent {
  FileEventType original_type;
  fs::path path;
  fs::path old_path;  // For rename events
  std::chrono::steady_clock::time_point last_activity;
  bool is_active;

  PendingFileEvent() : original_type(FileEventType::Modified), is_active(false) {}

  PendingFileEvent(FileEventType type, const fs::path& p, const fs::path& old = fs::path())
      : original_type(type), path(p), old_path(old), last_activity(std::chrono::steady_clock::now()), is_active(true) {}
};

class WindowsFileWatcher : public FileWatcherImpl {
 private:
  HANDLE h_directory;
  HANDLE h_event;
  std::thread watch_thread;
  std::atomic<bool> should_stop;
  std::atomic<bool> is_watching_flag;
  FileEventCallback callback;
  AssetMap* assets_;
  std::mutex* assets_mutex_;
  std::string watched_path;

  // Buffer for file change notifications
  char buffer[4096];
  DWORD bytes_returned;

  // Timer-based event tracking
  std::unordered_map<fs::path, PendingFileEvent> pending_events;
  std::mutex pending_events_mutex;
  std::thread timer_thread;
  std::atomic<bool> timer_should_stop;

  // Timer configuration - use config constant

 public:
  WindowsFileWatcher()
      : h_directory(INVALID_HANDLE_VALUE),
        h_event(INVALID_HANDLE_VALUE),
        should_stop(false),
        is_watching_flag(false),
        timer_should_stop(false) {}

  ~WindowsFileWatcher() { stop_watching(); }

  bool start_watching(const std::string& path, FileEventCallback cb, AssetMap* assets, std::mutex* assets_mutex) override {
    if (is_watching_flag.load()) {
      LOG_ERROR("Already watching a directory");
      return false;
    }

    watched_path = path;
    callback = cb;
    assets_ = assets;
    assets_mutex_ = assets_mutex;

    // Create event for signaling
    h_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (h_event == nullptr) {
      LOG_ERROR("Failed to create event: {}", GetLastError());
      return false;
    }

    // Open directory handle
    h_directory = CreateFileW(std::wstring(path.begin(), path.end()).c_str(), FILE_LIST_DIRECTORY,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                              FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr);

    if (h_directory == INVALID_HANDLE_VALUE) {
      LOG_ERROR("Failed to open directory: {}", GetLastError());
      CloseHandle(h_event);
      return false;
    }

    should_stop = false;
    timer_should_stop = false;
    is_watching_flag = true;

    // Start watching thread
    watch_thread = std::thread(&WindowsFileWatcher::watch_loop, this);

    // Start timer thread for processing pending events
    timer_thread = std::thread(&WindowsFileWatcher::timer_loop, this);

    LOG_INFO("Started watching directory: {}", path);
    return true;
  }

  void stop_watching() override {
    if (!is_watching_flag.load()) return;

    should_stop = true;
    timer_should_stop = true;

    // Signal the event to wake up the thread
    if (h_event != INVALID_HANDLE_VALUE) {
      SetEvent(h_event);
    }

    // Wait for threads to finish
    if (watch_thread.joinable()) {
      watch_thread.join();
    }

    if (timer_thread.joinable()) {
      timer_thread.join();
    }

    // Clean up handles
    if (h_directory != INVALID_HANDLE_VALUE) {
      CloseHandle(h_directory);
      h_directory = INVALID_HANDLE_VALUE;
    }

    if (h_event != INVALID_HANDLE_VALUE) {
      CloseHandle(h_event);
      h_event = INVALID_HANDLE_VALUE;
    }

    // Clear pending events
    {
      std::lock_guard<std::mutex> lock(pending_events_mutex);
      pending_events.clear();
    }

    is_watching_flag = false;
    LOG_INFO("Stopped watching directory: {}", watched_path);
  }

  bool is_watching() const override { return is_watching_flag.load(); }

 private:
  void timer_loop() {
    while (!timer_should_stop.load()) {
      auto now = std::chrono::steady_clock::now();
      std::vector<std::pair<fs::path, PendingFileEvent>> events_to_process;

      {
        std::lock_guard<std::mutex> lock(pending_events_mutex);

        for (auto it = pending_events.begin(); it != pending_events.end();) {
          auto& pending = it->second;

          if (!pending.is_active) {
            // Event has been cancelled (e.g., file deleted)
            it = pending_events.erase(it);
            continue;
          }

          auto time_since_activity =
              std::chrono::duration_cast<std::chrono::milliseconds>(now - pending.last_activity).count();

          if (time_since_activity >= Config::FILE_WATCHER_DEBOUNCE_MS) {
            // Event has timed out, store it for processing
            events_to_process.push_back({it->first, pending});
            it = pending_events.erase(it);
          } else {
            ++it;
          }
        }
      }

      // Process timed-out events
      for (const auto& [path, pending] : events_to_process) {
        if (callback) {
          // Determine the final event type based on the original event
          FileEventType final_type;
          if (pending.original_type == FileEventType::Created) {
            final_type = FileEventType::Created;
          } else {
            final_type = FileEventType::Modified;
          }

          FileEvent event(final_type, path, pending.old_path);
          callback(event);
        }
      }

      // Sleep for a short interval before checking again
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
  }

  void process_raw_file_event(FileEventType raw_type, const fs::path& full_path, const fs::path& old_path = fs::path()) {
    // Unified early filtering: skip directories, files without extensions, and ignored asset types
    bool is_directory = fs::is_directory(full_path);
    if (is_directory || !full_path.has_extension() || should_skip_asset(full_path.extension().string())) {
      return;
    }
    
    std::lock_guard<std::mutex> lock(pending_events_mutex);

    // For Deleted and Renamed, process immediately
    if (raw_type == FileEventType::Deleted || raw_type == FileEventType::Renamed) {
      if (callback) {
        FileEvent event(raw_type, full_path, old_path);
        callback(event);
      }
      return;
    }

    auto it = pending_events.find(full_path);
    if (it == pending_events.end()) {
      // First event for this file, store all attributes
      pending_events[full_path] = PendingFileEvent(raw_type, full_path, old_path);
    } else {
      // Only update last_activity and is_active
      it->second.last_activity = std::chrono::steady_clock::now();
      it->second.is_active = true;
      // Optionally, update old_path for rename tracking if needed
    }
  }

  void watch_loop() {
    OVERLAPPED overlapped = {0};
    overlapped.hEvent = h_event;

    while (!should_stop.load()) {
      // Start monitoring
      if (!ReadDirectoryChangesW(h_directory, buffer, sizeof(buffer),
                                 TRUE,  // Watch subtree
                                 FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME |
                                     FILE_NOTIFY_CHANGE_ATTRIBUTES | FILE_NOTIFY_CHANGE_SIZE |
                                     FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_CREATION,
                                 nullptr, &overlapped, nullptr)) {
        LOG_ERROR("ReadDirectoryChangesW failed: {}", GetLastError());
        break;
      }

      // Wait for either a change notification or stop signal
      DWORD wait_result = WaitForSingleObject(h_event, INFINITE);

      if (should_stop.load()) {
        break;
      }

      if (wait_result == WAIT_OBJECT_0) {
        // Get the overlapped result
        DWORD bytes_transferred;
        if (GetOverlappedResult(h_directory, &overlapped, &bytes_transferred, FALSE)) {
          process_file_changes(buffer, bytes_transferred);
        }

        // Reset event for next iteration
        ResetEvent(h_event);
      }
    }
  }

  void process_file_changes(const char* change_buffer, DWORD bytes_transferred) {
    (void)bytes_transferred;  // Suppress unused parameter warning

    const FILE_NOTIFY_INFORMATION* p_notify = reinterpret_cast<const FILE_NOTIFY_INFORMATION*>(change_buffer);

    while (p_notify) {
      // Convert wide string to fs::path directly (preserves Unicode correctly)
      std::wstring wide_name(p_notify->FileName, p_notify->FileNameLength / sizeof(WCHAR));
      
      // Create full path using fs::path to handle Unicode properly
      fs::path full_path = fs::path(watched_path) / wide_name;

      // Determine raw event type
      FileEventType raw_event_type = FileEventType::Modified;  // Default
      switch (p_notify->Action) {
        case FILE_ACTION_ADDED:
          raw_event_type = FileEventType::Created;
          break;
        case FILE_ACTION_REMOVED:
          raw_event_type = FileEventType::Deleted;
          break;
        case FILE_ACTION_MODIFIED:
          raw_event_type = FileEventType::Modified;
          break;
        case FILE_ACTION_RENAMED_OLD_NAME:
          // We'll handle rename in the next iteration
          break;
        case FILE_ACTION_RENAMED_NEW_NAME:
          raw_event_type = FileEventType::Renamed;
          break;
        default:
          raw_event_type = FileEventType::Modified;
          break;
      }

      // Process the raw event (will be debounced for Created/Modified)
      process_raw_file_event(raw_event_type, full_path);

      // Move to next notification
      if (p_notify->NextEntryOffset == 0) {
        break;
      }
      p_notify = reinterpret_cast<const FILE_NOTIFY_INFORMATION*>(reinterpret_cast<const char*>(p_notify) +
                                                                  p_notify->NextEntryOffset);
    }
  }

  // Helper function to convert wide string to UTF-8
  std::string wide_string_to_utf8(const std::wstring& wide_str) {
    if (wide_str.empty()) return std::string();

    // Convert wide string to UTF-8 using actual length (not null-terminated)
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, wide_str.data(), static_cast<int>(wide_str.length()), nullptr, 0,
                                          nullptr, nullptr);
    if (size_needed > 0) {
      std::string utf8_str(size_needed, 0);
      WideCharToMultiByte(CP_UTF8, 0, wide_str.data(), static_cast<int>(wide_str.length()), &utf8_str[0], size_needed,
                          nullptr, nullptr);
      return utf8_str;
    }
    return std::string();
  }
};

// Factory function for Windows implementation
std::unique_ptr<FileWatcherImpl> create_windows_file_watcher_impl() { return std::make_unique<WindowsFileWatcher>(); }
