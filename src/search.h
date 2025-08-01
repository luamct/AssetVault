#pragma once

#include <string>
#include <vector>
#include <chrono>
#include <atomic>
#include <optional>
#include <mutex>
#include "asset.h"
#include "3d.h"

// Token types for search query parsing
enum class SearchTokenType {
  TEXT,          // Regular text content
  FILTER_NAME,   // Filter names like "type", "path"
  EQUALS,        // = operator
  COMMA,         // , separator
  QUOTED_STRING, // "quoted string"
  END_OF_INPUT   // End of input marker
};

// Individual token with type, value, and position info
struct SearchToken {
  SearchTokenType type;
  std::string value;
  size_t position;        // Character position in original string
  size_t length;          // Length of token in original string
  
  SearchToken(SearchTokenType t, const std::string& v, size_t pos, size_t len)
    : type(t), value(v), position(pos), length(len) {}
};

// Parsed search query with optional filters
struct SearchQuery {
  std::string text_query;                    // Regular search terms
  std::vector<AssetType> type_filters;       // Multiple type filters (OR condition)
  std::vector<std::string> path_filters;     // Multiple path filters (OR condition)
};

// Tokenizer for breaking search query into tokens
class SearchTokenizer {
public:
  explicit SearchTokenizer(const std::string& input);
  
  // Get next token from input stream
  SearchToken next_token();
  
  // Peek at next token without consuming it
  SearchToken peek_token();
  
  // Check if there are more tokens
  bool has_more_tokens() const;
  
  // Get current position in input
  size_t get_position() const { return current_pos_; }

private:
  const std::string& input_;
  size_t current_pos_;
  std::optional<SearchToken> peeked_token_;
  
  // Helper methods
  void skip_whitespace();
  SearchToken parse_quoted_string();
  SearchToken parse_word();
  bool is_filter_name(const std::string& word) const;
};

// Parser for building structured SearchQuery from tokens
class SearchQueryParser {
public:
  explicit SearchQueryParser(SearchTokenizer& tokenizer);
  
  // Parse tokens into SearchQuery structure
  SearchQuery parse(const std::vector<AssetType>& ui_type_filters = {},
                   const std::vector<std::string>& ui_path_filters = {});

private:
  SearchTokenizer& tokenizer_;
  
  // Grammar parsing methods
  void parse_filter(SearchQuery& query, const SearchToken& filter_name);
  std::vector<std::string> parse_filter_values();
  void add_type_filter(SearchQuery& query, const std::string& type_str);
  void add_path_filter(SearchQuery& query, const std::string& path_str);
};

// Search state structure
struct SearchState {
  std::atomic<bool> update_needed{ true };

  char buffer[256] = "";
  std::string last_buffer = "";
  std::string input_tracking = ""; // Track input to detect real changes

  // Debouncing state
  std::chrono::steady_clock::time_point last_keypress_time;
  bool pending_search = false;

  // UI state
  std::vector<Asset> filtered_assets;
  int selected_asset_index = -1; // -1 means no selection

  // Infinite scroll state
  static constexpr int LOAD_BATCH_SIZE = 50;
  int loaded_start_index = 0;    // Always 0, never changes
  int loaded_end_index = 0;      // Grows as user scrolls down

  // Model preview state
  int model_preview_row = -1;    // Which row has the expanded preview
  Model current_model;

  // Audio playback settings
  bool auto_play_audio = true;

  // Type filter toggle states
  bool type_filter_2d = false;
  bool type_filter_3d = false;
  bool type_filter_audio = false;
  bool type_filter_shader = false;
  bool type_filter_font = false;
  
  // Internal path filters (set by clicking on path segments)
  std::vector<std::string> internal_path_filters;
};

// Parse search string into structured query
// UI filters take precedence over any filters found in the query string
SearchQuery parse_search_query(const std::string& search_string, 
                              const std::vector<AssetType>& ui_type_filters = {},
                              const std::vector<std::string>& ui_path_filters = {});

// Function to check if an asset matches the search query
bool asset_matches_search(const Asset& asset, const SearchQuery& query);

// Function to filter assets based on search query
void filter_assets(SearchState& search_state, const std::vector<Asset>& assets, std::mutex& assets_mutex);

