#include "database.h"
#include "logger.h"

#include <chrono>
#include <filesystem>
#include <iomanip>
#include <sstream>

namespace fs = std::filesystem;

AssetDatabase::AssetDatabase() : db_(nullptr), is_open_(false) {}

AssetDatabase::~AssetDatabase() {
  close();
}

bool AssetDatabase::initialize(const std::string& db_path) {
  if (is_open_) {
    close();
  }

  // Ensure directory exists
  std::filesystem::path path(db_path);
  std::filesystem::create_directories(path.parent_path());

  int rc = sqlite3_open(db_path.c_str(), &db_);
  if (rc != SQLITE_OK) {
    db_ = nullptr;
    return false;
  }

  is_open_ = true;

  // Enable foreign keys and WAL mode for better performance
  execute_sql("PRAGMA foreign_keys = ON");
  execute_sql("PRAGMA journal_mode = WAL");

  return create_tables();
}

void AssetDatabase::close() {
  if (db_) {
    sqlite3_close(db_);
    db_ = nullptr;
  }
  is_open_ = false;
}

bool AssetDatabase::is_open() const {
  return is_open_;
}

bool AssetDatabase::create_tables() {
  const std::string create_table_sql = R"(
        CREATE TABLE IF NOT EXISTS assets (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            name TEXT NOT NULL,
            extension TEXT,
            full_path TEXT UNIQUE NOT NULL,
            size INTEGER NOT NULL,
            last_modified TEXT NOT NULL,
            is_directory INTEGER NOT NULL,
            asset_type TEXT NOT NULL,
            created_at TEXT DEFAULT CURRENT_TIMESTAMP,
            updated_at TEXT DEFAULT CURRENT_TIMESTAMP
        );

        CREATE INDEX IF NOT EXISTS idx_assets_full_path ON assets(full_path);
        CREATE INDEX IF NOT EXISTS idx_assets_asset_type ON assets(asset_type);
        CREATE INDEX IF NOT EXISTS idx_assets_extension ON assets(extension);
        
        -- Search index tables
        CREATE TABLE IF NOT EXISTS tokens (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            token TEXT UNIQUE NOT NULL
        );
        
        CREATE TABLE IF NOT EXISTS token_assets (
            token_id INTEGER NOT NULL,
            asset_id INTEGER NOT NULL,
            PRIMARY KEY (token_id, asset_id),
            FOREIGN KEY (token_id) REFERENCES tokens(id) ON DELETE CASCADE,
            FOREIGN KEY (asset_id) REFERENCES assets(id) ON DELETE CASCADE
        );
        
        CREATE INDEX IF NOT EXISTS idx_tokens_token ON tokens(token);
        CREATE INDEX IF NOT EXISTS idx_token_assets_token ON token_assets(token_id);
        CREATE INDEX IF NOT EXISTS idx_token_assets_asset ON token_assets(asset_id);
    )";

  return execute_sql(create_table_sql);
}

bool AssetDatabase::drop_tables() {
  const std::string drop_table_sql = R"(
    DROP TABLE IF EXISTS token_assets;
    DROP TABLE IF EXISTS tokens;
    DROP TABLE IF EXISTS assets;
  )";
  return execute_sql(drop_table_sql);
}

bool AssetDatabase::insert_asset(Asset& file) {
  const std::string sql = R"(
        INSERT OR REPLACE INTO assets
        (name, extension, full_path, size, last_modified, is_directory, asset_type, updated_at)
        VALUES (?, ?, ?, ?, ?, ?, ?, CURRENT_TIMESTAMP)
    )";

  sqlite3_stmt* stmt;
  if (!prepare_statement(sql, &stmt)) {
    return false;
  }

  bool success = bind_file_info_to_statement(stmt, file);
  if (success) {
    int rc = sqlite3_step(stmt);
    success = (rc == SQLITE_DONE);
    if (!success) {
      print_sqlite_error("inserting asset");
    } else {
      // Get the auto-generated ID
      file.id = static_cast<uint32_t>(sqlite3_last_insert_rowid(db_));
    }
  }

  finalize_statement(stmt);
  return success;
}

bool AssetDatabase::update_asset(const Asset& file) {
  const std::string sql = R"(
        UPDATE assets SET
        name = ?, extension = ?, size = ?,
        last_modified = ?, is_directory = ?, asset_type = ?, updated_at = CURRENT_TIMESTAMP
        WHERE full_path = ?
    )";

  sqlite3_stmt* stmt;
  if (!prepare_statement(sql, &stmt)) {
    return false;
  }

  // Custom binding for UPDATE (different order than INSERT)
  auto time_t = std::chrono::system_clock::to_time_t(file.last_modified);
  std::stringstream ss;
