#pragma once
#include <atomic>
#include <chrono>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "asset.h"

namespace fs = std::filesystem;

// Forward declarations for platform-specific implementations
class FileWatcherImpl;

// Event types for file system changes
enum class FileEventType { Created, Deleted };

// File event structure
struct FileEvent {
  FileEventType type;
  std::string path;  // UTF-8 encoded path with normalized separators
  std::chrono::system_clock::time_point timestamp;
  int retry_count = 0;  // Number of times this event has been retried

  FileEvent(FileEventType t, const std::string& p)
      : type(t), path(p), timestamp(std::chrono::system_clock::now()), retry_count(0) {}
};

// Callback type for file events
using FileEventCallback = std::function<void(const FileEvent&)>;

// Asset map type alias
using AssetMap = std::map<std::string, Asset>;

// Main file watcher class
class FileWatcher {
 public:
  FileWatcher();
  ~FileWatcher();

  // Start watching a directory
  bool start_watching(const std::string& path, FileEventCallback callback, SafeAssets* assets = nullptr);

  // Stop watching
  void stop_watching();

  // Check if currently watching
  bool is_watching() const;

  // Get the watched path
  std::string get_watched_path() const;

  // Add file extensions to filter (optional)
  void set_file_extensions(const std::vector<std::string>& extensions);

  // Set polling interval for fallback mode (in milliseconds)
  void set_polling_interval(int milliseconds);

  static std::string file_event_type_to_string(FileEventType file_event_type);

 private:
  std::unique_ptr<FileWatcherImpl> p_impl;
  std::string watched_path;
  std::vector<std::string> file_extensions;
  int polling_interval;
  std::atomic<bool> is_watching_flag;
};

// Platform-specific implementation base class
class FileWatcherImpl {
 public:
  virtual ~FileWatcherImpl() = default;
  virtual bool start_watching(const std::string& path, FileEventCallback callback, SafeAssets* assets = nullptr) = 0;
  virtual void stop_watching() = 0;
  virtual bool is_watching() const = 0;
};
