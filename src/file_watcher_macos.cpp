#include <CoreServices/CoreServices.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <mutex>
#include <queue>
#include <thread>
#include <unordered_map>

#include "config.h"
#include "file_watcher.h"
#include "logger.h"

// Structure to track pending file events
struct PendingFileEvent {
  FileEventType original_type;
  std::filesystem::path path;
  std::filesystem::path old_path;  // For rename events
  std::chrono::steady_clock::time_point last_activity;
  bool is_active;

  PendingFileEvent() : original_type(FileEventType::Modified), is_active(false) {}

  PendingFileEvent(FileEventType type, const std::filesystem::path& p, const std::filesystem::path& old = std::filesystem::path())
      : original_type(type), path(p), old_path(old), last_activity(std::chrono::steady_clock::now()), is_active(true) {}
};

class MacOSFileWatcher : public FileWatcherImpl {
 private:
  FSEventStreamRef stream;
  std::thread watch_thread;
  std::atomic<bool> should_stop;
  std::atomic<bool> is_watching_flag;
  FileEventCallback callback;
  AssetExistsCallback asset_check_callback;
  std::string watched_path;
  
  // Timer-based event tracking
  std::unordered_map<std::filesystem::path, PendingFileEvent> pending_events;
  std::mutex pending_events_mutex;
  std::thread timer_thread;
  std::atomic<bool> timer_should_stop;
  
  // Store this instance for the callback
  static MacOSFileWatcher* current_instance;

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

  bool start_watching(const std::string& path, FileEventCallback cb, AssetExistsCallback asset_check) override {
    if (is_watching_flag.load()) {
      LOG_ERROR("Already watching a directory");
      return false;
    }

    watched_path = path;
    callback = cb;
    asset_check_callback = asset_check;

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
    char** paths = static_cast<char**>(eventPaths);

    for (size_t i = 0; i < numEvents; ++i) {
      std::string path_str(paths[i]);
      std::filesystem::path file_path(path_str);

      // Skip if it's the watched directory itself (normalize paths for comparison)
      try {
        auto normalized_file_path = std::filesystem::weakly_canonical(file_path);
        auto normalized_watched_path = std::filesystem::weakly_canonical(watcher->watched_path);
        if (normalized_file_path == normalized_watched_path) {
          continue;
        }
      } catch (const std::filesystem::filesystem_error&) {
        // If normalization fails, fall back to string comparison
        if (file_path == watcher->watched_path) {
          continue;
        }
      }

      // Determine event type based on flags
      FSEventStreamEventFlags flags = eventFlags[i];
      
      // Get relative path for logging
      std::string relative_path = file_path.string();
      if (relative_path.find(watcher->watched_path) == 0) {
        relative_path = relative_path.substr(watcher->watched_path.length());
        if (!relative_path.empty() && relative_path[0] == '/') {
          relative_path = relative_path.substr(1);
        }
      }
      
      // Debug: Log all flags for this event
      LOG_DEBUG("FSEvents: Event for '{}' with flags: 0x{:X} (Created:{} Removed:{} Modified:{} Renamed:{} IsDir:{})",
                relative_path, flags,
                (flags & kFSEventStreamEventFlagItemCreated) ? "Y" : "N",
                (flags & kFSEventStreamEventFlagItemRemoved) ? "Y" : "N", 
                (flags & kFSEventStreamEventFlagItemModified) ? "Y" : "N",
                (flags & kFSEventStreamEventFlagItemRenamed) ? "Y" : "N",
                (flags & kFSEventStreamEventFlagItemIsDir) ? "Y" : "N");
      
      // Check Renamed flag first as it can be combined with other flags
      if (flags & kFSEventStreamEventFlagItemRenamed) {
        // Handle rename events based solely on asset database
        // A rename event means either:
        // 1. File moved FROM this path (if asset exists in DB) -> Deleted
        // 2. File moved TO this path (if asset doesn't exist in DB) -> Created
        
        if (watcher->asset_check_callback) {
          bool is_tracked = watcher->asset_check_callback(file_path);
          LOG_DEBUG("FSEvents: Rename event for '{}', is_tracked={}", relative_path, is_tracked);
          
          if (is_tracked) {
            // Asset was tracked at this path - file moved away/deleted
            LOG_DEBUG("FSEvents: Treating as Deleted (asset was tracked)", relative_path);
            watcher->add_pending_event(FileEventType::Deleted, file_path);
          } else {
            // Asset wasn't tracked at this path - file moved here
            LOG_DEBUG("FSEvents: Treating as Created (asset not tracked)", relative_path);
            watcher->add_pending_event(FileEventType::Created, file_path);
          }
        } else {
          // No asset check callback provided - fall back to simple rename event
          LOG_DEBUG("FSEvents: Rename event for '{}', no asset check available", relative_path);
          watcher->add_pending_event(FileEventType::Renamed, file_path);
        }
      } else if (flags & kFSEventStreamEventFlagItemCreated) {
        if (flags & kFSEventStreamEventFlagItemIsDir) {
          watcher->add_pending_event(FileEventType::DirectoryCreated, file_path);
        } else {
          watcher->add_pending_event(FileEventType::Created, file_path);
        }
      } else if (flags & kFSEventStreamEventFlagItemRemoved) {
        if (flags & kFSEventStreamEventFlagItemIsDir) {
          watcher->add_pending_event(FileEventType::DirectoryDeleted, file_path);
        } else {
          watcher->add_pending_event(FileEventType::Deleted, file_path);
        }
      } else if (flags & kFSEventStreamEventFlagItemModified) {
        if (!(flags & kFSEventStreamEventFlagItemIsDir)) {
          watcher->add_pending_event(FileEventType::Modified, file_path);
        }
      } else {
        // Check if file exists - if not, it's been deleted even without explicit flags
        if (!std::filesystem::exists(file_path)) {
          LOG_DEBUG("FSEvents: File '{}' no longer exists (no explicit flags), treating as Deleted", relative_path);
          watcher->add_pending_event(FileEventType::Deleted, file_path);
        }
      }
    }
  }

