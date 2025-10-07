#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <vector>
#include <unordered_map>
#include <atomic>
#include <mutex>
#include <chrono>
#include <set>

#include "event_processor.h"
#include "database.h"
#include "search.h"
#include "texture_manager.h"
#include "file_watcher.h"
#include "asset.h"
#include "utils.h"
#include "config.h"
#include "test_helpers.h"
#include "services.h"

namespace fs = std::filesystem;

// Test cases for process_created_events method
TEST_CASE("process_created_events functionality", "[process_created_events]") {
    // Setup test environment
    fs::path temp_dir = create_temp_dir("test_basic");
    std::string assets_dir = temp_dir.string();

    // Create mocks
    MockDatabase db;
    MockTextureManager texture_mgr;
    MockSearchIndex search_idx;
    SafeAssets safe_assets;
    std::atomic<bool> search_update{false};

    // Create EventProcessor with mocks
    EventProcessor processor(safe_assets, search_update, assets_dir);

    // Register services for testing
    Services::provide(&db, &search_idx, &processor, nullptr, &texture_mgr, nullptr);

    SECTION("Process single created event") {
        // Create a test file
        auto test_file = create_temp_file(temp_dir, "test_model.fbx", "FBX model data");

        // Create FileEvent
        std::vector<FileEvent> events = {
            FileEvent(FileEventType::Created, test_file.generic_u8string())
        };

        // Process the events directly
        processor.process_created_events(events);

        // Verify asset was inserted into database
        REQUIRE(db.inserted_assets.size() == 1);

        const Asset& asset = db.inserted_assets[0];

        // Verify basic properties
        REQUIRE(asset.path == test_file.generic_u8string());
        REQUIRE(asset.name == "test_model.fbx");
        REQUIRE(asset.extension == ".fbx");
        REQUIRE(asset.type == AssetType::_3D);
        REQUIRE(asset.size == 14); // "FBX model data" is 14 bytes

        // Verify 3D thumbnail was generated
        REQUIRE(texture_mgr.generated_3d_thumbnails.size() == 1);
        REQUIRE(texture_mgr.generated_3d_thumbnails[0].model_path == test_file.generic_u8string());

        // Verify asset was added to search index
        REQUIRE(search_idx.added_assets.size() == 1);
        REQUIRE(search_idx.added_assets[0].asset.path == asset.path);

        // Verify asset was added to in-memory map
        {
            auto [lock, assets] = safe_assets.read();
            REQUIRE(assets.size() == 1);
            REQUIRE(assets.count(asset.path) == 1);
        }
    }

    SECTION("Process multiple created events") {
        // Create multiple test files including UTF-8 filenames
        auto fbx_file = create_temp_file(temp_dir, "model.fbx", "3D model");
        auto png_file = create_temp_file(temp_dir, "texture.png", "image data");
        auto svg_file = create_temp_file(temp_dir, "icon.svg", "<svg></svg>");
        auto utf8_file = create_temp_file(temp_dir, "日本語.obj", "Japanese model"); // Japanese filename
        auto russian_file = create_temp_file(temp_dir, "файл.mp3", "Russian audio"); // Russian filename

        std::vector<FileEvent> events = {
            FileEvent(FileEventType::Created, fbx_file.generic_u8string()),
            FileEvent(FileEventType::Created, png_file.generic_u8string()),
            FileEvent(FileEventType::Created, svg_file.generic_u8string()),
            FileEvent(FileEventType::Created, utf8_file.generic_u8string()),
            FileEvent(FileEventType::Created, russian_file.generic_u8string())
        };

        // Process all events
        processor.process_created_events(events);

        // Verify all assets were inserted
        REQUIRE(db.inserted_assets.size() == 5);

        // Verify correct types
        std::map<std::string, AssetType> expected_types = {
            {fbx_file.generic_u8string(), AssetType::_3D},
            {png_file.generic_u8string(), AssetType::_2D},
            {svg_file.generic_u8string(), AssetType::_2D},
            {utf8_file.generic_u8string(), AssetType::_3D},
            {russian_file.generic_u8string(), AssetType::Audio}
        };

        for (const auto& asset : db.inserted_assets) {
            REQUIRE(expected_types.count(asset.path) == 1);
            REQUIRE(asset.type == expected_types[asset.path]);
        }

        // Verify 3D thumbnails were generated for both 3D models
        REQUIRE(texture_mgr.generated_3d_thumbnails.size() == 2);
        std::set<std::string> generated_3d_paths;
        for (const auto& thumb : texture_mgr.generated_3d_thumbnails) {
            generated_3d_paths.insert(thumb.model_path);
        }
        REQUIRE(generated_3d_paths.count(fbx_file.generic_u8string()) == 1);
        REQUIRE(generated_3d_paths.count(utf8_file.generic_u8string()) == 1);

        // Verify SVG thumbnail was generated only for SVG
        REQUIRE(texture_mgr.generated_svg_thumbnails.size() == 1);
        REQUIRE(texture_mgr.generated_svg_thumbnails[0].svg_path == svg_file.generic_u8string());
    }

    SECTION("Process non-existent file with retry") {
        // Create event for non-existent file
        std::string non_existent = (temp_dir / "missing.png").string();

        std::vector<FileEvent> events = {
            FileEvent(FileEventType::Created, non_existent)
        };

        // Process the event
        processor.process_created_events(events);

        // Should not insert anything since file doesn't exist
        REQUIRE(db.inserted_assets.size() == 0);

        // No assets should be in memory either
        {
            auto [lock, assets] = safe_assets.read();
            REQUIRE(assets.size() == 0);
        }
    }

    SECTION("Process empty event list") {
        std::vector<FileEvent> events;

        // Should not crash or cause issues
        processor.process_created_events(events);

        REQUIRE(db.inserted_assets.size() == 0);
    }

    cleanup_temp_dir(temp_dir);
}

