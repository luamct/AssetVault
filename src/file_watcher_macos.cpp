#include <CoreServices/CoreServices.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <queue>
#include <thread>
#include <unordered_map>

#include "config.h"
#include "file_watcher.h"
#include "logger.h"
#include "asset.h"
#include "utils.h"

// Structure to track pending file events
struct PendingFileEvent {
  FileEventType original_type;
  std::string path;  // UTF-8 encoded path with normalized separators
  std::chrono::steady_clock::time_point last_activity;
  bool is_active;

  PendingFileEvent() : original_type(FileEventType::Created), is_active(false) {}

  PendingFileEvent(FileEventType type, const std::string& p)
    : original_type(type), path(p), last_activity(std::chrono::steady_clock::now()), is_active(true) {
  }
};

class MacOSFileWatcher : public FileWatcherImpl {
private:
  FSEventStreamRef stream;
  std::thread watch_thread;
  std::atomic<bool> should_stop;
  std::atomic<bool> is_watching_flag;
  FileEventCallback callback;
  SafeAssets* safe_assets_;
  std::string watched_path;

  // Timer-based event tracking
  std::unordered_map<std::string, PendingFileEvent> pending_events;
  std::mutex pending_events_mutex;
  std::thread timer_thread;
  std::atomic<bool> timer_should_stop;


  // Store this instance for the callback
  static MacOSFileWatcher* current_instance;

  // Helper method to format FSEvents flags into readable string
  static std::string format_fsevents_flags(FSEventStreamEventFlags flags) {
    std::string flag_names;
    if (flags & kFSEventStreamEventFlagItemCreated) flag_names += "Created ";
    if (flags & kFSEventStreamEventFlagItemRemoved) flag_names += "Removed ";
    if (flags & kFSEventStreamEventFlagItemModified) flag_names += "Modified ";
    if (flags & kFSEventStreamEventFlagItemRenamed) flag_names += "Renamed ";
    if (flags & kFSEventStreamEventFlagItemIsDir) flag_names += "IsDir ";
    if (flags & kFSEventStreamEventFlagItemIsFile) flag_names += "IsFile ";
    if (flags & kFSEventStreamEventFlagItemIsSymlink) flag_names += "IsSymlink ";
    if (flags & kFSEventStreamEventFlagItemIsHardlink) flag_names += "IsHardlink ";
    if (flags & kFSEventStreamEventFlagItemIsLastHardlink) flag_names += "IsLastHardlink ";
    if (flags & kFSEventStreamEventFlagItemFinderInfoMod) flag_names += "FinderInfoMod ";
    if (flags & kFSEventStreamEventFlagItemChangeOwner) flag_names += "ChangeOwner ";
    if (flags & kFSEventStreamEventFlagItemXattrMod) flag_names += "XattrMod ";
    if (flags & kFSEventStreamEventFlagItemInodeMetaMod) flag_names += "InodeMetaMod ";
    if (flags & kFSEventStreamEventFlagItemCloned) flag_names += "Cloned ";
    if (flags & kFSEventStreamEventFlagOwnEvent) flag_names += "OwnEvent ";
    if (flags & kFSEventStreamEventFlagMustScanSubDirs) flag_names += "MustScanSubDirs ";
    if (flags & kFSEventStreamEventFlagUserDropped) flag_names += "UserDropped ";
    if (flags & kFSEventStreamEventFlagKernelDropped) flag_names += "KernelDropped ";
    if (flags & kFSEventStreamEventFlagEventIdsWrapped) flag_names += "EventIdsWrapped ";
    if (flags & kFSEventStreamEventFlagHistoryDone) flag_names += "HistoryDone ";
    if (flags & kFSEventStreamEventFlagRootChanged) flag_names += "RootChanged ";
    if (flags & kFSEventStreamEventFlagMount) flag_names += "Mount ";
    if (flags & kFSEventStreamEventFlagUnmount) flag_names += "Unmount ";

    // Remove trailing space
    if (!flag_names.empty() && flag_names.back() == ' ') {
      flag_names.pop_back();
    }

    return flag_names;
  }

