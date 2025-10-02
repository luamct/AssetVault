#include "services.h"
#include "database.h"
#include "search.h"
#include "event_processor.h"
#include <cassert>

// Static member initialization
AssetDatabase* Services::database_ = nullptr;
SearchIndex* Services::search_index_ = nullptr;
EventProcessor* Services::event_processor_ = nullptr;

void Services::provide(AssetDatabase* database, SearchIndex* search_index, EventProcessor* event_processor) {
    database_ = database;
    search_index_ = search_index;
    event_processor_ = event_processor;
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
