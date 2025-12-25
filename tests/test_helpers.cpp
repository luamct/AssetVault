#include "test_helpers.h"
#include <filesystem>
#include <chrono>
#include <iostream>
#include "utils.h"

namespace fs = std::filesystem;

Asset create_test_asset(
    const std::string& relative_path,
    AssetType type,
    uint32_t id) {
    Asset asset;
    asset.id = id;

    std::string normalized_path = normalize_path_separators(relative_path);
    fs::path path_obj = fs::u8path(normalized_path);

    asset.relative_path = normalized_path;
    asset.path = normalized_path;
    asset.extension = to_lowercase(path_obj.has_extension() ? path_obj.extension().string() : "");
    asset.name = path_obj.stem().u8string();
    asset.type = type == AssetType::Unknown ? get_asset_type(asset.extension) : type;
    asset.size = 1024; // Default size
    asset.last_modified = std::chrono::system_clock::now();
    return asset;
}

void print_file_events(const std::vector<FileEvent>& events, const std::string& label) {
    std::cout << label << " - captured " << events.size() << " events:" << std::endl;
    
    for (const auto& event : events) {
        std::string event_type_str;
        switch (event.type) {
            case FileEventType::Created: 
                event_type_str = "Created";
                break;
            case FileEventType::Deleted: 
                event_type_str = "Deleted";
                break;
            default: 
                event_type_str = "Unknown";
                break;
        }
        
        std::cout << "  " << event_type_str << ": " << event.path;
        
        std::cout << std::endl;
    }
    
    if (events.empty()) {
        std::cout << "  (no events)" << std::endl;
    }
}

// Mock class implementations

// MockDatabase implementation
MockDatabase::MockDatabase() : AssetDatabase() {}

bool MockDatabase::initialize(const std::string& db_path) {
    return true;
}

bool MockDatabase::insert_assets_batch(std::vector<Asset>& assets) {
    // Simulate real database behavior by setting IDs
    for (auto& asset : assets) {
        asset.id = next_id_++;
    }
    inserted_assets.insert(inserted_assets.end(), assets.begin(), assets.end());
    return true;
}

bool MockDatabase::update_assets_batch(const std::vector<Asset>& assets) {
    updated_assets.insert(updated_assets.end(), assets.begin(), assets.end());
    return true;
}

bool MockDatabase::delete_assets_batch(const std::vector<std::string>& paths) {
    deleted_paths.insert(deleted_paths.end(), paths.begin(), paths.end());
    return true;
}

std::vector<Asset> MockDatabase::get_all_assets() {
    return {};
}

// MockTextureManager implementation
MockTextureManager::MockTextureManager() : TextureManager() {}

void MockTextureManager::generate_3d_model_thumbnail(const std::string& model_path, const fs::path& thumbnail_path) {
    generated_3d_thumbnails.push_back({model_path, thumbnail_path.string()});
}

void MockTextureManager::queue_texture_cleanup(const std::string& file_path) {
    cleanup_requests.push_back({file_path});
}

void MockTextureManager::generate_svg_thumbnail(const fs::path& svg_path, const fs::path& thumbnail_path) {
    generated_svg_thumbnails.push_back({svg_path.string(), thumbnail_path.string()});
}

// MockSearchIndex implementation
void MockSearchIndex::add_asset(uint32_t id, const Asset& asset) {
    added_assets.push_back({id, asset});
}

void MockSearchIndex::remove_asset(uint32_t id) {
    removed_ids.push_back(id);
}

void MockSearchIndex::update_asset(uint32_t id, const Asset& asset) {
    updated_assets.push_back({id, asset});
}

std::vector<uint32_t> MockSearchIndex::search(const std::string& query) {
    return {};
}

// Helper functions for temporary test files
fs::path create_temp_dir(const std::string& name) {
    fs::path temp_dir = fs::temp_directory_path() / name;
    fs::create_directories(temp_dir);
    return temp_dir;
}

fs::path create_temp_file(const fs::path& dir, const std::string& name, const std::string& content) {
    fs::path file_path = dir / name;
    std::ofstream file(file_path);
    file << content;
    file.close();
    return file_path;
}

void cleanup_temp_dir(const fs::path& dir) {
    std::error_code ec;
    fs::remove_all(dir, ec);
}
