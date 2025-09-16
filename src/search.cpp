#include "search.h"
#include "database.h"
#include "utils.h"
#include "event_processor.h"
#include "logger.h"
#include "asset.h"
#include <sstream>
#include <iostream>
#include <algorithm>
#include <mutex>
#include <regex>
#include <unordered_set>
#include <cctype>

// SearchTokenizer Implementation
SearchTokenizer::SearchTokenizer(const std::string& input)
  : input_(input), current_pos_(0) {
}

SearchToken SearchTokenizer::next_token() {
  if (peeked_token_.has_value()) {
    SearchToken token = peeked_token_.value();
    peeked_token_.reset();
    return token;
  }

  skip_whitespace();

  if (current_pos_ >= input_.length()) {
    return SearchToken(SearchTokenType::END_OF_INPUT, "", current_pos_, 0);
  }

  char current_char = input_[current_pos_];

  // Handle quoted strings
  if (current_char == '"') {
    return parse_quoted_string();
  }

  // Handle operators
  if (current_char == '=') {
    size_t start_pos = current_pos_;
    current_pos_++;
    return SearchToken(SearchTokenType::EQUALS, "=", start_pos, 1);
  }

  if (current_char == ',') {
    size_t start_pos = current_pos_;
    current_pos_++;
    return SearchToken(SearchTokenType::COMMA, ",", start_pos, 1);
  }

  // Handle words (identifiers and text)
  return parse_word();
}

SearchToken SearchTokenizer::peek_token() {
  if (!peeked_token_.has_value()) {
    peeked_token_ = next_token();
  }
  return peeked_token_.value();
}

bool SearchTokenizer::has_more_tokens() const {
  if (peeked_token_.has_value()) {
    return peeked_token_->type != SearchTokenType::END_OF_INPUT;
  }

  // Skip whitespace to check for real content
  size_t pos = current_pos_;
  while (pos < input_.length() && std::isspace(static_cast<unsigned char>(input_[pos]))) {
    pos++;
  }
  return pos < input_.length();
}

void SearchTokenizer::skip_whitespace() {
  while (current_pos_ < input_.length() && std::isspace(static_cast<unsigned char>(input_[current_pos_]))) {
    current_pos_++;
  }
}

SearchToken SearchTokenizer::parse_quoted_string() {
  size_t start_pos = current_pos_;
  current_pos_++; // Skip opening quote

  std::string value;

  while (current_pos_ < input_.length()) {
    char c = input_[current_pos_];

    if (c == '"') {
      // End of quoted string
      current_pos_++; // Skip closing quote
      size_t length = current_pos_ - start_pos;
      return SearchToken(SearchTokenType::QUOTED_STRING, value, start_pos, length);
    }

    if (c == '\\' && current_pos_ + 1 < input_.length()) {
      // Handle escape sequences
      current_pos_++; // Skip backslash
      char escaped_char = input_[current_pos_];
      value += escaped_char;
      current_pos_++;
    }
    else {
      value += c;
      current_pos_++;
    }
  }

  // Unclosed quoted string - return what we have
  size_t length = current_pos_ - start_pos;
  return SearchToken(SearchTokenType::QUOTED_STRING, value, start_pos, length);
}

SearchToken SearchTokenizer::parse_word() {
  size_t start_pos = current_pos_;
  std::string value;

  while (current_pos_ < input_.length()) {
    char c = input_[current_pos_];

    // Stop at whitespace, quotes, or operators
    if (std::isspace(static_cast<unsigned char>(c)) || c == '"' || c == '=' || c == ',') {
      break;
    }

    value += c;
    current_pos_++;
  }

  size_t length = current_pos_ - start_pos;

  // Determine if this is a filter name or regular text
  SearchTokenType type = is_filter_name(value) ? SearchTokenType::FILTER_NAME : SearchTokenType::TEXT;

  return SearchToken(type, value, start_pos, length);
}

bool SearchTokenizer::is_filter_name(const std::string& word) const {
  static const std::unordered_set<std::string> filter_names = {
    "type", "path"
  };

  std::string lowercase_word = to_lowercase(word);
  return filter_names.count(lowercase_word) > 0;
}