#ifdef _WIN32
  std::tm tm_buf;
  gmtime_s(&tm_buf, &time_t);
#else
  std::tm tm_buf;
  gmtime_r(&time_t, &tm_buf);
#endif
  ss << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S");
  std::string time_str = ss.str();

  // Convert path to UTF-8 string for database storage
  std::string full_path_utf8 = file.full_path.u8string();

  bool success =
    (sqlite3_bind_text(stmt, 1, file.name.c_str(), -1, SQLITE_TRANSIENT) == SQLITE_OK &&
      sqlite3_bind_text(stmt, 2, file.extension.c_str(), -1, SQLITE_TRANSIENT) == SQLITE_OK &&
      sqlite3_bind_int64(stmt, 3, file.size) == SQLITE_OK &&
      sqlite3_bind_text(stmt, 4, time_str.c_str(), -1, SQLITE_TRANSIENT) == SQLITE_OK &&
      sqlite3_bind_int(stmt, 5, file.is_directory ? 1 : 0) == SQLITE_OK &&
      sqlite3_bind_text(stmt, 6, get_asset_type_string(file.type).c_str(), -1, SQLITE_TRANSIENT) == SQLITE_OK &&
      sqlite3_bind_text(stmt, 7, full_path_utf8.c_str(), -1, SQLITE_TRANSIENT) == SQLITE_OK);

  if (!success) {
    print_sqlite_error("binding parameters for update");
  }

  if (success) {

    int rc = sqlite3_step(stmt);
    success = (rc == SQLITE_DONE);
    if (!success) {
      print_sqlite_error("updating asset");
    }
  }

  finalize_statement(stmt);
  return success;
}

bool AssetDatabase::delete_asset(const std::string& full_path) {
  const std::string sql = "DELETE FROM assets WHERE full_path = ?";

  sqlite3_stmt* stmt;
  if (!prepare_statement(sql, &stmt)) {
    return false;
  }

  sqlite3_bind_text(stmt, 1, full_path.c_str(), -1, SQLITE_TRANSIENT);

  int rc = sqlite3_step(stmt);
  bool success = (rc == SQLITE_DONE);
  if (!success) {
    print_sqlite_error("deleting asset");
  }

  finalize_statement(stmt);
  return success;
}

bool AssetDatabase::delete_assets_by_directory(const std::string& directory_path) {
  const std::string sql = "DELETE FROM assets WHERE full_path LIKE ? || '%'";

  sqlite3_stmt* stmt;
  if (!prepare_statement(sql, &stmt)) {
    return false;
  }

  sqlite3_bind_text(stmt, 1, directory_path.c_str(), -1, SQLITE_TRANSIENT);

  int rc = sqlite3_step(stmt);
  bool success = (rc == SQLITE_DONE);
  if (!success) {
    print_sqlite_error("deleting assets by directory");
  }

  finalize_statement(stmt);
  return success;
}

std::vector<Asset> AssetDatabase::get_all_assets() {
  const std::string sql = "SELECT * FROM assets ORDER BY full_path";
  std::vector<Asset> assets;

  sqlite3_stmt* stmt;
  if (!prepare_statement(sql, &stmt)) {
    return assets;
  }

  while (sqlite3_step(stmt) == SQLITE_ROW) {
    assets.push_back(create_file_info_from_statement(stmt));
  }

  finalize_statement(stmt);
  return assets;
}

std::vector<Asset> AssetDatabase::get_assets_by_type(AssetType type) {
  const std::string sql = "SELECT * FROM assets WHERE asset_type = ? ORDER BY full_path";
  std::vector<Asset> assets;

  sqlite3_stmt* stmt;
  if (!prepare_statement(sql, &stmt)) {
    return assets;
  }

  std::string type_str = get_asset_type_string(type);
  sqlite3_bind_text(stmt, 1, type_str.c_str(), -1, SQLITE_TRANSIENT);

  while (sqlite3_step(stmt) == SQLITE_ROW) {
    assets.push_back(create_file_info_from_statement(stmt));
  }

  finalize_statement(stmt);
  return assets;
}

std::vector<Asset> AssetDatabase::get_assets_by_directory(const std::string& directory_path) {
  const std::string sql = "SELECT * FROM assets WHERE full_path LIKE ? || '%' ORDER BY "
    "full_path";
  std::vector<Asset> assets;

  sqlite3_stmt* stmt;
  if (!prepare_statement(sql, &stmt)) {
    return assets;
  }

  sqlite3_bind_text(stmt, 1, directory_path.c_str(), -1, SQLITE_TRANSIENT);

  while (sqlite3_step(stmt) == SQLITE_ROW) {
    assets.push_back(create_file_info_from_statement(stmt));
  }

  finalize_statement(stmt);
  return assets;
}

