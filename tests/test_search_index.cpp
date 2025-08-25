#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <vector>

#include "search.h"
#include "database.h"
#include "asset.h"

namespace fs = std::filesystem;

TEST_CASE("Search index functionality", "[search][index]") {
    // Create test database
    fs::path test_db = fs::temp_directory_path() / "test_search_index.db";
    
    // Clean up any existing test database
    fs::remove(test_db);
    
    AssetDatabase db;
    REQUIRE(db.initialize(test_db.string()));
    REQUIRE(db.create_tables());
    
    // Create search index
    SearchIndex index(&db);
    
    SECTION("Tokenization works correctly") {
        Asset asset;
        asset.id = 1;
        asset.name = "MyTexture_diffuse.png";
        asset.extension = "png";
        asset.full_path = "/assets/textures/MyTexture_diffuse.png";
        asset.size = 1024;
        asset.last_modified = std::chrono::system_clock::now();
        asset.is_directory = false;
        asset.type = AssetType::_2D;
        
        // Insert asset into database first
        REQUIRE(db.insert_asset(asset));
        
        // Build index
        REQUIRE(index.build_from_database());
        
        // Test searches for different tokens
        auto results = index.search_prefix("mytexture");
        REQUIRE(!results.empty());
        REQUIRE(results[0] == asset.id);
        
        results = index.search_prefix("diffuse");
        REQUIRE(!results.empty());
        REQUIRE(results[0] == asset.id);
        
        results = index.search_prefix("png");
        REQUIRE(!results.empty());
        REQUIRE(results[0] == asset.id);
        
        results = index.search_prefix("texture");  // Part of "textures" directory
        REQUIRE(!results.empty());
        REQUIRE(results[0] == asset.id);
        
        // Test short queries are ignored
        results = index.search_prefix("my");  // <= 2 chars
        REQUIRE(results.empty());
    }
    
    SECTION("Multi-term search works correctly") {
        // Add multiple assets
        std::vector<Asset> assets;
        
        Asset asset1;
        asset1.name = "grass_texture.png";
        asset1.extension = "png";
        asset1.full_path = "/assets/nature/grass_texture.png";
        asset1.size = 1024;
        asset1.last_modified = std::chrono::system_clock::now();
        asset1.is_directory = false;
        asset1.type = AssetType::_2D;
        assets.push_back(asset1);
        
        Asset asset2;
        asset2.name = "rock_texture.jpg";
        asset2.extension = "jpg";
        asset2.full_path = "/assets/nature/rock_texture.jpg";
        asset2.size = 2048;
        asset2.last_modified = std::chrono::system_clock::now();
        asset2.is_directory = false;
        asset2.type = AssetType::_2D;
        assets.push_back(asset2);
        
        Asset asset3;
        asset3.name = "player_model.fbx";
        asset3.extension = "fbx";
        asset3.full_path = "/assets/models/player_model.fbx";
        asset3.size = 5120;
        asset3.last_modified = std::chrono::system_clock::now();
        asset3.is_directory = false;
        asset3.type = AssetType::_3D;
        assets.push_back(asset3);
        
        // Insert all assets
        REQUIRE(db.insert_assets_batch(assets));
        
        // Build index
        REQUIRE(index.build_from_database());
        
        // Test single term searches
        auto results = index.search_prefix("texture");
        REQUIRE(results.size() == 2);  // grass_texture and rock_texture
        
        results = index.search_prefix("nature");
        REQUIRE(results.size() == 2);  // Both in nature directory
        
        results = index.search_prefix("player");
        REQUIRE(results.size() == 1);  // Only player_model
        
        // Test multi-term searches (AND logic)
        std::vector<std::string> terms = {"texture", "nature"};
        results = index.search_terms(terms);
        REQUIRE(results.size() == 2);  // Both nature textures
        
        terms = {"grass", "texture"};
        results = index.search_terms(terms);
        REQUIRE(results.size() == 1);  // Only grass_texture
        
        terms = {"player", "texture"};
        results = index.search_terms(terms);
        REQUIRE(results.empty());  // No asset has both terms
    }
    
    SECTION("Prefix matching works correctly") {
        Asset asset;
        asset.name = "awesome_background.png";
        asset.extension = "png";
        asset.full_path = "/assets/ui/awesome_background.png";
        asset.size = 1024;
        asset.last_modified = std::chrono::system_clock::now();
        asset.is_directory = false;
        asset.type = AssetType::_2D;
        
        REQUIRE(db.insert_asset(asset));
        REQUIRE(index.build_from_database());
        
        // Test prefix matching
        auto results = index.search_prefix("awe");  // Prefix of "awesome"
        REQUIRE(!results.empty());
        REQUIRE(results[0] == asset.id);
        
        results = index.search_prefix("awesome");  // Exact match
        REQUIRE(!results.empty());
        REQUIRE(results[0] == asset.id);
        
        results = index.search_prefix("back");  // Prefix of "background"
        REQUIRE(!results.empty());
        REQUIRE(results[0] == asset.id);
        
        results = index.search_prefix("xyz");  // No match
        REQUIRE(results.empty());
    }
    
    SECTION("Index statistics work correctly") {
        // Add a few assets
        Asset asset1;
        asset1.name = "test1.png";
        asset1.extension = "png";
        asset1.full_path = "/assets/test1.png";
        asset1.size = 1024;
        asset1.last_modified = std::chrono::system_clock::now();
        asset1.is_directory = false;
        asset1.type = AssetType::_2D;
        
        Asset asset2;
        asset2.name = "test2.jpg";
        asset2.extension = "jpg";
        asset2.full_path = "/assets/images/test2.jpg";
        asset2.size = 2048;
        asset2.last_modified = std::chrono::system_clock::now();
        asset2.is_directory = false;
        asset2.type = AssetType::_2D;
        
        std::vector<Asset> assets = {asset1, asset2};
        REQUIRE(db.insert_assets_batch(assets));
        REQUIRE(index.build_from_database());
        
        // Check statistics
        REQUIRE(index.get_token_count() > 0);
        REQUIRE(index.get_memory_usage() > 0);
        
        // Clear and verify
        index.clear();
        REQUIRE(index.get_token_count() == 0);
    }
    
    // Clean up
    db.close();
    fs::remove(test_db);
}