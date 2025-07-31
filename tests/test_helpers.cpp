#include "test_helpers.h"
#include <filesystem>
#include <chrono>

Asset create_test_asset(
    const std::string& name,
    const std::string& extension,
    AssetType type,
    const std::string& path) {
    Asset asset;
    asset.name = name;
    asset.extension = extension;
    asset.type = type;
    asset.full_path = std::filesystem::path(path.empty() ? (name + extension) : path);
    asset.size = 1024; // Default size
    asset.last_modified = std::chrono::system_clock::now();
    asset.is_directory = false;
    return asset;
}