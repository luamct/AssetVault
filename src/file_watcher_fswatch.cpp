#include <atomic>
#include <chrono>
#include <filesystem>
#include <mutex>
#include <thread>
#include <unordered_map>

#include "config.h"
#include "file_watcher.h"
#include "logger.h"

// fswatch includes - using C++ API
#include <libfswatch/c++/monitor.hpp>
#include <libfswatch/c++/monitor_factory.hpp>
#include <libfswatch/c++/event.hpp>
#include <libfswatch/c/cevent.h>
#include <libfswatch/c/cmonitor.h>

// Structure to track pending file events (copied from platform implementations)
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

class FSWatchFileWatcher : public FileWatcherImpl {
private:
  // fswatch integration - using C++ API
  fsw::monitor* monitor_;
  std::thread watch_thread_;
  std::atomic<bool> should_stop_;
  std::atomic<bool> is_watching_flag_;
  FileEventCallback callback_;
  std::string watched_path_;

  // Integrated debouncing (same logic as platform implementations)
  std::unordered_map<std::filesystem::path, PendingFileEvent> pending_events_;
  std::mutex pending_events_mutex_;
  std::thread timer_thread_;
  std::atomic<bool> timer_should_stop_;

  // Static instance for callback
  static FSWatchFileWatcher* current_instance_;
  
  // Event priority system - higher values take precedence
  static int get_event_priority(FileEventType type) {
    switch (type) {
      case FileEventType::Created:
      case FileEventType::DirectoryCreated:
        return 3;  // Highest priority - creation events are most important
      case FileEventType::Deleted:
      case FileEventType::DirectoryDeleted:
      case FileEventType::Renamed:
        return 2;  // High priority - structural changes
      case FileEventType::Modified:
        return 1;  // Lowest priority - content changes
      default:
        return 0;
    }
  }

public:
  FSWatchFileWatcher()
      : monitor_(nullptr),
        should_stop_(false),
        is_watching_flag_(false),
        timer_should_stop_(false) {
    current_instance_ = this;
  }

  ~FSWatchFileWatcher() { 
    stop_watching();
    if (current_instance_ == this) {
      current_instance_ = nullptr;
    }
  }

  bool start_watching(const std::string& path, FileEventCallback cb, AssetExistsCallback asset_check) override {
    if (is_watching_flag_.load()) {
      LOG_ERROR("Already watching a directory");
      return false;
    }

    watched_path_ = path;
    callback_ = cb;

    // Create fswatch monitor using C++ API
    try {
      std::vector<std::string> paths = {path};
      monitor_ = fsw::monitor_factory::create_monitor(
        static_cast<fsw_monitor_type>(system_default_monitor_type), 
        paths, 
        fswatch_callback,
        nullptr
      );

      if (!monitor_) {
        LOG_ERROR("Failed to create fswatch monitor");
        return false;
      }

      // Configure monitor for minimal latency (10ms instead of default 1000ms)
      monitor_->set_latency(0.01);
      
      // On macOS, enable NoDefer flag for immediate event delivery
      // This makes events arrive as soon as latency expires, not batched
      monitor_->set_property("darwin.eventStream.noDefer", "true");
      
      LOG_DEBUG("fswatch: Configured with 10ms latency and immediate delivery");

      should_stop_ = false;
      timer_should_stop_ = false;
      is_watching_flag_ = true;

      // Start watching thread
      watch_thread_ = std::thread(&FSWatchFileWatcher::watch_loop, this);

      // Start timer thread for processing pending events
      timer_thread_ = std::thread(&FSWatchFileWatcher::timer_loop, this);

      LOG_INFO("Started watching directory: {}", path);
      return true;
    }
    catch (const std::exception& e) {
      LOG_ERROR("Failed to start fswatch monitor: {}", e.what());
      return false;
    }
  }

