#pragma once

// Forward declarations
class AssetDatabase;
class SearchIndex;
class EventProcessor;
class FileWatcher;
class TextureManager;

// Services - Central registry for application-wide singleton services
//
// This class provides global access to core services that are truly application-singletons.
// Services must be initialized via provide() before use, typically during application startup.
//
// Thread Safety: All registered services are expected to be thread-safe.
//                The Services class itself is thread-safe for reads after initialization.
//
// Lifecycle: Services are initialized in run.cpp and live for the application's lifetime.
//
// Testing: Use provide() to inject mock implementations.
//
// Example usage:
//   // In run.cpp (application startup):
//   Services::provide(&database, &search_index, &event_processor, &file_watcher, &texture_manager);
//
//   // Anywhere in the codebase:
//   Services::database().insert_asset(asset);
//   auto results = Services::search_index().search_terms(terms);
//   Services::event_processor().queue_event(event);
//   Services::file_watcher().start_watching(path, callback);
//   Services::texture_manager().load_texture(path);
//
//   // In tests:
//   MockDatabase mock_db;
//   MockSearchIndex mock_idx;
//   MockEventProcessor mock_proc;
//   MockFileWatcher mock_watcher;
//   MockTextureManager mock_tex;
//   Services::provide(&mock_db, &mock_idx, &mock_proc, &mock_watcher, &mock_tex);
//   // ... run tests ...
//
class Services {
public:
    // Register core services at application startup
    // Must be called before any service accessor methods are used
    static void provide(AssetDatabase* database, SearchIndex* search_index, EventProcessor* event_processor, FileWatcher* file_watcher, TextureManager* texture_manager);

    // Access registered services
    // Will assert if services haven't been provided yet
    static AssetDatabase& database();
    static SearchIndex& search_index();
    static EventProcessor& event_processor();
    static FileWatcher& file_watcher();
    static TextureManager& texture_manager();

private:
    static AssetDatabase* database_;
    static SearchIndex* search_index_;
    static EventProcessor* event_processor_;
    static FileWatcher* file_watcher_;
    static TextureManager* texture_manager_;
};
