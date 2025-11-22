#include <catch2/catch_all.hpp>
#include <map>
#include <filesystem>
#include <chrono>
#include "utils.h"
#include "asset.h" 
#include "test_helpers.h"
#include "ui/ui.h"

namespace fs = std::filesystem;

TEST_CASE("find_assets_under_directory optimization", "[utils][performance]") {
    // Create test asset map with paths
    std::map<std::string, Asset> test_assets;
    
    // Add assets with various paths in sorted order
    test_assets["/assets/models/character.fbx"] = create_test_asset("character", ".fbx", AssetType::_3D, "/assets/models/character.fbx");
    test_assets["/assets/models/weapons/sword.obj"] = create_test_asset("sword", ".obj", AssetType::_3D, "/assets/models/weapons/sword.obj");
    test_assets["/assets/textures/brick.png"] = create_test_asset("brick", ".png", AssetType::_2D, "/assets/textures/brick.png");
    test_assets["/assets/textures/ui/button.png"] = create_test_asset("button", ".png", AssetType::_2D, "/assets/textures/ui/button.png");
    test_assets["/assets/textures/ui/icons/health.png"] = create_test_asset("health", ".png", AssetType::_2D, "/assets/textures/ui/icons/health.png");
    test_assets["/other/sounds/explosion.wav"] = create_test_asset("explosion", ".wav", AssetType::Audio, "/other/sounds/explosion.wav");
    
    SECTION("Find assets under specific directory") {
        auto results = find_assets_under_directory(test_assets, fs::path("/assets/textures"));
        
        REQUIRE(results.size() == 3);
        REQUIRE(std::find(results.begin(), results.end(), fs::path("/assets/textures/brick.png")) != results.end());
        REQUIRE(std::find(results.begin(), results.end(), fs::path("/assets/textures/ui/button.png")) != results.end());
        REQUIRE(std::find(results.begin(), results.end(), fs::path("/assets/textures/ui/icons/health.png")) != results.end());
    }
    
    SECTION("Find assets under nested subdirectory") {
        auto results = find_assets_under_directory(test_assets, fs::path("/assets/textures/ui"));
        
        REQUIRE(results.size() == 2);
        REQUIRE(std::find(results.begin(), results.end(), fs::path("/assets/textures/ui/button.png")) != results.end());
        REQUIRE(std::find(results.begin(), results.end(), fs::path("/assets/textures/ui/icons/health.png")) != results.end());
    }
    
    SECTION("Find assets under directory with single file") {
        auto results = find_assets_under_directory(test_assets, fs::path("/assets/models/weapons"));
        
        REQUIRE(results.size() == 1);
        REQUIRE(results[0] == fs::path("/assets/models/weapons/sword.obj"));
    }
    
    SECTION("Find assets under non-existent directory") {
        auto results = find_assets_under_directory(test_assets, fs::path("/non-existent"));
        
        REQUIRE(results.empty());
    }
    
    SECTION("Find assets under root returns all") {
        auto results = find_assets_under_directory(test_assets, fs::path("/assets"));
        
        REQUIRE(results.size() == 5);  // All assets except /other/sounds/explosion.wav
    }
    
    SECTION("Empty asset map returns empty results") {
        std::map<std::string, Asset> empty_assets;
        auto results = find_assets_under_directory(empty_assets, fs::path("/assets"));
        
        REQUIRE(results.empty());
    }
    
    SECTION("Directory path without trailing slash works") {
        auto results = find_assets_under_directory(test_assets, fs::path("/assets/models"));
        
        REQUIRE(results.size() == 2);
        REQUIRE(std::find(results.begin(), results.end(), fs::path("/assets/models/character.fbx")) != results.end());
        REQUIRE(std::find(results.begin(), results.end(), fs::path("/assets/models/weapons/sword.obj")) != results.end());
    }
    
    SECTION("Performance test - should use binary search") {
        // Create larger dataset to verify O(log n) behavior
        std::map<std::string, Asset> large_assets;
        
        // Add 1000 assets with different prefixes  
        for (int i = 0; i < 1000; ++i) {
            std::string path = "/prefix" + std::to_string(i % 10) + "/file" + std::to_string(i) + ".png";
            large_assets[path] = create_test_asset("file" + std::to_string(i), ".png", AssetType::_2D, path);
        }
        
        // Find assets under /prefix5/ - should be fast O(log n + k) where k ~= 100
        auto start_time = std::chrono::high_resolution_clock::now();
        auto results = find_assets_under_directory(large_assets, fs::path("/prefix5"));
        auto end_time = std::chrono::high_resolution_clock::now();
        
        // Should find approximately 100 assets (1000/10)
        REQUIRE(results.size() >= 90);
        REQUIRE(results.size() <= 110);
        
        // Should complete very quickly (under 1ms for this size)
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
        REQUIRE(duration.count() < 1000);  // Less than 1ms
    }
}