Asset AssetDatabase::get_asset_by_path(const std::string& full_path) {
  const std::string sql = "SELECT * FROM assets WHERE full_path = ?";
  Asset file;

  sqlite3_stmt* stmt;
  if (!prepare_statement(sql, &stmt)) {
    return file;
  }

  sqlite3_bind_text(stmt, 1, full_path.c_str(), -1, SQLITE_TRANSIENT);

  if (sqlite3_step(stmt) == SQLITE_ROW) {
    file = create_file_info_from_statement(stmt);
  }

  finalize_statement(stmt);
  return file;
}

std::vector<Asset> AssetDatabase::search_assets_by_name(const std::string& search_term) {
  const std::string sql = "SELECT * FROM assets WHERE name LIKE ? ORDER BY full_path";
  std::vector<Asset> assets;

  sqlite3_stmt* stmt;
  if (!prepare_statement(sql, &stmt)) {
    return assets;
  }

  std::string pattern = "%" + search_term + "%";
  sqlite3_bind_text(stmt, 1, pattern.c_str(), -1, SQLITE_TRANSIENT);

  while (sqlite3_step(stmt) == SQLITE_ROW) {
    assets.push_back(create_file_info_from_statement(stmt));
  }

  finalize_statement(stmt);
  return assets;
}

int AssetDatabase::get_total_asset_count() {
  const std::string sql = "SELECT COUNT(*) FROM assets";
  int count = 0;

  sqlite3_stmt* stmt;
  if (!prepare_statement(sql, &stmt)) {
    return count;
  }

  if (sqlite3_step(stmt) == SQLITE_ROW) {
    count = sqlite3_column_int(stmt, 0);
  }

  finalize_statement(stmt);
  return count;
}

int AssetDatabase::get_asset_count_by_type(AssetType type) {
  const std::string sql = "SELECT COUNT(*) FROM assets WHERE asset_type = ?";
  int count = 0;

  sqlite3_stmt* stmt;
  if (!prepare_statement(sql, &stmt)) {
    return count;
  }

  std::string type_str = get_asset_type_string(type);
  sqlite3_bind_text(stmt, 1, type_str.c_str(), -1, SQLITE_TRANSIENT);

  if (sqlite3_step(stmt) == SQLITE_ROW) {
    count = sqlite3_column_int(stmt, 0);
  }

  finalize_statement(stmt);
  return count;
}

uint64_t AssetDatabase::get_total_size() {
  const std::string sql = "SELECT SUM(size) FROM assets WHERE is_directory = 0";
  uint64_t total_size = 0;

  sqlite3_stmt* stmt;
  if (!prepare_statement(sql, &stmt)) {
    return total_size;
  }

  if (sqlite3_step(stmt) == SQLITE_ROW) {
    total_size = sqlite3_column_int64(stmt, 0);
  }

  finalize_statement(stmt);
  return total_size;
}

uint64_t AssetDatabase::get_size_by_type(AssetType type) {
  const std::string sql = "SELECT SUM(size) FROM assets WHERE asset_type = ? AND is_directory = 0";
  uint64_t total_size = 0;

  sqlite3_stmt* stmt;
  if (!prepare_statement(sql, &stmt)) {
    return total_size;
  }

  std::string type_str = get_asset_type_string(type);
  sqlite3_bind_text(stmt, 1, type_str.c_str(), -1, SQLITE_TRANSIENT);

  if (sqlite3_step(stmt) == SQLITE_ROW) {
    total_size = sqlite3_column_int64(stmt, 0);
  }

  finalize_statement(stmt);
  return total_size;
}

bool AssetDatabase::insert_assets_batch(std::vector<Asset>& files) {
  if (files.empty()) {
    return true;
  }

  // Begin transaction for better performance
  if (!execute_sql("BEGIN TRANSACTION")) {
    return false;
  }

  bool success = true;
  for (auto& file : files) {  // Non-const to allow ID updates
    if (!insert_asset(file)) {
      success = false;
      break;
    }
  }

  if (success) {
    execute_sql("COMMIT");
  }
  else {
    execute_sql("ROLLBACK");
  }

  return success;
}

