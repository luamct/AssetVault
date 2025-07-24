#pragma once

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
    constexpr float THUMBNAIL_SIZE = 180.0f;
    constexpr float GRID_SPACING = 30.0f;
    constexpr float TEXT_MARGIN = 20.0f;         // Space below thumbnail for text positioning
    constexpr float TEXT_HEIGHT = 20.0f;         // Height reserved for text
    constexpr float ICON_SCALE = 0.5f;           // Icon occupies 50% of the thumbnail area

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

    // Asset processing
    constexpr int SVG_THUMBNAIL_SIZE = 240;

    // Image scaling limits
    constexpr float MAX_THUMBNAIL_UPSCALE_FACTOR = 3.0f;  // 3x upscaling for grid thumbnails
    constexpr float MAX_PREVIEW_UPSCALE_FACTOR = 100.0f;  // High upscaling for preview panel

    // =============================================================================
    // FILE SYSTEM & MONITORING
    // =============================================================================

    // File watcher settings
    constexpr int FILE_WATCHER_DEBOUNCE_MS = 500;

    // Asset root directory
    constexpr const char* ASSET_ROOT_DIRECTORY = "assets";

    // Database settings
    constexpr const char* DATABASE_PATH = "db/assets.db";

    // =============================================================================
    // DEBUG & DEVELOPMENT
    // =============================================================================

    // Set to true to force database clearing and reindexing on startup
    constexpr bool DEBUG_FORCE_DB_CLEAR = false;

    // Font settings
    constexpr const char* FONT_PATH = "external/fonts/Roboto-Regular.ttf";
    constexpr float FONT_SIZE = 24.0f;
}
