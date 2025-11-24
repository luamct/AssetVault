#include "services.h"
#include "database.h"
#include "search.h"
#include "event_processor.h"
#include "file_watcher.h"
#include "texture_manager.h"
#include "3d.h"
#include "audio_manager.h"
#include "drag_drop.h"
#include "logger.h"
#include "config.h"
#include "utils.h"
#include <cassert>
#include <vector>

// Static member initialization
AssetDatabase* Services::database_ = nullptr;
SearchIndex* Services::search_index_ = nullptr;
EventProcessor* Services::event_processor_ = nullptr;
FileWatcher* Services::file_watcher_ = nullptr;
TextureManager* Services::texture_manager_ = nullptr;
AudioManager* Services::audio_manager_ = nullptr;
DragDropManager* Services::drag_drop_manager_ = nullptr;
void Services::provide(AssetDatabase* database, SearchIndex* search_index, EventProcessor* event_processor, FileWatcher* file_watcher, TextureManager* texture_manager, AudioManager* audio_manager, DragDropManager* drag_drop_manager) {
    database_ = database;
    search_index_ = search_index;
    event_processor_ = event_processor;
    file_watcher_ = file_watcher;
    texture_manager_ = texture_manager;
    audio_manager_ = audio_manager;
    drag_drop_manager_ = drag_drop_manager;
}

bool Services::start(FileEventCallback file_event_callback, SafeAssets* safe_assets) {
    assert(database_ != nullptr && "Services not provided before start()");
    assert(search_index_ != nullptr && "Services not provided before start()");
    assert(texture_manager_ != nullptr && "Services not provided before start()");
    assert(event_processor_ != nullptr && "Services not provided before start()");
    assert(audio_manager_ != nullptr && "Services not provided before start()");
    assert(file_watcher_ != nullptr && "Services not provided before start()");

    // Initialize database
    std::string db_path = Config::get_database_path().string();
    LOG_INFO("Using database path: {}", db_path);
    if (!database_->initialize(db_path)) {
        LOG_ERROR("Failed to initialize database");
        return false;
    }

    if (!Config::initialize(database_)) {
        LOG_ERROR("Failed to initialize config");
        return false;
    }

    const std::string assets_directory = Config::assets_directory();
    if (!assets_directory.empty()) {
        LOG_INFO("Loaded assets directory from config: {}", assets_directory);
    }

    // Debug: Clean start - clear both database and thumbnails
    if (Config::DEBUG_CLEAN_START) {
        LOG_WARN("DEBUG_CLEAN_START enabled - clearing database and thumbnails...");
        database_->clear_all_assets();
        clear_all_thumbnails();
    }

    // Get all assets from database for search index
    auto db_assets = database_->get_all_assets();
    LOG_INFO("Loaded {} assets from database", db_assets.size());

    // Initialize search index from assets
    if (!search_index_->build_from_assets(db_assets)) {
        LOG_ERROR("Failed to initialize search index");
        return false;
    }

    // Initialize texture manager
    if (!texture_manager_->initialize()) {
        LOG_ERROR("Failed to initialize texture manager");
        return false;
    }

    // Initialize 3D preview system before starting any background rendering work
    if (!texture_manager_->initialize_preview_system()) {
        LOG_ERROR("Failed to initialize 3D preview system");
        return false;
    }

    // Compile and link the unified 3D shader while we're still on the main context
    if (!initialize_3d_shaders()) {
        LOG_ERROR("Failed to initialize 3D shaders");
        return false;
    }

    // Start event processor with assets directory (thumbnail thread now sees ready GL resources)
    if (!event_processor_->start(assets_directory)) {
        LOG_ERROR("Failed to start event processor");
        return false;
    }

    // Initialize audio manager (not critical)
    if (!audio_manager_->initialize()) {
        LOG_WARN("Failed to initialize audio system");
        // Not critical - continue without audio support
    }

    // Scan for changes and start file watcher if assets directory is configured
    if (!assets_directory.empty() && safe_assets != nullptr) {
        scan_for_changes(assets_directory, db_assets, *safe_assets);

        if (!file_watcher_->start(assets_directory, file_event_callback, safe_assets)) {
            LOG_ERROR("Failed to start file watcher for path: {}", assets_directory);
            return false;
        }
    }

    return true;
}

void Services::stop(SafeAssets* safe_assets) {
    // Stop services in reverse order of startup

    // Stop file watcher first to prevent new events
    file_watcher_->stop();

    // Stop event processor to finish/discard pending events
    event_processor_->stop();

    // Clear assets and data if requested (for restart scenario)
    if (safe_assets != nullptr) {
        // Clear assets from memory
        {
            auto [lock, assets] = safe_assets->write();
            assets.clear();
        }

        // Clear database
        if (!database_->clear_all_assets()) {
            LOG_WARN("Failed to clear assets table");
        }

        // Clear search index
        search_index_->clear();

        LOG_INFO("Services stopped and data cleared (restart scenario)");
    } else {
        // Final shutdown - close database connection
        database_->close();
        LOG_INFO("All services stopped (final shutdown)");
    }
}

AssetDatabase& Services::database() {
    assert(database_ != nullptr && "AssetDatabase service not provided! Call Services::provide() first.");
    return *database_;
}

SearchIndex& Services::search_index() {
    assert(search_index_ != nullptr && "SearchIndex service not provided! Call Services::provide() first.");
    return *search_index_;
}

EventProcessor& Services::event_processor() {
    assert(event_processor_ != nullptr && "EventProcessor service not provided! Call Services::provide() first.");
    return *event_processor_;
}

FileWatcher& Services::file_watcher() {
    assert(file_watcher_ != nullptr && "FileWatcher service not provided! Call Services::provide() first.");
    return *file_watcher_;
}

TextureManager& Services::texture_manager() {
    assert(texture_manager_ != nullptr && "TextureManager service not provided! Call Services::provide() first.");
    return *texture_manager_;
}

AudioManager& Services::audio_manager() {
    assert(audio_manager_ != nullptr && "AudioManager service not provided! Call Services::provide() first.");
    return *audio_manager_;
}

DragDropManager& Services::drag_drop_manager() {
    assert(drag_drop_manager_ != nullptr && "DragDropManager service not provided! Call Services::provide() first.");
    return *drag_drop_manager_;
}