  // Helper method to detect atomic save operations
  static bool is_atomic_save(FSEventStreamEventFlags flags) {
    // Check for the exact combination: Renamed + IsFile + XattrMod + Cloned
    const FSEventStreamEventFlags ATOMIC_SAVE_FLAGS =
      kFSEventStreamEventFlagItemRenamed |
      kFSEventStreamEventFlagItemIsFile |
      kFSEventStreamEventFlagItemXattrMod |
      kFSEventStreamEventFlagItemCloned;

    // Must have all these flags set
    return (flags & ATOMIC_SAVE_FLAGS) == ATOMIC_SAVE_FLAGS;
  }

public:
  MacOSFileWatcher()
    : stream(nullptr),
    should_stop(false),
    is_watching_flag(false),
    timer_should_stop(false) {
    current_instance = this;
  }

  ~MacOSFileWatcher() {
    stop_watching();
    current_instance = nullptr;
  }

  bool start_watching(const std::string& path, FileEventCallback cb, SafeAssets* safe_assets) override {
    if (is_watching_flag.load()) {
      LOG_ERROR("Already watching a directory");
      return false;
    }

    watched_path = path;
    callback = cb;
    safe_assets_ = safe_assets;

    should_stop = false;
    timer_should_stop = false;
    is_watching_flag = true;

    // Start watching thread
    watch_thread = std::thread(&MacOSFileWatcher::watch_loop, this);

    // Start timer thread for processing pending events
    timer_thread = std::thread(&MacOSFileWatcher::timer_loop, this);


    LOG_INFO("Started watching directory: {}", path);
    return true;
  }

  void stop_watching() override {
    if (!is_watching_flag.load()) {
      return;
    }

    should_stop = true;
    timer_should_stop = true;

    // Stop the FSEventStream if it's running
    if (stream) {
      FSEventStreamStop(stream);
      FSEventStreamInvalidate(stream);
      FSEventStreamRelease(stream);
      stream = nullptr;
    }

    // Wait for threads to finish
    if (watch_thread.joinable()) {
      watch_thread.join();
    }

    if (timer_thread.joinable()) {
      timer_thread.join();
    }

    is_watching_flag = false;
    LOG_INFO("Stopped watching directory");
  }