TEST_CASE("process_created_events thumbnail generation", "[process_created_events]") {
    // Setup test environment
    fs::path temp_dir = create_temp_dir("test_thumbnails");
    std::string assets_dir = temp_dir.string();

    // Create mocks
    MockDatabase db;
    MockTextureManager texture_mgr;
    MockSearchIndex search_idx;
    SafeAssets safe_assets;
    std::atomic<bool> search_update{false};

    // Create EventProcessor with mocks
    EventProcessor processor(safe_assets, search_update, assets_dir);

    // Register services for testing
    Services::provide(&db, &search_idx, &processor, nullptr, &texture_mgr, nullptr);

    SECTION("Generate 3D thumbnails for 3D models") {
        auto fbx_file = create_temp_file(temp_dir, "model.fbx", "3D model data");
        auto obj_file = create_temp_file(temp_dir, "model.obj", "OBJ model data");

        std::vector<FileEvent> events = {
            FileEvent(FileEventType::Created, fbx_file.generic_u8string()),
            FileEvent(FileEventType::Created, obj_file.generic_u8string())
        };

        processor.process_created_events(events);

        // Verify 3D thumbnails were generated for both models
        REQUIRE(texture_mgr.generated_3d_thumbnails.size() == 2);

        std::set<std::string> generated_paths;
        for (const auto& thumb : texture_mgr.generated_3d_thumbnails) {
            generated_paths.insert(thumb.model_path);
        }

        REQUIRE(generated_paths.count(fbx_file.generic_u8string()) == 1);
        REQUIRE(generated_paths.count(obj_file.generic_u8string()) == 1);
    }

    SECTION("No thumbnails for non-3D assets") {
        auto png_file = create_temp_file(temp_dir, "image.png", "image data");
        auto txt_file = create_temp_file(temp_dir, "doc.txt", "text content");

        std::vector<FileEvent> events = {
            FileEvent(FileEventType::Created, png_file.generic_u8string()),
            FileEvent(FileEventType::Created, txt_file.generic_u8string())
        };

        processor.process_created_events(events);

        // No 3D thumbnails should be generated
        REQUIRE(texture_mgr.generated_3d_thumbnails.size() == 0);

        // No SVG thumbnails should be generated
        REQUIRE(texture_mgr.generated_svg_thumbnails.size() == 0);

        // But assets should still be processed
        REQUIRE(db.inserted_assets.size() == 2);
    }

    SECTION("Generate SVG thumbnails for SVG files") {
        auto svg1_file = create_temp_file(temp_dir, "icon1.svg", "<svg></svg>");
        auto svg2_file = create_temp_file(temp_dir, "logo.svg", "<svg><circle r='10'/></svg>");

        std::vector<FileEvent> events = {
            FileEvent(FileEventType::Created, svg1_file.generic_u8string()),
            FileEvent(FileEventType::Created, svg2_file.generic_u8string())
        };

        processor.process_created_events(events);

        // Verify SVG thumbnails were generated for both files
        REQUIRE(texture_mgr.generated_svg_thumbnails.size() == 2);

        std::set<std::string> generated_svg_paths;
        for (const auto& svg_thumb : texture_mgr.generated_svg_thumbnails) {
            generated_svg_paths.insert(svg_thumb.svg_path);
        }

        REQUIRE(generated_svg_paths.count(svg1_file.generic_u8string()) == 1);
        REQUIRE(generated_svg_paths.count(svg2_file.generic_u8string()) == 1);

        // No 3D thumbnails should be generated
        REQUIRE(texture_mgr.generated_3d_thumbnails.size() == 0);

        // Assets should still be processed
        REQUIRE(db.inserted_assets.size() == 2);
    }

    cleanup_temp_dir(temp_dir);
}

