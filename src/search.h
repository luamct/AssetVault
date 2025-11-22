#pragma once

#include <string>
#include <vector>
#include <chrono>
#include <atomic>
#include <optional>
#include <map>
#include <mutex>
#include <unordered_map>
#include <optional>
#include <unordered_set>
#include "asset.h"
#include "ui/ui.h"

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
    : type(t), value(v), position(pos), length(len) {
  }
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

// Parse search string into structured query
// UI filters take precedence over any filters found in the query string
SearchQuery parse_search_query(const std::string& search_string,
  const std::vector<AssetType>& ui_type_filters = {},
  const std::vector<std::string>& ui_path_filters = {});

// Function to check if an asset matches the search query
bool asset_matches_search(const Asset& asset, const SearchQuery& query);

// Forward declarations
class AssetDatabase;
class SearchIndex;

// Function to filter assets based on search query using search index
void filter_assets(UIState& ui_state, const SafeAssets& safe_assets);

// Entry in the sorted token index
struct TokenEntry {
    std::string token;
    std::vector<uint32_t> asset_ids;  // Sorted for efficient intersection
    
    TokenEntry() = default;
    TokenEntry(const std::string& t) : token(t) {}
    
    // For binary search in sorted vector
    bool operator<(const TokenEntry& other) const {
        return token < other.token;
    }
};

// Main search index class
class SearchIndex {
public:
    SearchIndex();
    virtual ~SearchIndex() = default;

    // Index management
    bool build_from_assets(const std::vector<Asset>& assets);
    bool save_to_database() const;

    // Asset operations
    virtual void add_asset(uint32_t asset_id, const Asset& asset);
    virtual void remove_asset(uint32_t asset_id);
    virtual void update_asset(uint32_t asset_id, const Asset& asset);
    
    // Search operations
    std::vector<uint32_t> search_prefix(const std::string& prefix) const;
    std::vector<uint32_t> search_terms(const std::vector<std::string>& terms) const;
    
    // Asset lookup
    const Asset* get_asset_by_id(uint32_t asset_id) const;
    
    // Utilities
    void clear();
    size_t get_token_count() const;
    size_t get_memory_usage() const;
    
    // Debug utility to print all tokens (for testing)
    void debug_print_tokens() const;
    
private:
    std::vector<TokenEntry> sorted_tokens_;  // Binary searchable
    std::unordered_map<uint32_t, Asset> asset_cache_;  // Fast ID-to-asset lookup

    // Tokenization
    std::unordered_set<std::string> tokenize_asset(const Asset& asset) const;
    bool is_valid_token(const std::string& token) const;
    
    // Index operations
    void rebuild_sorted_index();
    std::vector<uint32_t> intersect_results(const std::vector<std::vector<uint32_t>>& results) const;
    
    // Database operations
    bool save_token_to_db(const std::string& token, uint32_t& token_id) const;
    bool save_token_assets_to_db(uint32_t token_id, const std::vector<uint32_t>& asset_ids) const;
    bool clear_database_index() const;
};
