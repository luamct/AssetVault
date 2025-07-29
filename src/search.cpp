#include "search.h"
#include "utils.h"
#include "config.h"
#include "event_processor.h"
#include <iostream>
#include <sstream>
#include <algorithm>
#include <mutex>

// Global event processor reference (defined in main.cpp)
extern EventProcessor* g_event_processor;

bool asset_matches_search(const Asset& asset, const std::string& search_query) {
  if (search_query.empty()) {
    return true; // Show all assets when search is empty
  }

  std::string query_lower = to_lowercase(search_query);
  std::string name_lower = to_lowercase(asset.name);
  std::string extension_lower = to_lowercase(asset.extension);
  std::string path_lower = to_lowercase(asset.full_path.u8string());

  // Split search query into terms (space-separated)
  std::vector<std::string> search_terms;
  std::stringstream ss(query_lower);
  std::string search_term;
  while (ss >> search_term) {
    if (!search_term.empty()) {
      search_terms.push_back(search_term);
    }
  }

  // All terms must match (AND logic)
  for (const auto& term : search_terms) {
    bool term_matches = false;

    // Check if the term matches name, extension, or path
    if (name_lower.find(term) != std::string::npos ||
        extension_lower.find(term) != std::string::npos ||
        path_lower.find(term) != std::string::npos) {
      term_matches = true;
    }

    if (!term_matches) {
      return false; // This term doesn't match, so the asset doesn't match
    }
  }

  return true;
}

void filter_assets(SearchState& search_state, const std::vector<Asset>& assets) {
  auto start_time = std::chrono::high_resolution_clock::now();

  search_state.filtered_assets.clear();
  search_state.selected_asset_index = -1; // Clear selection when search results change

  // Reset model preview state when filtering
  search_state.model_preview_row = -1;

  size_t total_assets = 0;
  size_t filtered_count = 0;

  // Lock assets during filtering to prevent race conditions
  std::lock_guard<std::mutex> lock(g_event_processor->get_assets_mutex());

  total_assets = assets.size();
  for (const auto& asset : assets) {
    // Skip ignored asset types
    if (Config::IGNORED_ASSET_TYPES.count(asset.type) > 0) {
      continue;
    }

    if (asset_matches_search(asset, search_state.buffer)) {
      search_state.filtered_assets.push_back(asset);
      filtered_count++;
    }

    // Enforce reasonable limit to prevent UI blocking
    if (filtered_count >= Config::MAX_SEARCH_RESULTS) {
      std::cout << "Search results limited to " << Config::MAX_SEARCH_RESULTS << " items\n";
      break;
    }
  }

  // Initialize loaded range for infinite scroll
  search_state.loaded_start_index = 0;
  search_state.loaded_end_index = std::min(
    static_cast<int>(search_state.filtered_assets.size()),
    SearchState::LOAD_BATCH_SIZE
  );

  // Measure and print search time
  auto end_time = std::chrono::high_resolution_clock::now();
  auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);

  std::cout << "Search for \"" << search_state.buffer << "\" completed in " 
            << duration.count() / 1000.0 << " ms. "
            << "Filtered " << filtered_count << "/" << total_assets << " assets\n";
}