TEST_CASE("process_deleted_events functionality", "[process_deleted_events]") {
    // Setup test environment
    fs::path temp_dir = create_temp_dir("test_deleted");
    std::string assets_dir = temp_dir.string();

    // Create mocks
    MockDatabase db;
    MockTextureManager texture_mgr;
    MockSearchIndex search_idx;
    SafeAssets safe_assets;
    std::atomic<bool> search_update{false};

    // Create EventProcessor with mocks
    EventProcessor processor(safe_assets, search_update, assets_dir);

    // Register services for testing
    Services::provide(&db, &search_idx, &processor, nullptr, &texture_mgr, nullptr);

    SECTION("Delete existing assets") {
        // Pre-populate assets map with test assets
        Asset asset1 = create_test_asset("model.fbx", ".fbx", AssetType::_3D,
                                        (temp_dir / "model.fbx").string(), assets_dir, 1);

        Asset asset2 = create_test_asset("texture.png", ".png", AssetType::_2D,
                                        (temp_dir / "texture.png").string(), assets_dir, 2);

        {
            auto [lock, assets] = safe_assets.write();
            assets[asset1.path] = asset1;
            assets[asset2.path] = asset2;
        }

        // Create deletion events
        std::vector<FileEvent> events = {
            FileEvent(FileEventType::Deleted, asset1.path),
            FileEvent(FileEventType::Deleted, asset2.path)
        };

        // Process deletion events
        processor.process_deleted_events(events);

        // Verify assets were removed from in-memory map
        {
            auto [lock, assets] = safe_assets.read();
            REQUIRE(assets.size() == 0);
        }

        // Verify database deletion was called
        REQUIRE(db.deleted_paths.size() == 2);
        REQUIRE(db.deleted_paths[0] == asset1.path);
        REQUIRE(db.deleted_paths[1] == asset2.path);

        // Verify search index removal was called
        REQUIRE(search_idx.removed_ids.size() == 2);
        REQUIRE(search_idx.removed_ids[0] == 1);
        REQUIRE(search_idx.removed_ids[1] == 2);

        // Verify texture cleanup was requested
        REQUIRE(texture_mgr.cleanup_requests.size() == 2);
        REQUIRE(texture_mgr.cleanup_requests[0].path == asset1.path);
        REQUIRE(texture_mgr.cleanup_requests[1].path == asset2.path);
    }

    SECTION("Delete non-existent assets") {
        // Create deletion events for assets not in the map
        std::string non_existent1 = (temp_dir / "missing1.jpg").string();
        std::string non_existent2 = (temp_dir / "missing2.obj").string();

        std::vector<FileEvent> events = {
            FileEvent(FileEventType::Deleted, non_existent1),
            FileEvent(FileEventType::Deleted, non_existent2)
        };

        // Process deletion events
        processor.process_deleted_events(events);

        // Assets map should remain empty
        {
            auto [lock, assets] = safe_assets.read();
            REQUIRE(assets.size() == 0);
        }

        // Database deletion should still be called (paths are passed regardless)
        REQUIRE(db.deleted_paths.size() == 2);
        REQUIRE(db.deleted_paths[0] == non_existent1);
        REQUIRE(db.deleted_paths[1] == non_existent2);

        // No search index removals since assets didn't exist
        REQUIRE(search_idx.removed_ids.size() == 0);

        // Texture cleanup should still be requested
        REQUIRE(texture_mgr.cleanup_requests.size() == 2);
        REQUIRE(texture_mgr.cleanup_requests[0].path == non_existent1);
        REQUIRE(texture_mgr.cleanup_requests[1].path == non_existent2);
    }

    SECTION("Empty events list") {
        std::vector<FileEvent> events;

        // Should not crash or cause issues
        processor.process_deleted_events(events);

        REQUIRE(db.deleted_paths.size() == 0);
        REQUIRE(search_idx.removed_ids.size() == 0);
        REQUIRE(texture_mgr.cleanup_requests.size() == 0);
    }

    cleanup_temp_dir(temp_dir);
}