  bool is_watching() const override { return is_watching_flag.load(); }

private:
  static void fsevents_callback(
    ConstFSEventStreamRef streamRef,
    void* clientCallBackInfo,
    size_t numEvents,
    void* eventPaths,
    const FSEventStreamEventFlags eventFlags[],
    const FSEventStreamEventId eventIds[]) {

    auto* watcher = static_cast<MacOSFileWatcher*>(clientCallBackInfo);

    // Early exit if we're being shut down
    if (!watcher || watcher->should_stop.load()) {
      return;
    }

    char** paths = static_cast<char**>(eventPaths);

    for (size_t i = 0; i < numEvents; ++i) {
      std::string path(paths[i]);
      fs::path file_path(path);

      // Skip if it's the watched directory itself
      if (path == watcher->watched_path) {
        LOG_DEBUG("Skipped event {}", file_path.string());
        continue;
      }
      
      // Determine event type based on flags
      FSEventStreamEventFlags flags = eventFlags[i];

      // Get relative path for logging
      std::string relative_path = get_relative_path(path, watcher->watched_path);

      // Debug: Log only positive flags for this event
      LOG_TRACE("FSEvents: '{}' [0x{:X}] {}", relative_path, flags, format_fsevents_flags(flags));

      bool is_directory = (flags & kFSEventStreamEventFlagItemIsDir) != 0;

      // Check Renamed flag first as it can be combined with other flags
      if (flags & kFSEventStreamEventFlagItemRenamed) {
        // Check if this is an atomic save operation (file modified via temp file swap)
        if (is_atomic_save(flags)) {
          // This is an atomic save - send Delete+Create events
          watcher->add_pending_event(FileEventType::Deleted, path);
          watcher->add_pending_event(FileEventType::Created, path);
        }
        else if (watcher->safe_assets_) {
          // Not an atomic save - handle as a real rename/move
          // Only trigger events for actual moves, not metadata-only renames:
          // - Created: file exists AND is NOT tracked (moved in)
          // - Deleted: file doesn't exist AND IS tracked (moved out)
          // - Otherwise: ignore (metadata change or already handled)

          bool file_exists = fs::exists(file_path);

          if (is_directory) {
            // For directories, check if there are any tracked assets under this path
            bool has_tracked_assets = false;
            if (watcher->safe_assets_) {
              auto [lock, assets] = watcher->safe_assets_->read();
              std::string dir_path_str = file_path.generic_u8string();

              // Use binary search to find the first potential match
              auto it = assets.lower_bound(dir_path_str);

              // Check if any assets start with this directory path
              while (it != assets.end()) {
                const std::string& asset_path = it->first;

                // Check if this asset is under the directory
                if (asset_path.find(dir_path_str) != 0) {
                  break;  // No more assets under this directory
                }

                // Ensure it's actually a child path, not just a prefix match
                if (asset_path.length() > dir_path_str.length() &&
                  (dir_path_str.back() == '/' || asset_path[dir_path_str.length()] == '/')) {
                  has_tracked_assets = true;
                  break;
                }

                ++it;
              }
            }

            if (file_exists && !has_tracked_assets) {
              // Directory exists and has no tracked assets - it was moved TO this path
              watcher->handle_directory_moved_in(file_path);
            }
            else if (!file_exists && has_tracked_assets) {
              // Directory doesn't exist but has tracked assets - it was moved FROM this path
              watcher->emit_deletion_events_for_directory(file_path);
            }
            else {
              // Directory exists with tracked assets (in-place change) OR doesn't exist with no tracked assets (temp dir)
              LOG_TRACE("FSEvents: Ignoring directory rename event for '{}' (exists:{}, has_tracked:{})",
                relative_path, file_exists, has_tracked_assets);
            }
          }
          else {
            // For files, check if the specific file is tracked
            bool is_tracked;
            {
              auto [lock, assets] = watcher->safe_assets_->read();
              is_tracked = assets.find(file_path.generic_u8string()) != assets.end();
            }

            if (file_exists && !is_tracked) {
              // File exists and is not tracked - it was moved TO this path
              watcher->add_pending_event(FileEventType::Created, path);
            }
            else if (!file_exists && is_tracked) {
              // File doesn't exist but is tracked - it was moved FROM this path
              watcher->add_pending_event(FileEventType::Deleted, path);
            }
            else {
              // File exists and is tracked (metadata change) OR doesn't exist and isn't tracked (temp file)
              LOG_TRACE("FSEvents: Ignoring file rename event for '{}' (exists:{}, tracked:{})",
                relative_path, file_exists, is_tracked);
            }
          }
        }
      }
      else if (flags & kFSEventStreamEventFlagItemRemoved) {
        // Handle removal events - this should come before Created check
        // because FSEvents can set both Created+Removed for deletion
        if (is_directory) {
          // For directory deletion, emit events for all tracked assets under it
          // This handles the case where FSEvents doesn't report individual file deletions
          watcher->emit_deletion_events_for_directory(file_path);
        }
        else {
          // For file deletion, just emit the single event
          watcher->add_pending_event(FileEventType::Deleted, path);
        }
      }
      else if (flags & kFSEventStreamEventFlagItemCreated) {
        // Only treat as creation if it's NOT also a rename (rename takes precedence)
        if (!(flags & kFSEventStreamEventFlagItemRenamed)) {
          watcher->add_pending_event(FileEventType::Created, path);
        }
      }
      else if (flags & kFSEventStreamEventFlagItemModified) {
        if (!is_directory) {
          // Send Delete+Create for modifications
          watcher->add_pending_event(FileEventType::Deleted, path);
          watcher->add_pending_event(FileEventType::Created, path);
        }
      }
      else {
        // Check if file exists - if not, it's been deleted even without explicit flags
        if (!fs::exists(file_path)) {
          LOG_TRACE("FSEvents: File '{}' no longer exists (no explicit flags), treating as Deleted", relative_path);
          watcher->add_pending_event(FileEventType::Deleted, path);
        }
      }
    }
  }

