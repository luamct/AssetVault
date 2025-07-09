#include <chrono>
#include <iomanip>
#include <iostream>

#include "../src/database.h"
#include "../src/index.h"

void print_asset_info(const FileInfo& file) {
  std::cout << std::left << std::setw(20) << file.name;
  std::cout << std::setw(15) << get_asset_type_string(file.type);
  std::cout << std::setw(10) << file.size;
  std::cout << std::setw(30) << file.relative_path;
  std::cout << std::endl;
}

void print_separator() { std::cout << std::string(80, '-') << std::endl; }

void print_header() {
  std::cout << std::left << std::setw(20) << "Name";
  std::cout << std::setw(15) << "Type";
  std::cout << std::setw(10) << "Size";
  std::cout << std::setw(30) << "Path";
  std::cout << std::endl;
  print_separator();
}

void test_database_operations() {
  std::cout << "=== Asset Database Test ===" << std::endl;

  // Initialize database
  AssetDatabase db;
  if (!db.initialize("test_asset_inventory.db")) {
    std::cerr << "Failed to initialize database!" << std::endl;
    return;
  }

  std::cout << "Database initialized successfully!" << std::endl;

  // Scan assets directory
  std::cout << "\nScanning assets directory..." << std::endl;
  std::vector<FileInfo> files = scan_directory("assets");

  if (files.empty()) {
    std::cout << "No files found in assets directory!" << std::endl;
    return;
  }

  std::cout << "Found " << files.size() << " files and directories" << std::endl;

  // Clear existing data and insert new data
  std::cout << "\nClearing existing data..." << std::endl;
  db.clear_all_assets();

  // Insert all files into database
  std::cout << "Inserting files into database..." << std::endl;
  auto start_time = std::chrono::high_resolution_clock::now();

  if (db.insert_assets_batch(files)) {
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    std::cout << "Successfully inserted " << files.size() << " files in " << duration.count() << "ms" << std::endl;
  } else {
    std::cerr << "Failed to insert files!" << std::endl;
    return;
  }

  // Test queries
  std::cout << "\n=== Database Statistics ===" << std::endl;
  std::cout << "Total assets: " << db.get_total_asset_count() << std::endl;
  std::cout << "Total size: " << db.get_total_size() << " bytes" << std::endl;

  // Count by type
  std::cout << "\nAssets by type:" << std::endl;
  std::vector<AssetType> types = {AssetType::Texture, AssetType::Model,     AssetType::Sound,
                                  AssetType::Font,    AssetType::Shader,    AssetType::Document,
                                  AssetType::Archive, AssetType::Directory, AssetType::Unknown};

  for (auto type : types) {
    int count = db.get_asset_count_by_type(type);
    uint64_t size = db.get_size_by_type(type);
    if (count > 0) {
      std::cout << "  " << std::left << std::setw(12) << get_asset_type_string(type) << ": " << count << " files, "
                << size << " bytes" << std::endl;
    }
  }

  // Test retrieving all assets
  std::cout << "\n=== All Assets ===" << std::endl;
  std::vector<FileInfo> all_assets = db.get_all_assets();
  print_header();

  int count = 0;
  for (const auto& asset : all_assets) {
    if (count++ >= 20) {  // Limit to first 20 for display
      std::cout << "... and " << (all_assets.size() - 20) << " more files" << std::endl;
      break;
    }
    print_asset_info(asset);
  }

  // Test filtering by type
  std::cout << "\n=== Textures Only ===" << std::endl;
  std::vector<FileInfo> textures = db.get_assets_by_type(AssetType::Texture);
  if (!textures.empty()) {
    print_header();
    for (const auto& texture : textures) {
      print_asset_info(texture);
    }
  } else {
    std::cout << "No textures found." << std::endl;
  }

  // Test searching by name
  std::cout << "\n=== Search for 'texture' ===" << std::endl;
  std::vector<FileInfo> search_results = db.search_assets_by_name("texture");
  if (!search_results.empty()) {
    print_header();
    for (const auto& result : search_results) {
      print_asset_info(result);
    }
  } else {
    std::cout << "No files found matching 'texture'." << std::endl;
  }

  // Test getting assets by directory
  std::cout << "\n=== Files in 'icons' directory ===" << std::endl;
  std::vector<FileInfo> icon_files = db.get_assets_by_directory("icons");
  if (!icon_files.empty()) {
    print_header();
    for (const auto& file : icon_files) {
      print_asset_info(file);
    }
  } else {
    std::cout << "No files found in 'icons' directory." << std::endl;
  }

  // Test getting a specific asset
  if (!all_assets.empty()) {
    std::cout << "\n=== Specific Asset Lookup ===" << std::endl;
    const std::string& test_path = all_assets[0].full_path;
    FileInfo found_asset = db.get_asset_by_path(test_path);

    if (!found_asset.full_path.empty()) {
      std::cout << "Found asset: " << found_asset.name << " (" << found_asset.full_path << ")" << std::endl;
    } else {
      std::cout << "Asset not found: " << test_path << std::endl;
    }
  }

  std::cout << "\n=== Database Test Complete ===" << std::endl;
}

int main() {
  std::cout << '\n';
  std::cout << "Asset Inventory Database Test\n";
  std::cout << std::string(80, '-') << '\n';

  AssetDatabase db;

  // Initialize database
  if (!db.initialize("test_asset_inventory.db")) {
    std::cerr << "Failed to initialize database!\n";
    return 1;
  }

  std::cout << "Database initialized successfully!\n";

  // Scan assets directory
  std::cout << "\nScanning assets directory...\n";
  std::vector<FileInfo> files = scan_directory("assets");

  if (files.empty()) {
    std::cout << "No files found in assets directory!\n";
    return 0;
  }

  std::cout << "Found " << files.size() << " files and directories\n";

  // Clear existing data
  std::cout << "\nClearing existing data...\n";
  db.clear_all_assets();

  // Insert files into database
  std::cout << "Inserting files into database...\n";
  auto start_time = std::chrono::high_resolution_clock::now();
  bool success = db.insert_assets_batch(files);
  auto end_time = std::chrono::high_resolution_clock::now();
  auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

  if (success) {
    std::cout << "Successfully inserted " << files.size() << " files in " << duration.count() << "ms\n";
  } else {
    std::cerr << "Failed to insert files!\n";
    return 1;
  }

  try {
    test_database_operations();
  } catch (const std::exception& e) {
    std::cerr << "Exception: " << e.what() << '\n';
    return 1;
  }

  return 0;
}