  void stop_watching() override {
    if (!is_watching_flag_.load()) {
      return;
    }

    // Set stop flags first
    should_stop_ = true;
    timer_should_stop_ = true;
    is_watching_flag_ = false;

    // Stop the monitor - this should unblock the watch thread
    if (monitor_) {
      try {
        monitor_->stop();
      }
      catch (const std::exception& e) {
        // Ignore errors during shutdown - the mutex error is expected on macOS
        LOG_DEBUG("fswatch monitor stop (expected on macOS): {}", e.what());
      }
    }

    // Wait for threads to finish
    if (watch_thread_.joinable()) {
      watch_thread_.join();
    }

    if (timer_thread_.joinable()) {
      timer_thread_.join();
    }

    // Clean up monitor after threads are done
    if (monitor_) {
      delete monitor_;
      monitor_ = nullptr;
    }

    // Clear pending events
    {
      std::lock_guard<std::mutex> lock(pending_events_mutex_);
      pending_events_.clear();
    }

    LOG_INFO("Stopped watching directory: {}", watched_path_);
  }

  // Static callback function for fswatch
  static void fswatch_callback(const std::vector<fsw::event>& events, void*) {
    if (current_instance_) {
      current_instance_->process_fswatch_events(events);
    }
  }

  bool is_watching() const override { 
    return is_watching_flag_.load(); 
  }

private:
  void watch_loop() {
    try {
      if (monitor_ && !should_stop_.load()) {
        monitor_->start();
      }
    }
    catch (const std::exception& e) {
      // Only log as error if we're not shutting down
      if (!should_stop_.load()) {
        LOG_ERROR("fswatch monitor error: {}", e.what());
        is_watching_flag_ = false;
      }
    }
  }

  void process_fswatch_events(const std::vector<fsw::event>& events) {
    // Don't process events if we're shutting down
    if (should_stop_.load()) {
      return;
    }
    
    LOG_DEBUG("fswatch: Received {} events", events.size());
    
    for (const auto& evt : events) {
      std::filesystem::path file_path(evt.get_path());
      
      // Skip if it's the watched directory itself
      if (file_path == watched_path_) {
        LOG_DEBUG("fswatch: Skipping watched directory itself: {}", file_path.string());
        continue;
      }

      // Convert fswatch event flags to our FileEventType
      FileEventType event_type = FileEventType::Modified;  // Default
      
      auto flags = evt.get_flags();
      
      // Debug: Print all flags for this event
      std::string flag_names;
      for (const auto& flag : flags) {
        if (!flag_names.empty()) flag_names += ", ";
        switch (flag) {
          case NoOp: flag_names += "NoOp"; break;
          case PlatformSpecific: flag_names += "PlatformSpecific"; break;
          case Created: flag_names += "Created"; break;
          case Updated: flag_names += "Updated"; break;
          case Removed: flag_names += "Removed"; break;
          case Renamed: flag_names += "Renamed"; break;
          case OwnerModified: flag_names += "OwnerModified"; break;
          case AttributeModified: flag_names += "AttributeModified"; break;
          case MovedFrom: flag_names += "MovedFrom"; break;
          case MovedTo: flag_names += "MovedTo"; break;
          case IsFile: flag_names += "IsFile"; break;
          case IsDir: flag_names += "IsDir"; break;
          case IsSymLink: flag_names += "IsSymLink"; break;
          case Link: flag_names += "Link"; break;
          case Overflow: flag_names += "Overflow"; break;
          case CloseWrite: flag_names += "CloseWrite"; break;
          default: flag_names += "Unknown(" + std::to_string(flag) + ")"; break;
        }
      }
      LOG_DEBUG("fswatch: Event for '{}' with flags: [{}]", file_path.string(), flag_names);
      
      // Check for specific event types (using correct enum)
      bool is_created = false;
      bool is_removed = false;
      bool is_renamed = false;
      bool is_modified = false;
      bool is_directory = false;
      
      for (const auto& flag : flags) {
        switch (flag) {
          case Created:
            is_created = true;
            break;
          case Removed:
            is_removed = true;
            break;
          case Renamed:
            is_renamed = true;
            break;
          case Updated:
          case AttributeModified:
            is_modified = true;
            break;
          case IsDir:
            is_directory = true;
            break;
          default:
            // Other flags we don't specifically handle
            break;
        }
      }
      
      // Determine event type based on flags
      if (is_created) {
        event_type = is_directory ? FileEventType::DirectoryCreated : FileEventType::Created;
        LOG_INFO("FSWATCH TEST: Emitting {} event for '{}'", is_directory ? "DirectoryCreated" : "Created", file_path.string());
      } else if (is_removed) {
        event_type = is_directory ? FileEventType::DirectoryDeleted : FileEventType::Deleted;
        LOG_INFO("FSWATCH TEST: Emitting {} event for '{}'", is_directory ? "DirectoryDeleted" : "Deleted", file_path.string());
      } else if (is_renamed) {
        event_type = FileEventType::Renamed;
        LOG_INFO("FSWATCH TEST: Emitting Renamed event for '{}'", file_path.string());
      } else if (is_modified) {
        event_type = FileEventType::Modified;
        LOG_INFO("FSWATCH TEST: Emitting Modified event for '{}'", file_path.string());
      }
      
      const char* event_type_name = 
        (event_type == FileEventType::Created) ? "Created" :
        (event_type == FileEventType::Modified) ? "Modified" :
        (event_type == FileEventType::Deleted) ? "Deleted" :
        (event_type == FileEventType::Renamed) ? "Renamed" :
        (event_type == FileEventType::DirectoryCreated) ? "DirectoryCreated" :
        (event_type == FileEventType::DirectoryDeleted) ? "DirectoryDeleted" : "Unknown";
        
      LOG_DEBUG("fswatch: Determined event type '{}' for '{}'", event_type_name, file_path.string());
      
      // Process the event (will be debounced for Created/Modified)
      add_pending_event(event_type, file_path);
    }
  }