bool AssetDatabase::update_assets_batch(const std::vector<Asset>& files) {
  if (files.empty()) {
    return true;
  }

  // Begin transaction for better performance
  if (!execute_sql("BEGIN TRANSACTION")) {
    return false;
  }

  bool success = true;
  for (const auto& file : files) {
    if (!update_asset(file)) {
      success = false;
      break;
    }
  }

  if (success) {
    execute_sql("COMMIT");
  }
  else {
    execute_sql("ROLLBACK");
  }

  return success;
}

bool AssetDatabase::delete_assets_batch(const std::vector<std::string>& paths) {
  if (paths.empty()) {
    return true;
  }

  // Begin transaction for better performance
  if (!execute_sql("BEGIN TRANSACTION")) {
    return false;
  }

  bool success = true;
  for (const auto& path : paths) {
    if (!delete_asset(path)) {
      success = false;
      break;
    }
  }

  if (success) {
    execute_sql("COMMIT");
  }
  else {
    execute_sql("ROLLBACK");
  }

  return success;
}

bool AssetDatabase::clear_all_assets() {
  return execute_sql("DELETE FROM assets");
}

// Private helper methods

bool AssetDatabase::execute_sql(const std::string& sql) {
  char* error_msg = nullptr;
  int rc = sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &error_msg);

  if (rc != SQLITE_OK) {
    LOG_ERROR("SQL error: {}", error_msg);
    sqlite3_free(error_msg);
    return false;
  }

  return true;
}

bool AssetDatabase::prepare_statement(const std::string& sql, sqlite3_stmt** stmt) {
  int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, stmt, nullptr);
  if (rc != SQLITE_OK) {
    print_sqlite_error("preparing statement");
    return false;
  }
  return true;
}

void AssetDatabase::finalize_statement(sqlite3_stmt* stmt) {
  if (stmt) {
    sqlite3_finalize(stmt);
  }
}

bool AssetDatabase::bind_file_info_to_statement(sqlite3_stmt* stmt, const Asset& file) {
  int param = 1;

  // Convert time_point to string for storage
  auto time_t = std::chrono::system_clock::to_time_t(file.last_modified);
  std::stringstream ss;

  // Use thread-safe version of gmtime
  std::tm tm_buf;
#ifdef _WIN32
  gmtime_s(&tm_buf, &time_t);
#else
  gmtime_r(&time_t, &tm_buf);
#endif
  ss << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S");
  std::string time_str = ss.str();

  // Convert path to UTF-8 string for database storage
  std::string full_path_utf8 = file.full_path.u8string();

  // Use SQLITE_TRANSIENT instead of SQLITE_STATIC for better string handling
  if (sqlite3_bind_text(stmt, param++, file.name.c_str(), -1, SQLITE_TRANSIENT) != SQLITE_OK ||
    sqlite3_bind_text(stmt, param++, file.extension.c_str(), -1, SQLITE_TRANSIENT) != SQLITE_OK ||
    sqlite3_bind_text(stmt, param++, full_path_utf8.c_str(), -1, SQLITE_TRANSIENT) != SQLITE_OK ||
    sqlite3_bind_int64(stmt, param++, file.size) != SQLITE_OK ||
    sqlite3_bind_text(stmt, param++, time_str.c_str(), -1, SQLITE_TRANSIENT) != SQLITE_OK ||
    sqlite3_bind_int(stmt, param++, file.is_directory ? 1 : 0) != SQLITE_OK ||
    sqlite3_bind_text(stmt, param++, get_asset_type_string(file.type).c_str(), -1, SQLITE_TRANSIENT) != SQLITE_OK) {
    print_sqlite_error("binding parameters");
    return false;
  }

  return true;
}

Asset AssetDatabase::create_file_info_from_statement(sqlite3_stmt* stmt) {
  Asset file;

  file.id = sqlite3_column_int(stmt, 0);  // ID is the first column
  file.name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
  file.extension = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));

  // Convert UTF-8 string from database to filesystem::path
  const char* path_str = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
  file.full_path = std::filesystem::u8path(path_str);
  file.size = sqlite3_column_int64(stmt, 4);

  // Parse time string back to time_point
  std::string time_str = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
  std::tm tm = {};
  std::istringstream ss(time_str);
  ss >> std::get_time(&tm, "%Y-%m-%d %H:%M:%S");
  file.last_modified = std::chrono::system_clock::from_time_t(std::mktime(&tm));

  file.is_directory = sqlite3_column_int(stmt, 6) != 0;

  std::string type_str = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7));

  // Use centralized conversion function
  file.type = get_asset_type_from_string(type_str);

  return file;
}

void AssetDatabase::print_sqlite_error(const std::string& operation) {
  LOG_ERROR("SQLite error during {}: {}", operation, sqlite3_errmsg(db_));
}