  void watch_loop() {
    // Create CFString for the path
    CFStringRef path_cfstr = CFStringCreateWithCString(nullptr, watched_path.c_str(), kCFStringEncodingUTF8);
    CFArrayRef paths_to_watch = CFArrayCreate(nullptr, (const void**) &path_cfstr, 1, &kCFTypeArrayCallBacks);

    // Create the event stream
    FSEventStreamContext context = { 0, this, nullptr, nullptr, nullptr };
    stream = FSEventStreamCreate(
      nullptr,
      &MacOSFileWatcher::fsevents_callback,
      &context,
      paths_to_watch,
      kFSEventStreamEventIdSinceNow,
      0.01,  // Latency in seconds - reduced to 10ms for timing analysis
      kFSEventStreamCreateFlagFileEvents | kFSEventStreamCreateFlagNoDefer
    );

    if (!stream) {
      LOG_ERROR("Failed to create FSEventStream");
      CFRelease(paths_to_watch);
      CFRelease(path_cfstr);
      is_watching_flag = false;
      return;
    }

    // Create run loop for this thread
    CFRunLoopRef run_loop = CFRunLoopGetCurrent();
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    FSEventStreamScheduleWithRunLoop(stream, run_loop, kCFRunLoopDefaultMode);
#pragma clang diagnostic pop

    if (!FSEventStreamStart(stream)) {
      LOG_ERROR("Failed to start FSEventStream");
      FSEventStreamRelease(stream);
      stream = nullptr;
      CFRelease(paths_to_watch);
      CFRelease(path_cfstr);
      is_watching_flag = false;
      return;
    }

    // Run the event loop until should_stop is set
    while (!should_stop.load()) {
      CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.1, true);
    }

    // Cleanup
    FSEventStreamStop(stream);
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    FSEventStreamUnscheduleFromRunLoop(stream, run_loop, kCFRunLoopDefaultMode);
#pragma clang diagnostic pop
    FSEventStreamRelease(stream);
    stream = nullptr;

    CFRelease(paths_to_watch);
    CFRelease(path_cfstr);
  }

  void add_pending_event(FileEventType type, const std::string& path) {
    // Filter out directories, ignored asset types and unknown file types
    fs::path path_obj = fs::u8path(path);
    if (!path_obj.has_extension() || should_skip_asset(path_obj.extension().string())) {
      return;
    }

    // For Deleted events, process immediately (don't debounce)
    if (type == FileEventType::Deleted) {
      if (callback) {
        FileEvent event(type, path);
        callback(event);
      }
      return;
    }

    std::lock_guard<std::mutex> lock(pending_events_mutex);

    // Create or update the pending event (only for Created/Modified events)
    pending_events[path] = PendingFileEvent(type, path);
  }

  void timer_loop() {
    while (!timer_should_stop.load()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));

      std::lock_guard<std::mutex> lock(pending_events_mutex);
      auto now = std::chrono::steady_clock::now();

      for (auto it = pending_events.begin(); it != pending_events.end();) {
        auto& event = it->second;
        auto time_since_activity = std::chrono::duration_cast<std::chrono::milliseconds>(now - event.last_activity).count();

        if (event.is_active && time_since_activity >= Config::FILE_WATCHER_DEBOUNCE_MS) {
          // Process the event
          callback(FileEvent(event.original_type, event.path));

          it = pending_events.erase(it);
        }
        else {
          ++it;
        }
      }
    }
  }

  void handle_directory_moved_in(const fs::path& dir_path) {
    LOG_DEBUG("Scanning directory for moved-in contents: {}", dir_path.string());


    try {
      // Recursively scan the directory and emit Create events for all contents
      for (const auto& entry : fs::recursive_directory_iterator(dir_path)) {
        std::string path = entry.path().generic_u8string();
        add_pending_event(FileEventType::Created, path);
      }
    }
    catch (const fs::filesystem_error& e) {
      LOG_WARN("Failed to scan moved-in directory {}: {}", dir_path.string(), e.what());
    }
  }

  void emit_deletion_events_for_directory(const fs::path& dir_path) {
    LOG_DEBUG("Emitting deletion events for directory: {}", dir_path.string());

    if (!safe_assets_) {
      LOG_WARN("No assets provided for directory deletion handling");
      return;
    }

    // Use optimized O(log n) lookup to find tracked files under this path
    std::vector<fs::path> files_to_delete;

    {
      auto [lock, assets] = safe_assets_->read();
      files_to_delete = find_assets_under_directory(assets, dir_path);
    }

    // Emit deletion events for all found assets
    for (const auto& file_path : files_to_delete) {
      if (callback) {
        std::string path = file_path.generic_u8string();
        FileEvent event(FileEventType::Deleted, path);
        callback(event);
      }
    }

    LOG_DEBUG("Emitted {} deletion events for directory and assets under it", files_to_delete.size());
  }

};

// Static member definition
MacOSFileWatcher* MacOSFileWatcher::current_instance = nullptr;

// Factory function
std::unique_ptr<FileWatcherImpl> create_macos_file_watcher_impl() {
  return std::make_unique<MacOSFileWatcher>();
}
