#pragma once

// Forward declarations
class AssetDatabase;
class SearchIndex;
class EventProcessor;

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
//   Services::provide(&database, &search_index, &event_processor);
//
//   // Anywhere in the codebase:
//   Services::database().insert_asset(asset);
//   auto results = Services::search_index().search_terms(terms);
//   Services::event_processor().queue_event(event);
//
//   // In tests:
//   MockDatabase mock_db;
//   MockSearchIndex mock_idx;
//   MockEventProcessor mock_proc;
//   Services::provide(&mock_db, &mock_idx, &mock_proc);
//   // ... run tests ...
//
class Services {
public:
    // Register core services at application startup
    // Must be called before any service accessor methods are used
    static void provide(AssetDatabase* database, SearchIndex* search_index, EventProcessor* event_processor);

    // Access registered services
    // Will assert if services haven't been provided yet
    static AssetDatabase& database();
    static SearchIndex& search_index();
    static EventProcessor& event_processor();

private:
    static AssetDatabase* database_;
    static SearchIndex* search_index_;
    static EventProcessor* event_processor_;
};
