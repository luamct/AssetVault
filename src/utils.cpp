#include "utils.h"

#include <algorithm>
#include <cstdio>
#include <iomanip>
#include <sstream>
#include <filesystem> // Added for std::filesystem::current_path()

// Function to truncate filename to specified length with ellipsis
std::string truncate_filename(const std::string& filename, size_t max_length) {
  if (filename.length() <= max_length) {
    return filename;
  }
  return filename.substr(0, max_length - 3) + "...";
}

// Function to convert string to lowercase for case-insensitive search
std::string to_lowercase(const std::string& str) {
  std::string result = str;
  std::transform(result.begin(), result.end(), result.begin(), ::tolower);
  return result;
}

// Function to format path for display (remove everything before first / and convert backslashes)
std::string format_display_path(const std::string& full_path) {
  std::string display_path = full_path;

  // Remove working directory prefix if present
  std::string wd = std::filesystem::current_path().string();
  if (display_path.length() > wd.length() && display_path.substr(0, wd.length()) == wd) {
    display_path = display_path.substr(wd.length());
    // Remove leading slash or backslash
    if (!display_path.empty() && (display_path[0] == '/' || display_path[0] == '\\')) {
      display_path = display_path.substr(1);
    }
  }

  // Make path wrappable by adding spaces around slashes
  size_t pos = 0;
  while ((pos = display_path.find('/', pos)) != std::string::npos) {
    display_path.replace(pos, 1, " / ");
    pos += 3; // Move past the " / "
  }

  return display_path;
}

std::string format_file_size(uint64_t size_bytes) {
  if (size_bytes >= 1024 * 1024) {
    // Convert to MB
    double size_mb = static_cast<double>(size_bytes) / (1024.0 * 1024.0);
    char buffer[32];
    snprintf(buffer, sizeof(buffer), "%.1f MB", size_mb);
    return std::string(buffer);
  } else if (size_bytes >= 1024) {
    // Convert to KB
    double size_kb = static_cast<double>(size_bytes) / 1024.0;
    char buffer[32];
    snprintf(buffer, sizeof(buffer), "%.1f KB", size_kb);
    return std::string(buffer);
  } else {
    // Keep as bytes
    return std::to_string(size_bytes) + " bytes";
  }
}
