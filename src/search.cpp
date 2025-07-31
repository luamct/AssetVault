#include "search.h"
#include "utils.h"
#include "config.h"
#include "event_processor.h"
#include "logger.h"
#include "asset.h"
#include <sstream>
#include <algorithm>
#include <mutex>
#include <regex>


// Parse search string to extract type filter and text query
SearchQuery parse_search_query(const std::string& search_string, 
                              const std::vector<AssetType>& ui_type_filters) {
  SearchQuery query;

  LOG_DEBUG("Parsing search query: '{}'", search_string);

  // If UI filters are provided, use them directly
  if (!ui_type_filters.empty()) {
    query.type_filters = ui_type_filters;
    LOG_DEBUG("Using {} UI type filters", ui_type_filters.size());
  }

  // Regex to match type=<name> pattern (case insensitive)
  // Now supports comma-separated values like type=2d,3d or type=2d, 3d
  // Use non-greedy matching to avoid consuming text after the type list
  std::regex type_regex(R"(type\s*=\s*([^\s]+(?:\s*,\s*[^\s]+)*))", std::regex::icase);
  std::smatch match;

  std::string remaining = search_string;

  // Look for type filter in query string only if no UI filters
  if (ui_type_filters.empty() && std::regex_search(remaining, match, type_regex)) {
    std::string types_str = match[1];

    // Split comma-separated types
    std::stringstream ss(types_str);
    std::string single_type;

    while (std::getline(ss, single_type, ',')) {
      // Trim whitespace before and after
      size_t first = single_type.find_first_not_of(" \t\n\r");
      size_t last = single_type.find_last_not_of(" \t\n\r");

      if (first != std::string::npos && last != std::string::npos) {
        single_type = single_type.substr(first, last - first + 1);
      }
      else {
        continue; // Skip empty strings
      }

      // Convert to lowercase
      std::transform(single_type.begin(), single_type.end(), single_type.begin(),
        [](unsigned char c) { return std::tolower(c); });

      // Use the existing function (expects lowercase input)
      AssetType type = get_asset_type_from_string(single_type);

      if (type != AssetType::Unknown) {
        query.type_filters.push_back(type);
      }
    }

    // Remove the type filter from the string to get the text query
    remaining = match.prefix().str() + match.suffix().str();
  }

  // Trim whitespace from remaining text query
  size_t first = remaining.find_first_not_of(" \t\n\r");
  if (first == std::string::npos) {
    remaining.clear();
  }
  else {
    size_t last = remaining.find_last_not_of(" \t\n\r");
    remaining = remaining.substr(first, last - first + 1);
  }

  query.text_query = remaining;

  LOG_DEBUG("Final parsed query - Text: '{}', Type filters count: {}", query.text_query, query.type_filters.size());

  return query;
}

bool asset_matches_search(const Asset& asset, const SearchQuery& query) {
  // Check type filters first (OR condition - asset must match at least one type)
  if (!query.type_filters.empty()) {
    bool type_matches = false;
    for (const auto& filter_type : query.type_filters) {
      if (asset.type == filter_type) {
        type_matches = true;
        break;
      }
    }
    if (!type_matches) {
      return false;
    }
  }

  // If no text query, asset matches based on type filter alone
  if (query.text_query.empty()) {
    return true;
  }

  std::string query_lower = to_lowercase(query.text_query);
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

void filter_assets(SearchState& search_state, const std::vector<Asset>& assets, std::mutex& assets_mutex) {
  auto start_time = std::chrono::high_resolution_clock::now();

  search_state.filtered_assets.clear();
  search_state.selected_asset_index = -1; // Clear selection when search results change

  // Reset model preview state when filtering
  search_state.model_preview_row = -1;

  size_t total_assets = 0;
  size_t filtered_count = 0;

  // Build UI type filters from toggle states
  std::vector<AssetType> ui_type_filters;
  if (search_state.type_filter_2d) ui_type_filters.push_back(AssetType::_2D);
  if (search_state.type_filter_3d) ui_type_filters.push_back(AssetType::_3D);
  if (search_state.type_filter_audio) ui_type_filters.push_back(AssetType::Audio);
  if (search_state.type_filter_shader) ui_type_filters.push_back(AssetType::Shader);
  if (search_state.type_filter_font) ui_type_filters.push_back(AssetType::Font);

  // Parse the search query once before the loop, passing UI filters
  SearchQuery query = parse_search_query(search_state.buffer, ui_type_filters);

  // Lock assets during filtering to prevent race conditions
  std::lock_guard<std::mutex> lock(assets_mutex);

  total_assets = assets.size();
  LOG_DEBUG("Filtering {} assets with query: '{}', type filters count: {}",
    total_assets, search_state.buffer, query.type_filters.size());

  for (const auto& asset : assets) {
    // Skip ignored asset types
    if (Config::IGNORED_ASSET_TYPES.count(asset.type) > 0) {
      LOG_DEBUG("Skipping ignored asset type: {} ({})", asset.name, get_asset_type_string(asset.type));
      continue;
    }

    if (asset_matches_search(asset, query)) {
      search_state.filtered_assets.push_back(asset);
      filtered_count++;
      LOG_DEBUG("Asset matched: {} ({})", asset.name, get_asset_type_string(asset.type));
    }

    // Enforce reasonable limit to prevent UI blocking
    if (filtered_count >= Config::MAX_SEARCH_RESULTS) {
      LOG_INFO("Search results limited to {} items", Config::MAX_SEARCH_RESULTS);
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

  LOG_INFO("Search for \"{}\" completed in {:.1f} ms. Filtered {}/{} assets",
    search_state.buffer, duration.count() / 1000.0, filtered_count, total_assets);
}
