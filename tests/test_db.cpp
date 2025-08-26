#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <vector>
#include <set>

#include "database.h"
#include "asset.h"

namespace fs = std::filesystem;

TEST_CASE("Database functionality", "[database]") {
    // Create test database
    fs::path test_db = fs::temp_directory_path() / "test_asset_id.db";
    
    // Clean up any existing test database
    fs::remove(test_db);
    
    AssetDatabase db;
    REQUIRE(db.initialize(test_db.string()));
    REQUIRE(db.create_tables());
    
    SECTION("Single asset insertion assigns ID") {
        Asset asset;
        asset.name = "test.txt";
        asset.extension = "txt";
        asset.full_path = "/path/to/test.txt";
        asset.size = 1024;
        asset.last_modified = std::chrono::system_clock::now();
        asset.is_directory = false;
        asset.type = AssetType::Document;
        
        // ID should be 0 before insertion
        REQUIRE(asset.id == 0);
        
        // Insert the asset
        REQUIRE(db.insert_asset(asset));
        
        // ID should be assigned after insertion
        REQUIRE(asset.id > 0);
        
        // Verify we can retrieve the asset by path and it has the same ID
        Asset retrieved = db.get_asset_by_path("/path/to/test.txt");
        REQUIRE(retrieved.id == asset.id);
        REQUIRE(retrieved.name == asset.name);
    }
    
    SECTION("Batch insertion assigns unique IDs") {
        std::vector<Asset> assets;
        
        for (int i = 1; i <= 5; i++) {
            Asset asset;
            asset.name = "file" + std::to_string(i) + ".txt";
            asset.extension = "txt";
            asset.full_path = "/path/to/file" + std::to_string(i) + ".txt";
            asset.size = 1024 * i;
            asset.last_modified = std::chrono::system_clock::now();
            asset.is_directory = false;
            asset.type = AssetType::Document;
            assets.push_back(asset);
        }
        
        // All IDs should be 0 before insertion
        for (const auto& asset : assets) {
            REQUIRE(asset.id == 0);
        }
        
        // Insert batch
        REQUIRE(db.insert_assets_batch(assets));
        
        // All assets should have unique IDs assigned
        std::set<uint32_t> unique_ids;
        for (const auto& asset : assets) {
            REQUIRE(asset.id > 0);
            unique_ids.insert(asset.id);
        }
        
        // Verify all IDs are unique
        REQUIRE(unique_ids.size() == assets.size());
        
        // Verify we can retrieve all assets and they have correct IDs
        auto all_assets = db.get_all_assets();
        REQUIRE(all_assets.size() == assets.size());
        
        for (const auto& retrieved : all_assets) {
            REQUIRE(retrieved.id > 0);
        }
    }
    
    SECTION("Update preserves existing ID") {
        Asset asset;
        asset.name = "original.txt";
        asset.extension = "txt";
        asset.full_path = "/path/to/original.txt";
        asset.size = 1024;
        asset.last_modified = std::chrono::system_clock::now();
        asset.is_directory = false;
        asset.type = AssetType::Document;
        
        // Insert the asset
        REQUIRE(db.insert_asset(asset));
        uint32_t original_id = asset.id;
        REQUIRE(original_id > 0);
        
        // Modify the asset
        asset.size = 2048;
        asset.name = "modified.txt";  // Note: path stays the same
        
        // Update the asset
        REQUIRE(db.update_asset(asset));
        
        // Retrieve and verify ID is preserved
        Asset retrieved = db.get_asset_by_path("/path/to/original.txt");
        REQUIRE(retrieved.id == original_id);
        REQUIRE(retrieved.size == 2048);
        REQUIRE(retrieved.name == "modified.txt");
    }
    
    SECTION("IDs are sequential and persistent") {
        // Insert first asset
        Asset asset1;
        asset1.name = "first.txt";
        asset1.extension = "txt";
        asset1.full_path = "/path/to/first.txt";
        asset1.size = 1024;
        asset1.last_modified = std::chrono::system_clock::now();
        asset1.is_directory = false;
        asset1.type = AssetType::Document;
        
        REQUIRE(db.insert_asset(asset1));
        uint32_t id1 = asset1.id;
        
        // Insert second asset
        Asset asset2;
        asset2.name = "second.txt";
        asset2.extension = "txt";
        asset2.full_path = "/path/to/second.txt";
        asset2.size = 2048;
        asset2.last_modified = std::chrono::system_clock::now();
        asset2.is_directory = false;
        asset2.type = AssetType::Document;
        
        REQUIRE(db.insert_asset(asset2));
        uint32_t id2 = asset2.id;
        
        // IDs should be sequential
        REQUIRE(id2 == id1 + 1);
        
        // Delete first asset
        REQUIRE(db.delete_asset("/path/to/first.txt"));
        
        // Insert third asset - should get next ID, not reuse deleted one
        Asset asset3;
        asset3.name = "third.txt";
        asset3.extension = "txt";
        asset3.full_path = "/path/to/third.txt";
        asset3.size = 3072;
        asset3.last_modified = std::chrono::system_clock::now();
        asset3.is_directory = false;
        asset3.type = AssetType::Document;
        
        REQUIRE(db.insert_asset(asset3));
        uint32_t id3 = asset3.id;
        
        // ID should continue sequentially, not reuse deleted ID
        REQUIRE(id3 == id2 + 1);
    }
    
    // Clean up
    db.close();
    fs::remove(test_db);
}

