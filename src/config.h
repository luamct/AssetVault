#pragma once

#include <unordered_set>
#include <filesystem>
#include <string>
#include <cstdlib>
#include "asset.h"

namespace Config {
    // =============================================================================
    // WINDOW & UI LAYOUT
    // =============================================================================

    // Main window dimensions
    constexpr int WINDOW_WIDTH = 1960;
    constexpr int WINDOW_HEIGHT = 1080;

    // Search box dimensions
    constexpr float SEARCH_BOX_WIDTH = 375.0f;
    constexpr float SEARCH_BOX_HEIGHT = 60.0f;

    // Grid layout
    constexpr float THUMBNAIL_SIZE = 240.0f;
    constexpr float GRID_SPACING = 30.0f;
    constexpr float TEXT_MARGIN = 20.0f;         // Space below thumbnail for text positioning
    constexpr float TEXT_HEIGHT = 20.0f;         // Height reserved for text
    constexpr float ICON_SCALE = 0.5f;           // Icon occupies 50% of the thumbnail area

    // 3D model thumbnail generation
    constexpr int MODEL_THUMBNAIL_SIZE = 180;    // Size for generated 3D model thumbnails
    constexpr int MAX_TEXTURE_RETRY_ATTEMPTS = 50; // Max retries for texture loading before giving up

    // 3D preview controls
    constexpr float PREVIEW_3D_ROTATION_SENSITIVITY = 0.167f;  // Degrees per pixel (was 0.5, now 1/3 of that)
    constexpr float PREVIEW_3D_ZOOM_FACTOR = 1.1f;            // Zoom multiplier per scroll wheel notch

    // Preview panel layout
    constexpr float PREVIEW_RIGHT_MARGIN = 40.0f;     // Margin from window right edge
    constexpr float PREVIEW_INTERNAL_PADDING = 30.0f; // Internal padding within preview panel

    // =============================================================================
    // PERFORMANCE & PROCESSING
    // =============================================================================

    // Event processing
    constexpr size_t EVENT_PROCESSOR_BATCH_SIZE = 100;

    // Search & UI limits
    constexpr size_t MAX_SEARCH_RESULTS = 1000; // Limit results to prevent UI blocking
    constexpr int SEARCH_DEBOUNCE_MS = 250;      // Delay before executing search (milliseconds)

    // Asset types to exclude from search results (O(1) lookup)
    inline const std::unordered_set<AssetType> IGNORED_ASSET_TYPES = {
        AssetType::Auxiliary,  // System/helper files (.mtl, .log, .cache, .tmp, .bak, etc.)
        AssetType::Unknown,    // Unrecognized files (.DS_Store, .gitignore, README, etc.)
        AssetType::Directory,  // Folders
        AssetType::Document    // Documents (.txt, .md, .pdf, .doc, etc.)
    };

    // Asset processing
    constexpr int SVG_THUMBNAIL_SIZE = 240;

    // Image scaling limits
    constexpr float MAX_THUMBNAIL_UPSCALE_FACTOR = 3.0f;  // 3x upscaling for grid thumbnails
    constexpr float MAX_PREVIEW_UPSCALE_FACTOR = 100.0f;  // High upscaling for preview panel

    // =============================================================================
    // FILE SYSTEM & MONITORING
    // =============================================================================

    // File watcher settings
    constexpr int FILE_WATCHER_DEBOUNCE_MS = 50; // Time window to coalesce related events from the same files

    // Asset root directory
#ifdef _WIN32
    constexpr const char* ASSET_ROOT_DIRECTORY = "D:/GameDev/AssetInventory/assets";
#else
    constexpr const char* ASSET_ROOT_DIRECTORY = "/Users/luamct/GameDev/AssetInventory/assets";
#endif

    // Database settings
    constexpr const char* DATABASE_PATH = "db/assets.db";

    // Thumbnail settings
    constexpr const char* THUMBNAIL_DIRECTORY = "thumbnails";

