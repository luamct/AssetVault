#include "file_watcher.h"
#include "logger.h"

// Platform-specific factory functions
#ifdef _WIN32
std::unique_ptr<FileWatcherImpl> create_windows_file_watcher_impl();
#elif defined(__APPLE__)
std::unique_ptr<FileWatcherImpl> create_platform_file_watcher();
#else
// For non-Windows/non-macOS platforms, we'll need to implement a different solution
#error "FileWatcher only supports Windows and macOS at this time"
#endif

std::unique_ptr<FileWatcherImpl> create_file_watcher_impl() {
#ifdef _WIN32
  LOG_INFO("Using native Windows file watcher");
  return create_windows_file_watcher_impl();
#elif defined(__APPLE__)
  LOG_INFO("Using native macOS file watcher (FSEvents)");
  return create_platform_file_watcher();
#else
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

bool FileWatcher::start_watching(const std::string& path, FileEventCallback callback) {
  if (!p_impl) {
    LOG_ERROR("No file watcher implementation available");
    return false;
  }

  watched_path = path;
  is_watching_flag = true;

  return p_impl->start_watching(path, callback);
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