// SearchQueryParser Implementation
SearchQueryParser::SearchQueryParser(SearchTokenizer& tokenizer)
  : tokenizer_(tokenizer) {
}

SearchQuery SearchQueryParser::parse(const std::vector<AssetType>& ui_type_filters,
  const std::vector<std::string>& ui_path_filters) {
  SearchQuery query;

  // Use UI filters if provided (they take precedence)
  if (!ui_type_filters.empty()) {
    query.type_filters = ui_type_filters;
  }
  if (!ui_path_filters.empty()) {
    query.path_filters = ui_path_filters;
  }

  std::vector<std::string> text_terms;

  while (tokenizer_.has_more_tokens()) {
    SearchToken token = tokenizer_.next_token();

    if (token.type == SearchTokenType::END_OF_INPUT) {
      break;
    }

    if (token.type == SearchTokenType::FILTER_NAME) {
      // Parse filter only if UI hasn't provided filters of this type
      std::string filter_name = to_lowercase(token.value);

      if (filter_name == "type" && ui_type_filters.empty()) {
        parse_filter(query, token);
      }
      else if (filter_name == "path" && ui_path_filters.empty()) {
        parse_filter(query, token);
      }
      else {
        // Skip this filter (UI takes precedence) - consume tokens until next filter or end
        SearchToken next = tokenizer_.peek_token();
        if (next.type == SearchTokenType::EQUALS) {
          tokenizer_.next_token(); // consume equals
          parse_filter_values(); // consume and discard values
        }
      }
    }
    else if (token.type == SearchTokenType::TEXT) {
      text_terms.push_back(token.value);
    }
    // Ignore other tokens (EQUALS, COMMA, QUOTED_STRING outside of filters)
  }

  // Combine text terms into text query
  if (!text_terms.empty()) {
    std::ostringstream oss;
    for (size_t i = 0; i < text_terms.size(); ++i) {
      if (i > 0) oss << " ";
      oss << text_terms[i];
    }
    query.text_query = oss.str();
  }

  return query;
}

void SearchQueryParser::parse_filter(SearchQuery& query, const SearchToken& filter_name) {
  std::string filter_name_lower = to_lowercase(filter_name.value);

  // Expect equals token
  SearchToken equals_token = tokenizer_.next_token();
  if (equals_token.type != SearchTokenType::EQUALS) {
    // Malformed filter - treat filter name as text
    return;
  }

  // Parse filter values
  std::vector<std::string> values = parse_filter_values();

  // Add values to appropriate filter type
  for (const std::string& value : values) {
    if (filter_name_lower == "type") {
      add_type_filter(query, value);
    }
    else if (filter_name_lower == "path") {
      add_path_filter(query, value);
    }
  }
}

std::vector<std::string> SearchQueryParser::parse_filter_values() {
  std::vector<std::string> values;

  while (tokenizer_.has_more_tokens()) {
    SearchToken token = tokenizer_.next_token();

    if (token.type == SearchTokenType::TEXT ||
      token.type == SearchTokenType::QUOTED_STRING) {
      values.push_back(token.value);

      // Check for comma (more values)
      SearchToken next = tokenizer_.peek_token();
      if (next.type == SearchTokenType::COMMA) {
        tokenizer_.next_token(); // consume comma
        continue;
      }
      else {
        break; // No more values in this filter
      }
    }
    else {
      // Unexpected token - stop parsing values
      break;
    }
  }

  return values;
}

void SearchQueryParser::add_type_filter(SearchQuery& query, const std::string& type_str) {
  std::string type_lower = to_lowercase(type_str);
  AssetType type = get_asset_type_from_string(type_lower);

  if (type != AssetType::Unknown) {
    query.type_filters.push_back(type);
  }
}

void SearchQueryParser::add_path_filter(SearchQuery& query, const std::string& path_str) {
  std::string normalized_path = normalize_path_separators(path_str);

  if (!normalized_path.empty()) {
    query.path_filters.push_back(normalized_path);
  }
}