TEST_CASE("Asset type filtering helper function", "[asset][filtering]") {
    SECTION("should_skip_asset correctly identifies types to skip") {
        // Test ignored types (should return true - these should be skipped)
        REQUIRE(should_skip_asset(".txt"));    // Documents
        REQUIRE(should_skip_asset(".md"));     
        REQUIRE(should_skip_asset(".pdf"));    
        REQUIRE(should_skip_asset(".doc"));    
        
        REQUIRE(should_skip_asset(".mtl"));    // Auxiliary
        REQUIRE(should_skip_asset(".log"));    
        REQUIRE(should_skip_asset(".cache")); 
        REQUIRE(should_skip_asset(".tmp"));    
        REQUIRE(should_skip_asset(".bak"));    
        
        REQUIRE(should_skip_asset(""));        // No extension (Unknown)
        REQUIRE(should_skip_asset(".xyz"));    // Unknown extension
        
        // Case insensitive for ignored types too
        REQUIRE(should_skip_asset(".TXT"));
        REQUIRE(should_skip_asset(".MTL"));
    }
    
    SECTION("should_skip_asset correctly identifies processable types") {
        // Test processable types (should return false - these should NOT be skipped)
        REQUIRE_FALSE(should_skip_asset(".fbx"));    // 3D models
        REQUIRE_FALSE(should_skip_asset(".obj"));    
        REQUIRE_FALSE(should_skip_asset(".png"));    // 2D textures
        REQUIRE_FALSE(should_skip_asset(".jpg"));    
        REQUIRE_FALSE(should_skip_asset(".wav"));    // Audio
        REQUIRE_FALSE(should_skip_asset(".mp3"));    
        REQUIRE_FALSE(should_skip_asset(".ttf"));    // Fonts
        REQUIRE_FALSE(should_skip_asset(".glsl"));   // Shaders
        REQUIRE_FALSE(should_skip_asset(".zip"));    // Archives
        
        // Test case insensitive processing
        REQUIRE_FALSE(should_skip_asset(".FBX"));
        REQUIRE_FALSE(should_skip_asset(".PNG"));
        REQUIRE_FALSE(should_skip_asset(".WAV"));
    }
    
    SECTION("should_skip_asset handles edge cases") {
        // Empty strings and edge cases
        REQUIRE(should_skip_asset(""));        // Empty - should skip
        REQUIRE(should_skip_asset("noext"));   // No dot - should skip
        REQUIRE(should_skip_asset("."));       // Just dot - should skip
        
        // Very long extensions (should still work)
        REQUIRE(should_skip_asset(".verylongextension"));  // Unknown - should skip
    }
}