    // =============================================================================
    // DEBUG & DEVELOPMENT
    // =============================================================================

    // Set to true to force database clearing and reindexing on startup
    constexpr bool DEBUG_FORCE_DB_CLEAR = false;

    // Set to true to delete all thumbnails on startup
    constexpr bool DEBUG_FORCE_THUMBNAIL_CLEAR = true;

    // Font settings
    constexpr const char* FONT_PATH = "external/fonts/Roboto-Regular.ttf";
    constexpr float FONT_SIZE = 24.0f;

    // =============================================================================
    // CROSS-PLATFORM PATHS
    // =============================================================================

    // Get the proper cache directory for the current platform
    inline std::filesystem::path get_cache_directory() {
#ifdef _WIN32
        // Windows: %LOCALAPPDATA%\AssetInventory
        const char* localappdata = std::getenv("LOCALAPPDATA");
        if (localappdata) {
            return std::filesystem::path(localappdata) / "AssetInventory";
        } else {
            // Fallback to current directory if env var not found
            return "cache";
        }
#elif __APPLE__
        // macOS: ~/Library/Caches/AssetInventory
        const char* home = std::getenv("HOME");
        if (home) {
            return std::filesystem::path(home) / "Library" / "Caches" / "AssetInventory";
        } else {
            return "cache";
        }
#else
        // Linux: $XDG_CACHE_HOME/AssetInventory or ~/.cache/AssetInventory
        const char* xdg_cache = std::getenv("XDG_CACHE_HOME");
        if (xdg_cache) {
            return std::filesystem::path(xdg_cache) / "AssetInventory";
        } else {
            const char* home = std::getenv("HOME");
            if (home) {
                return std::filesystem::path(home) / ".cache" / "AssetInventory";
            } else {
                return "cache";
            }
        }
#endif
    }

    // Get the proper data directory for the current platform
    inline std::filesystem::path get_data_directory() {
#ifdef _WIN32
        // Windows: %LOCALAPPDATA%\AssetInventory (same as cache for local apps)
        const char* localappdata = std::getenv("LOCALAPPDATA");
        if (localappdata) {
            return std::filesystem::path(localappdata) / "AssetInventory";
        } else {
            return "data";
        }
#elif __APPLE__
        // macOS: ~/Library/Application Support/AssetInventory
        const char* home = std::getenv("HOME");
        if (home) {
            return std::filesystem::path(home) / "Library" / "Application Support" / "AssetInventory";
        } else {
            return "data";
        }
#else
        // Linux: $XDG_DATA_HOME/AssetInventory or ~/.local/share/AssetInventory
        const char* xdg_data = std::getenv("XDG_DATA_HOME");
        if (xdg_data) {
            return std::filesystem::path(xdg_data) / "AssetInventory";
        } else {
            const char* home = std::getenv("HOME");
            if (home) {
                return std::filesystem::path(home) / ".local" / "share" / "AssetInventory";
            } else {
                return "data";
            }
        }
#endif
    }

    // Get the thumbnail directory (in cache)
    inline std::filesystem::path get_thumbnail_directory() {
        return get_cache_directory() / THUMBNAIL_DIRECTORY;
    }

    // Get the database path (in data directory)
    inline std::filesystem::path get_database_path() {
        return get_data_directory() / "assets.db";
    }

    // Create application directories if necessary
    inline void initialize_directories() {
        // Create cache directory for thumbnails
        std::filesystem::path cache_dir = get_cache_directory();
        if (!std::filesystem::exists(cache_dir)) {
            std::filesystem::create_directories(cache_dir);
        }
        
        // Create thumbnail directory
        std::filesystem::path thumbnail_dir = get_thumbnail_directory();
        if (!std::filesystem::exists(thumbnail_dir)) {
            std::filesystem::create_directories(thumbnail_dir);
        }
        
        // Create data directory for database
        std::filesystem::path data_dir = get_data_directory();
        if (!std::filesystem::exists(data_dir)) {
            std::filesystem::create_directories(data_dir);
        }
    }
}
