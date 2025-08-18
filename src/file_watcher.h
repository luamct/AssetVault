#pragma once
#include <atomic>
#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

// Forward declarations for platform-specific implementations
class FileWatcherImpl;

// Event types for file system changes
enum class FileEventType { Created, Modified, Deleted, Renamed, DirectoryCreated, DirectoryDeleted };

// File event structure
struct FileEvent {
  FileEventType type;
  std::filesystem::path path;
  std::filesystem::path old_path;  // For rename events
  std::chrono::system_clock::time_point timestamp;

  FileEvent(FileEventType t, const std::filesystem::path& p, const std::filesystem::path& old = "")
      : type(t), path(p), old_path(old), timestamp(std::chrono::system_clock::now()) {}
};

// Callback type for file events
using FileEventCallback = std::function<void(const FileEvent&)>;

// Callback type for checking if an asset exists in the database
using AssetExistsCallback = std::function<bool(const std::filesystem::path&)>;

// Main file watcher class
class FileWatcher {
 public:
  FileWatcher();
  ~FileWatcher();

  // Start watching a directory
  bool start_watching(const std::string& path, FileEventCallback callback, AssetExistsCallback asset_check = nullptr);

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
  virtual bool start_watching(const std::string& path, FileEventCallback callback, AssetExistsCallback asset_check = nullptr) = 0;
  virtual void stop_watching() = 0;
  virtual bool is_watching() const = 0;
};