  void add_pending_event(FileEventType type, const std::filesystem::path& path, const std::filesystem::path& old_path = std::filesystem::path()) {
    std::lock_guard<std::mutex> lock(pending_events_mutex_);
    
    const char* type_name = 
      (type == FileEventType::Created) ? "Created" :
      (type == FileEventType::Modified) ? "Modified" :
      (type == FileEventType::Deleted) ? "Deleted" :
      (type == FileEventType::Renamed) ? "Renamed" :
      (type == FileEventType::DirectoryCreated) ? "DirectoryCreated" :
      (type == FileEventType::DirectoryDeleted) ? "DirectoryDeleted" : "Unknown...";
    
    // Keep renames as renames - let EventProcessor handle the logic
    if (type == FileEventType::Renamed) {
      LOG_DEBUG("fswatch: Renamed event for '{}'", path.string());
    }
    
    // For Deleted and Renamed, process immediately (same logic as platform implementations)
    if (type == FileEventType::Deleted || type == FileEventType::Renamed || 
        type == FileEventType::DirectoryDeleted) {
      LOG_DEBUG("fswatch: Processing {} event IMMEDIATELY for '{}'", type_name, path.string());
      if (callback_) {
        FileEvent event(type, path, old_path);
        callback_(event);
        LOG_DEBUG("fswatch: Immediate {} event callback completed for '{}'", type_name, path.string());
      } else {
        LOG_ERROR("fswatch: No callback available for immediate {} event '{}'", type_name, path.string());
      }
      return;
    }

    // For files without extension (unless it's a directory event), skip
    if (!path.has_extension() && type != FileEventType::DirectoryCreated) {
      LOG_DEBUG("fswatch: Skipping {} event for '{}' (no extension)", type_name, path.string());
      return;
    }

    // Create or update the pending event
    auto it = pending_events_.find(path);
    if (it == pending_events_.end()) {
      // First event for this file, store all attributes
      LOG_DEBUG("fswatch: Adding new DEBOUNCED {} event for '{}' (will process in {}ms)", 
                type_name, path.string(), Config::FILE_WATCHER_DEBOUNCE_MS);
      pending_events_[path] = PendingFileEvent(type, path, old_path);
    } else {
      // Check event priority - higher priority events should override lower priority ones
      int current_priority = get_event_priority(it->second.original_type);
      int new_priority = get_event_priority(type);
      
      if (new_priority > current_priority) {
        // New event has higher priority, update the event type
        LOG_DEBUG("fswatch: Upgrading DEBOUNCED event for '{}' from {} to {} (higher priority, resetting {}ms timer)", 
                  path.string(), 
                  (it->second.original_type == FileEventType::Created) ? "Created" :
                  (it->second.original_type == FileEventType::Modified) ? "Modified" :
                  (it->second.original_type == FileEventType::DirectoryCreated) ? "DirectoryCreated" : "Unknown",
                  type_name, Config::FILE_WATCHER_DEBOUNCE_MS);
        it->second.original_type = type;
        it->second.old_path = old_path;
      } else {
        LOG_DEBUG("fswatch: Updating existing DEBOUNCED {} event for '{}' (resetting {}ms timer, keeping same priority)", 
                  type_name, path.string(), Config::FILE_WATCHER_DEBOUNCE_MS);
      }
      
      // Always update timing and active status
      it->second.last_activity = std::chrono::steady_clock::now();
      it->second.is_active = true;
    }
  }

