#include "services.h"
#include "database.h"
#include "search.h"
#include "event_processor.h"
#include "file_watcher.h"
#include "texture_manager.h"
#include "audio_manager.h"
#include "logger.h"
#include <cassert>

// Static member initialization
AssetDatabase* Services::database_ = nullptr;
SearchIndex* Services::search_index_ = nullptr;
EventProcessor* Services::event_processor_ = nullptr;
FileWatcher* Services::file_watcher_ = nullptr;
TextureManager* Services::texture_manager_ = nullptr;
AudioManager* Services::audio_manager_ = nullptr;

void Services::provide(AssetDatabase* database, SearchIndex* search_index, EventProcessor* event_processor, FileWatcher* file_watcher, TextureManager* texture_manager, AudioManager* audio_manager) {
    database_ = database;
    search_index_ = search_index;
    event_processor_ = event_processor;
    file_watcher_ = file_watcher;
    texture_manager_ = texture_manager;
    audio_manager_ = audio_manager;
}

bool Services::start() {
    assert(texture_manager_ != nullptr && "Services not provided before start()");
    assert(event_processor_ != nullptr && "Services not provided before start()");
    assert(audio_manager_ != nullptr && "Services not provided before start()");

    if (!texture_manager_->initialize()) {
        LOG_ERROR("Failed to initialize texture manager");
        return false;
    }

    if (!event_processor_->start()) {
        LOG_ERROR("Failed to start event processor");
        return false;
    }

    if (!texture_manager_->initialize_preview_system()) {
        LOG_ERROR("Failed to initialize 3D preview system");
        return false;
    }

    if (!audio_manager_->initialize()) {
        LOG_WARN("Failed to initialize audio system");
        // Not critical - continue without audio support
    }

    return true;
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
