#pragma once
#include <sqlite3.h>
#include <string>
#include <vector>

#include "asset.h"

class AssetDatabase {
public:
  AssetDatabase();
  virtual ~AssetDatabase();

  // Database operations
  virtual bool initialize(const std::string& db_path = "asset_inventory.db");
  void close();
  bool is_open() const;

  // Table operations
  bool create_tables();
  bool drop_tables();

  // Asset operations
  bool insert_asset(Asset& file);  // Non-const to update ID after insertion
  bool update_asset(const Asset& file);
  bool delete_asset(const std::string& full_path);
  bool delete_assets_by_directory(const std::string& directory_path);

  // Query operations
  virtual std::vector<Asset> get_all_assets();
  std::vector<Asset> get_assets_by_type(AssetType type);
  std::vector<Asset> get_assets_by_directory(const std::string& directory_path);
  Asset get_asset_by_path(const std::string& full_path);
  std::vector<Asset> search_assets_by_name(const std::string& search_term);

  // Statistics
  int get_total_asset_count();
  int get_asset_count_by_type(AssetType type);
  uint64_t get_total_size();
  uint64_t get_size_by_type(AssetType type);

  // Batch operations
  virtual bool insert_assets_batch(std::vector<Asset>& files);  // Non-const to update IDs after insertion
  virtual bool update_assets_batch(const std::vector<Asset>& files);
  virtual bool delete_assets_batch(const std::vector<std::string>& paths);
  bool clear_all_assets();

  // Configuration operations
  bool upsert_config_value(const std::string& key, const std::string& value);
  bool try_get_config_value(const std::string& key, std::string& out_value);

private:
  sqlite3* db_;
  bool is_open_;

  // Helper methods
  bool execute_sql(const std::string& sql);
  bool prepare_statement(const std::string& sql, sqlite3_stmt** stmt);
  void finalize_statement(sqlite3_stmt* stmt);

  // Convert between Asset and database format
  bool bind_file_info_to_statement(sqlite3_stmt* stmt, const Asset& file);
  Asset create_file_info_from_statement(sqlite3_stmt* stmt);

  // Error handling
  void print_sqlite_error(const std::string& operation);
};
