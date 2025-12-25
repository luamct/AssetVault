#pragma once

#include "asset.h"
#include "file_watcher.h"
#include "database.h"
#include "texture_manager.h"
#include "search.h"
#include <string>
#include <vector>
#include <filesystem>
#include <chrono>
#include <fstream>

// Test helper functions for creating mock assets and other test utilities
Asset create_test_asset(
    const std::string& relative_path,
    AssetType type = AssetType::Unknown,
    uint32_t id = 1);

// Helper function to print FileEvents for debugging tests
void print_file_events(const std::vector<FileEvent>& events, const std::string& label = "Events");

// Mock classes for testing
class MockDatabase : public AssetDatabase {
public:
    MockDatabase();

    bool initialize(const std::string& db_path = "test.db") override;
    bool insert_assets_batch(std::vector<Asset>& assets) override;
    bool update_assets_batch(const std::vector<Asset>& assets) override;
    bool delete_assets_batch(const std::vector<std::string>& paths) override;
    std::vector<Asset> get_all_assets() override;

    // Test access to mock data
    std::vector<Asset> inserted_assets;
    std::vector<Asset> updated_assets;
    std::vector<std::string> deleted_paths;
    uint32_t next_id_ = 1;
};

class MockTextureManager : public TextureManager {
public:
    MockTextureManager();

    // Override virtual methods
    void generate_3d_model_thumbnail(const std::string& model_path, const std::filesystem::path& thumbnail_path) override;
    void queue_texture_cleanup(const std::string& file_path) override;
    void generate_svg_thumbnail(const std::filesystem::path& svg_path, const std::filesystem::path& thumbnail_path) override;

    struct ThumbnailRequest {
        std::string model_path;
        std::string thumbnail_path;
    };

    struct SVGThumbnailRequest {
        std::string svg_path;
        std::string thumbnail_path;
    };

    struct CleanupRequest {
        std::string path;
    };

    std::vector<ThumbnailRequest> generated_3d_thumbnails;
    std::vector<SVGThumbnailRequest> generated_svg_thumbnails;
    std::vector<CleanupRequest> cleanup_requests;
};

class MockSearchIndex : public SearchIndex {
public:
    void add_asset(uint32_t id, const Asset& asset) override;
    void remove_asset(uint32_t id) override;
    void update_asset(uint32_t id, const Asset& asset) override;
    std::vector<uint32_t> search(const std::string& query);

    struct IndexEntry {
        uint32_t id;
        Asset asset;
    };

    std::vector<IndexEntry> added_assets;
    std::vector<uint32_t> removed_ids;
    std::vector<IndexEntry> updated_assets;
};

// Helper functions for managing temporary test files
std::filesystem::path create_temp_dir(const std::string& name = "assetinv_test");
std::filesystem::path create_temp_file(const std::filesystem::path& dir, const std::string& name, const std::string& content = "test content");
void cleanup_temp_dir(const std::filesystem::path& dir);