  void timer_loop() {
    while (!timer_should_stop_.load()) {
      auto now = std::chrono::steady_clock::now();
      std::vector<std::pair<std::filesystem::path, PendingFileEvent>> events_to_process;

      {
        std::lock_guard<std::mutex> lock(pending_events_mutex_);

        for (auto it = pending_events_.begin(); it != pending_events_.end();) {
          auto& pending = it->second;

          if (!pending.is_active) {
            // Event has been cancelled (e.g., file deleted)
            it = pending_events_.erase(it);
            continue;
          }

          auto time_since_activity =
              std::chrono::duration_cast<std::chrono::milliseconds>(now - pending.last_activity).count();

          if (time_since_activity >= Config::FILE_WATCHER_DEBOUNCE_MS) {
            // Event has timed out, store it for processing
            events_to_process.push_back({it->first, pending});
            it = pending_events_.erase(it);
          } else {
            ++it;
          }
        }
      }

      // Process timed-out events
      for (const auto& [path, pending] : events_to_process) {
        if (callback_) {
          // Determine the final event type based on the original event
          FileEventType final_type;
          if (pending.original_type == FileEventType::Created ||
              pending.original_type == FileEventType::DirectoryCreated) {
            final_type = pending.original_type;
          } else {
            final_type = FileEventType::Modified;
          }

          const char* final_type_name = 
            (final_type == FileEventType::Created) ? "Created" :
            (final_type == FileEventType::Modified) ? "Modified" :
            (final_type == FileEventType::DirectoryCreated) ? "DirectoryCreated" : "Unknown";

          LOG_DEBUG("fswatch: Processing DEBOUNCED {} event for '{}' (timer expired)", final_type_name, path.string());
          FileEvent event(final_type, path, pending.old_path);
          callback_(event);
          LOG_DEBUG("fswatch: Debounced {} event callback completed for '{}'", final_type_name, path.string());
        }
      }

      // Sleep for a short interval before checking again
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
  }
};

// Static member definition
FSWatchFileWatcher* FSWatchFileWatcher::current_instance_ = nullptr;

// Factory function for fswatch implementation
std::unique_ptr<FileWatcherImpl> create_fswatch_file_watcher_impl() { 
  return std::make_unique<FSWatchFileWatcher>(); 
}