// Parse search string using new token-based parser
SearchQuery parse_search_query(const std::string& search_string,
  const std::vector<AssetType>& ui_type_filters,
  const std::vector<std::string>& ui_path_filters) {

  SearchTokenizer tokenizer(search_string);
  SearchQueryParser parser(tokenizer);
  SearchQuery query = parser.parse(ui_type_filters, ui_path_filters);

  LOG_TRACE("Final parsed query - Text: '{}', Type filters count: {}, Path filters count: {}",
    query.text_query, query.type_filters.size(), query.path_filters.size());

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

  // Check path filters (OR condition - asset must match at least one path)
  if (!query.path_filters.empty()) {
    bool path_matches = false;
    std::string asset_relative_path = to_lowercase(get_relative_asset_path(asset.path));

    for (const auto& filter_path : query.path_filters) {
      std::string filter_path_lower = to_lowercase(filter_path);

      // Check if asset path starts with the filter path
      if (asset_relative_path.substr(0, filter_path_lower.length()) == filter_path_lower) {
        // Exact match or the asset path continues with a directory separator
        if (asset_relative_path.length() == filter_path_lower.length() ||
          asset_relative_path[filter_path_lower.length()] == '/') {
          path_matches = true;
          break;
        }
      }
    }
    if (!path_matches) {
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
  std::string path_lower = to_lowercase(get_relative_asset_path(asset.path));

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

void filter_assets(SearchState& search_state, const std::map<std::string, Asset>& assets,
  std::mutex& assets_mutex, SearchIndex& search_index) {
  auto start_time = std::chrono::high_resolution_clock::now();

  search_state.results.clear();
  search_state.results_ids.clear();

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
  // Only include path filters if the path filter toggle is active
  std::vector<std::string> active_path_filters;
  if (search_state.path_filter_active && !search_state.path_filters.empty()) {
    active_path_filters = search_state.path_filters;
  }
  SearchQuery query = parse_search_query(search_state.buffer, ui_type_filters, active_path_filters);

  // Lock assets during filtering to prevent race conditions
  std::lock_guard<std::mutex> lock(assets_mutex);

  total_assets = assets.size();
  LOG_TRACE("Using SearchIndex for {} assets with query: '{}', type filters count: {}, path filters count: {}",
    total_assets, search_state.buffer, query.type_filters.size(), query.path_filters.size());

  // Use SearchIndex cache for efficient asset lookup (eliminates O(n) rebuild)

  // Use search index for text queries, fall back to full scan for filter-only queries
  std::vector<uint32_t> candidate_ids;

  if (!query.text_query.empty()) {
    // Use search index for text search (O(log n) performance)
    std::vector<std::string> search_terms;
    std::stringstream ss(to_lowercase(query.text_query));
    std::string term;
    while (ss >> term) {
      if (term.length() > 2) {  // Only use terms longer than 2 characters
        search_terms.push_back(term);
      }
    }

    if (!search_terms.empty()) {
      candidate_ids = search_index.search_terms(search_terms);
      LOG_TRACE("Search index returned {} candidates for {} valid terms", candidate_ids.size(), search_terms.size());
    }
    else {
      // All search terms were too short - ignore them and show all assets like empty search
      LOG_TRACE("All search terms too short (<=2 chars), treating as empty search");
      for (const auto& [key, asset] : assets) {
        if (asset.id > 0) {
          candidate_ids.push_back(asset.id);
        }
      }
    }
  }
  else {
    // No text query - show all assets for type/path filters
    LOG_TRACE("No text query, showing all assets");
    for (const auto& [key, asset] : assets) {
      if (asset.id > 0) {  // Only include assets with valid IDs
        candidate_ids.push_back(asset.id);
      }
    }
  }

  // Convert asset IDs to Asset objects and apply remaining filters
  for (uint32_t asset_id : candidate_ids) {
    // Efficient O(1) lookup using SearchIndex cache
    const Asset* asset_ptr = search_index.get_asset_by_id(asset_id);
    if (!asset_ptr) {
      continue; // Asset ID not found in SearchIndex cache (might be stale index)
    }

    const Asset& asset = *asset_ptr;

    // Apply type filters (if any)
    if (!query.type_filters.empty()) {
      bool type_matches = false;
      for (AssetType filter_type : query.type_filters) {
        if (asset.type == filter_type) {
          type_matches = true;
          break;
        }
      }
      if (!type_matches) {
        continue;
      }
    }

    // Apply path filters (if any)
    if (!query.path_filters.empty()) {
      bool path_matches = false;
      std::string path_lower = to_lowercase(asset.path);

      for (const std::string& path_filter : query.path_filters) {
        std::string filter_lower = to_lowercase(path_filter);
        if (path_lower.find(filter_lower) != std::string::npos) {
          path_matches = true;
          break;
        }
      }
      if (!path_matches) {
        continue;
      }
    }

    // Asset passed all filters
    search_state.results.push_back(asset);
    if (asset.id > 0) {
      search_state.results_ids.insert(asset.id);
    }
    filtered_count++;
  }

  // Initialize loaded range for infinite scroll
  search_state.loaded_start_index = 0;
  search_state.loaded_end_index = std::min(
    static_cast<int>(search_state.results.size()),
    SearchState::LOAD_BATCH_SIZE
  );

  // Measure and print search time
  auto end_time = std::chrono::high_resolution_clock::now();
  auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);

  LOG_INFO("Search for \"{}\" completed in {:.1f} ms. Filtered {}/{} assets ({} candidates)",
    search_state.buffer, duration.count() / 1000.0, filtered_count, total_assets, candidate_ids.size());
}

// SearchIndex Implementation

SearchIndex::SearchIndex(AssetDatabase* database) : database_(database) {
}

std::vector<std::string> SearchIndex::tokenize_asset(const Asset& asset) const {
  std::vector<std::string> tokens;

  // Tokenize filename (without extension)
  auto name_tokens = tokenize_string(asset.name);
  tokens.insert(tokens.end(), name_tokens.begin(), name_tokens.end());

  // Add extension as a token
  if (!asset.extension.empty()) {
    tokens.push_back(to_lowercase(asset.extension));
  }

  // Tokenize path segments
  std::string path_str = asset.path;
  size_t pos = 0;
  while ((pos = path_str.find('/', pos)) != std::string::npos) {
    pos++;
    size_t next_pos = path_str.find('/', pos);
    if (next_pos == std::string::npos) next_pos = path_str.length();

    if (next_pos > pos) {
      std::string segment = path_str.substr(pos, next_pos - pos);
      if (segment != asset.name) {  // Don't duplicate filename
        auto segment_tokens = tokenize_string(segment);
        tokens.insert(tokens.end(), segment_tokens.begin(), segment_tokens.end());
      }
    }
    pos = next_pos;
  }

  // Remove duplicates and invalid tokens
  std::unordered_set<std::string> unique_tokens;
  std::vector<std::string> result;

  for (const auto& token : tokens) {
    if (is_valid_token(token) && unique_tokens.find(token) == unique_tokens.end()) {
      unique_tokens.insert(token);
      result.push_back(token);
    }
  }

  return result;
}

std::vector<std::string> SearchIndex::tokenize_string(const std::string& text) const {
  std::vector<std::string> tokens;
  std::string current_token;
  std::string lower_text = to_lowercase(text);

  for (char c : lower_text) {
    if (std::isalnum(static_cast<unsigned char>(c))) {
      current_token += c;
    }
    else if (!current_token.empty()) {
      // Split on non-alphanumeric characters
      if (is_valid_token(current_token)) {
        tokens.push_back(current_token);
      }
      current_token.clear();
    }

    // Also handle camelCase by splitting on uppercase letters
    // This is done in the lowercase string, so we need a different approach
    // For now, we'll rely on the above splitting on non-alphanumeric
  }

  // Add final token if any
  if (!current_token.empty() && is_valid_token(current_token)) {
    tokens.push_back(current_token);
  }

  return tokens;
}

bool SearchIndex::is_valid_token(const std::string& token) const {
  // Ignore tokens with length <= 2 as specified in requirements
  if (token.length() <= 2) {
    return false;
  }

  // Must contain at least one alphabetic character
  bool has_alpha = false;
  for (char c : token) {
    if (std::isalpha(static_cast<unsigned char>(c))) {
      has_alpha = true;
      break;
    }
  }

  return has_alpha;
}

std::vector<uint32_t> SearchIndex::search_prefix(const std::string& prefix) const {
  if (prefix.length() <= 2) {
    return {};  // Ignore short queries
  }

  std::string lower_prefix = to_lowercase(prefix);

  // Find the range of tokens that start with this prefix
  TokenEntry search_token(lower_prefix);

  // Find lower bound (first token >= prefix)
  auto lower = std::lower_bound(sorted_tokens_.begin(), sorted_tokens_.end(), search_token);

  // Find upper bound (first token > next_prefix)
  std::string next_prefix = lower_prefix;
  if (!next_prefix.empty()) {
    next_prefix.back()++;
    TokenEntry next_token(next_prefix);
    auto upper = std::lower_bound(sorted_tokens_.begin(), sorted_tokens_.end(), next_token);

    // Collect all asset IDs from tokens in range
    std::unordered_set<uint32_t> result_set;
    for (auto it = lower; it != upper; ++it) {
      if (it->token.substr(0, lower_prefix.length()) == lower_prefix) {
        for (uint32_t asset_id : it->asset_ids) {
          result_set.insert(asset_id);
        }
      }
    }

    // Convert to sorted vector
    std::vector<uint32_t> result(result_set.begin(), result_set.end());
    std::sort(result.begin(), result.end());
    return result;
  }

  return {};
}

std::vector<uint32_t> SearchIndex::search_terms(const std::vector<std::string>& terms) const {
  if (terms.empty()) {
    return {};
  }

  std::vector<std::vector<uint32_t>> term_results;
  term_results.reserve(terms.size());

  // Get results for each term
  for (const auto& term : terms) {
    auto results = search_prefix(term);
    if (results.empty()) {
      // If any term has no results, the intersection is empty
      return {};
    }
    term_results.push_back(std::move(results));
  }

  // Intersect all results
  return intersect_results(term_results);
}

std::vector<uint32_t> SearchIndex::intersect_results(const std::vector<std::vector<uint32_t>>& results) const {
  if (results.empty()) {
    return {};
  }

  if (results.size() == 1) {
    return results[0];
  }

  // Start with the smallest result set
  auto current_result = results[0];
  for (size_t i = 1; i < results.size(); ++i) {
    std::vector<uint32_t> intersection;
    std::set_intersection(
      current_result.begin(), current_result.end(),
      results[i].begin(), results[i].end(),
      std::back_inserter(intersection)
    );
    current_result = std::move(intersection);

    if (current_result.empty()) {
      break;  // Early termination
    }
  }

  return current_result;
}

const Asset* SearchIndex::get_asset_by_id(uint32_t asset_id) const {
  auto it = asset_cache_.find(asset_id);
  return (it != asset_cache_.end()) ? &it->second : nullptr;
}

void SearchIndex::add_asset(uint32_t asset_id, const Asset& asset) {
  auto tokens = tokenize_asset(asset);
  if (tokens.empty()) {
    return; // Nothing to index
  }

  // Add to asset cache
  asset_cache_[asset_id] = asset;

  // Add tokens to the sorted index
  for (const auto& token : tokens) {
    // Find if token already exists
    TokenEntry search_token(token);
    auto it = std::lower_bound(sorted_tokens_.begin(), sorted_tokens_.end(), search_token);

    if (it != sorted_tokens_.end() && it->token == token) {
      // Token exists, add asset ID if not already present
      auto& asset_ids = it->asset_ids;
      auto asset_it = std::lower_bound(asset_ids.begin(), asset_ids.end(), asset_id);
      if (asset_it == asset_ids.end() || *asset_it != asset_id) {
        asset_ids.insert(asset_it, asset_id);
      }
    }
    else {
      // Token doesn't exist, create new entry
      TokenEntry new_entry(token);
      new_entry.asset_ids.push_back(asset_id);
      sorted_tokens_.insert(it, std::move(new_entry));
    }
  }
}

void SearchIndex::remove_asset(uint32_t asset_id) {
  // Remove from asset cache
  asset_cache_.erase(asset_id);

  // Remove asset ID from all tokens
  for (auto it = sorted_tokens_.begin(); it != sorted_tokens_.end(); ) {
    auto& asset_ids = it->asset_ids;
    auto asset_it = std::find(asset_ids.begin(), asset_ids.end(), asset_id);

    if (asset_it != asset_ids.end()) {
      asset_ids.erase(asset_it);

      // If no more assets reference this token, remove the token entirely
      if (asset_ids.empty()) {
        it = sorted_tokens_.erase(it);
      }
      else {
        ++it;
      }
    }
    else {
      ++it;
    }
  }
}

void SearchIndex::update_asset(uint32_t asset_id, const Asset& asset) {
  LOG_DEBUG("SearchIndex: Updating asset {} ({})", asset_id, asset.name);

  // For updates, we remove the old asset and add the new one
  // This ensures that any changed tokens are properly updated
  remove_asset(asset_id);
  add_asset(asset_id, asset);
}

bool SearchIndex::build_from_database() {
  LOG_INFO("Building search index from database...");

  // Clear existing index
  LOG_DEBUG("Clearing existing search index...");
  clear();

  // Get all assets from database
  LOG_DEBUG("Fetching all assets from database...");
  auto assets = database_->get_all_assets();
  LOG_DEBUG("Retrieved {} assets from database", assets.size());
  if (assets.empty()) {
    LOG_INFO("No assets found in database");
    return true;
  }

  // Build token to asset ID mapping
  LOG_DEBUG("Building token to asset ID mapping...");
  std::unordered_map<std::string, std::vector<uint32_t>> token_map;

  size_t processed_count = 0;
  for (const auto& asset : assets) {
    if (processed_count % 1000 == 0) {
      LOG_DEBUG("Processed {} assets...", processed_count);
    }
    if (asset.id == 0) {
      LOG_ERROR("Asset has invalid ID (0): {}", asset.path);
      continue;
    }

    // Add to asset cache
    asset_cache_[asset.id] = asset;

    auto tokens = tokenize_asset(asset);
    for (const auto& token : tokens) {
      token_map[token].push_back(asset.id);
    }
    processed_count++;
  }

  LOG_DEBUG("Finished processing {} assets, converting to sorted structure...", processed_count);

  // Convert to sorted vector structure
  LOG_DEBUG("Converting {} tokens to sorted vector structure...", token_map.size());
  sorted_tokens_.reserve(token_map.size());
  size_t token_count = 0;
  for (auto& [token, asset_ids] : token_map) {
    if (token_count % 10000 == 0) {
      LOG_DEBUG("Processed {} tokens...", token_count);
    }
    // Sort asset IDs for efficient intersection
    std::sort(asset_ids.begin(), asset_ids.end());

    TokenEntry entry(token);
    entry.asset_ids = std::move(asset_ids);
    sorted_tokens_.push_back(std::move(entry));
    token_count++;
  }

  LOG_DEBUG("Sorting {} tokens for binary search...", sorted_tokens_.size());
  // Sort tokens for binary search
  std::sort(sorted_tokens_.begin(), sorted_tokens_.end());

  LOG_INFO("Search index built: {} tokens for {} assets", sorted_tokens_.size(), assets.size());
  return true;
}

bool SearchIndex::load_from_database() {
  // For initial implementation, we'll always rebuild from assets
  // TODO: Implement database storage/loading of index
  LOG_DEBUG("Loading search index from database (fallback to rebuild)");
  return build_from_database();
}

bool SearchIndex::save_to_database() const {
  // For initial implementation, we don't persist the index
  // TODO: Implement database storage of index
  LOG_DEBUG("Saving search index to database (not yet implemented)");
  return true;
}

void SearchIndex::clear() {
  sorted_tokens_.clear();
  asset_cache_.clear();
}

size_t SearchIndex::get_token_count() const {
  return sorted_tokens_.size();
}

size_t SearchIndex::get_memory_usage() const {
  size_t total = 0;
  for (const auto& entry : sorted_tokens_) {
    total += entry.token.size();
    total += entry.asset_ids.size() * sizeof(uint32_t);
  }
  return total + sorted_tokens_.size() * sizeof(TokenEntry);
}

void SearchIndex::debug_print_tokens() const {
  std::cout << "=== DEBUG: All tokens in index ===\n";
  for (const auto& entry : sorted_tokens_) {
    std::cout << "Token: '" << entry.token << "' -> assets: ";
    for (uint32_t asset_id : entry.asset_ids) {
      std::cout << asset_id << " ";
    }
    std::cout << "\n";
  }
  std::cout << "=== End of tokens ===\n";
}