  void watch_loop() {
    // Create CFString for the path
    CFStringRef path_cfstr = CFStringCreateWithCString(nullptr, watched_path.c_str(), kCFStringEncodingUTF8);
    CFArrayRef paths_to_watch = CFArrayCreate(nullptr, (const void**)&path_cfstr, 1, &kCFTypeArrayCallBacks);

    // Create the event stream
    FSEventStreamContext context = {0, this, nullptr, nullptr, nullptr};
    stream = FSEventStreamCreate(
        nullptr,
        &MacOSFileWatcher::fsevents_callback,
        &context,
        paths_to_watch,
        kFSEventStreamEventIdSinceNow,
        0.1,  // Latency in seconds
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

  void add_pending_event(FileEventType type, const std::filesystem::path& path, const std::filesystem::path& old_path = std::filesystem::path()) {
    // For Deleted and Renamed events, process immediately (don't debounce)
    if (type == FileEventType::Deleted || type == FileEventType::Renamed || 
        type == FileEventType::DirectoryDeleted) {
      // Get relative path for logging
      std::string relative_path = path.string();
      if (relative_path.find(watched_path) == 0) {
        relative_path = relative_path.substr(watched_path.length());
        if (!relative_path.empty() && relative_path[0] == '/') {
          relative_path = relative_path.substr(1);
        }
      }
      LOG_DEBUG("Processing {} event IMMEDIATELY for '{}'", 
                (type == FileEventType::Deleted) ? "Deleted" :
                (type == FileEventType::Renamed) ? "Renamed" :
                "DirectoryDeleted", relative_path);
      if (callback) {
        FileEvent event(type, path, old_path);
        callback(event);
      }
      return;
    }
    
    std::lock_guard<std::mutex> lock(pending_events_mutex);
    
    // Check if it's an asset file (has extension)
    if (!path.has_extension() && type != FileEventType::DirectoryCreated) {
      return;
    }

    // Create or update the pending event (only for Created/Modified events)
    pending_events[path] = PendingFileEvent(type, path, old_path);
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
          if (event.original_type == FileEventType::Renamed) {
            callback(FileEvent(event.original_type, event.path, event.old_path));
          } else {
            callback(FileEvent(event.original_type, event.path));
          }
          
          it = pending_events.erase(it);
        } else {
          ++it;
        }
      }
    }
  }
};

// Static member definition
MacOSFileWatcher* MacOSFileWatcher::current_instance = nullptr;

// Factory function
std::unique_ptr<FileWatcherImpl> create_macos_file_watcher_impl() {
  return std::make_unique<MacOSFileWatcher>();
}