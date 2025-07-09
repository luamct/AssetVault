#pragma once
#include <sqlite3.h>

#include <memory>
#include <string>
#include <vector>

#include "index.h"


class AssetDatabase {
 public:
  AssetDatabase();
  ~AssetDatabase();

  // Database operations
  bool initialize(const std::string& db_path = "asset_inventory.db");
  void close();
  bool is_open() const;

  // Table operations
  bool create_tables();
  bool drop_tables();

  // Asset operations
  bool insert_asset(const FileInfo& file);
  bool update_asset(const FileInfo& file);
  bool delete_asset(const std::string& full_path);
  bool delete_assets_by_directory(const std::string& directory_path);

  // Query operations
  std::vector<FileInfo> get_all_assets();
  std::vector<FileInfo> get_assets_by_type(AssetType type);
  std::vector<FileInfo> get_assets_by_directory(const std::string& directory_path);
  FileInfo get_asset_by_path(const std::string& full_path);
  std::vector<FileInfo> search_assets_by_name(const std::string& search_term);

  // Statistics
  int get_total_asset_count();
  int get_asset_count_by_type(AssetType type);
  uint64_t get_total_size();
  uint64_t get_size_by_type(AssetType type);

  // Batch operations
  bool insert_assets_batch(const std::vector<FileInfo>& files);
  bool clear_all_assets();

 private:
  sqlite3* db_;
  bool is_open_;

  // Helper methods
  bool execute_sql(const std::string& sql);
  bool prepare_statement(const std::string& sql, sqlite3_stmt** stmt);
  void finalize_statement(sqlite3_stmt* stmt);

  // Convert between FileInfo and database format
  bool bind_file_info_to_statement(sqlite3_stmt* stmt, const FileInfo& file);
  FileInfo create_file_info_from_statement(sqlite3_stmt* stmt);

  // Error handling
  void print_sqlite_error(const std::string& operation);
};
