#include "search.h"
#include "utils.h"
#include "config.h"
#include "event_processor.h"
#include "logger.h"
#include "asset.h"
#include "imgui.h"
#include "theme.h"
#include <sstream>
#include <algorithm>
#include <mutex>
#include <regex>
#include <unordered_set>

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
  while (pos < input_.length() && std::isspace(input_[pos])) {
    pos++;
  }
  return pos < input_.length();
}

void SearchTokenizer::skip_whitespace() {
  while (current_pos_ < input_.length() && std::isspace(input_[current_pos_])) {
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
    if (std::isspace(c) || c == '"' || c == '=' || c == ',') {
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
    std::string asset_relative_path = to_lowercase(get_relative_asset_path(asset.full_path.u8string()));

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
  std::string path_lower = to_lowercase(get_relative_asset_path(asset.full_path.u8string()));

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

void filter_assets(SearchState& search_state, const std::unordered_map<std::string, Asset>& assets, std::mutex& assets_mutex) {
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
  SearchQuery query = parse_search_query(search_state.buffer, ui_type_filters, search_state.path_filters);

  // Lock assets during filtering to prevent race conditions
  std::lock_guard<std::mutex> lock(assets_mutex);

  total_assets = assets.size();
  LOG_TRACE("Filtering {} assets with query: '{}', type filters count: {}, path filters count: {}",
    total_assets, search_state.buffer, query.type_filters.size(), query.path_filters.size());

  for (const auto& [key, asset] : assets) {
    // Skip ignored asset types
    if (Config::IGNORED_ASSET_TYPES.count(asset.type) > 0) {
      continue;
    }

    if (asset_matches_search(asset, query)) {
      search_state.filtered_assets.push_back(asset);
      filtered_count++;
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

