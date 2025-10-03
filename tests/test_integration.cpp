#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <iostream>
#include <thread>
#include <chrono>
#include <cstdlib>

#include <glad/glad.h>
#include <GLFW/glfw3.h>

// Windows compatibility for setenv/unsetenv
#ifdef _WIN32
#define setenv(name, value, overwrite) _putenv_s(name, value)
#define unsetenv(name) _putenv_s(name, "")
#endif

#include "database.h"
#include "event_processor.h"
#include "texture_manager.h"
#include "search.h"
#include "audio_manager.h"
#include "file_watcher.h"
#include "config.h"
#include "logger.h"
#include "ui.h"
#include "run.h"
#include "test_helpers.h"

namespace fs = std::filesystem;

TEST_CASE("Integration: Real application execution", "[integration]") {
    // NOTE: These integration tests are designed to run IN ORDER and depend on each other.
    // Each test builds on the state left by the previous test to minimize setup overhead.
    // DO NOT run tests in isolation or in random order.

    // Find test assets directory
    fs::path test_assets_source = fs::current_path() / "tests" / "files" / "assets";
    if (!fs::exists(test_assets_source)) {
        test_assets_source = fs::current_path().parent_path() / "tests" / "files" / "assets";
        REQUIRE(fs::exists(test_assets_source));
    }

    // One-time setup: Enable headless mode and configure database
    setenv("TESTING", "1", 1);

    fs::path data_dir = Config::get_data_directory();
    fs::path test_db = data_dir / "assets.db";
    if (fs::exists(test_db)) {
        fs::remove(test_db);
    }
    fs::create_directories(data_dir);

    std::string db_path_str = Config::get_database_path().string();
    std::string assets_directory = test_assets_source.string();
    fs::path assets_dir(assets_directory);

    {
        AssetDatabase setup_db;
        REQUIRE(setup_db.initialize(db_path_str));
        REQUIRE(setup_db.upsert_config_value(Config::CONFIG_KEY_ASSETS_DIRECTORY, assets_directory));
        setup_db.close();
    }

    SECTION("Processes files already in folder at start") {
        std::atomic<bool> shutdown_requested(false);

        std::thread test_thread([&]{
            // Wait for app initialization and initial scan
            std::this_thread::sleep_for(std::chrono::milliseconds(500));

            // Verify all existing files were found and processed
            AssetDatabase verify_db;
            REQUIRE(verify_db.initialize(db_path_str));
            auto assets = verify_db.get_all_assets();

            LOG_INFO("[TEST] Found {} assets in database", assets.size());
            REQUIRE(assets.size() == 5);  // racer.fbx, racer.glb, racer.obj, racer.png, zombie.svg

            // Verify we found expected asset types
            bool found_fbx = false, found_glb = false, found_obj = false;
            bool found_png = false, found_svg = false;

            for (const auto& asset : assets) {
                if (asset.extension == ".fbx") found_fbx = true;
                if (asset.extension == ".glb") found_glb = true;
                if (asset.extension == ".obj") found_obj = true;
                if (asset.extension == ".png") found_png = true;
                if (asset.extension == ".svg") found_svg = true;
            }

            REQUIRE(found_fbx);
            REQUIRE(found_glb);
            REQUIRE(found_obj);
            REQUIRE(found_png);
            REQUIRE(found_svg);

            verify_db.close();

            LOG_INFO("[TEST] ✓ All existing files processed successfully");
            shutdown_requested = true;
        });

        int result = run(&shutdown_requested);
        test_thread.join();
        REQUIRE(result == 0);
    }

    SECTION("Loads database which already contains assets") {
        // Database already has assets from previous test - just verify it loads them
        std::atomic<bool> shutdown_requested(false);

        std::thread test_thread([&]{
            std::this_thread::sleep_for(std::chrono::milliseconds(300));

            // Verify it loaded the existing database
            AssetDatabase verify_db;
            REQUIRE(verify_db.initialize(db_path_str));
            auto assets = verify_db.get_all_assets();

            LOG_INFO("[TEST] Database loaded with {} existing assets", assets.size());
            REQUIRE(assets.size() > 0);  // Should have pre-populated asset plus scanned ones
            verify_db.close();

            LOG_INFO("[TEST] ✓ Successfully loaded existing database");
            shutdown_requested = true;
        });

        int result = run(&shutdown_requested);
        test_thread.join();
        REQUIRE(result == 0);
    }

    SECTION("Adds assets added during execution") {
        // Database and assets directory already configured from previous tests
        std::atomic<bool> shutdown_requested(false);

        std::thread test_thread([&]{
            // Wait for app initialization
            std::this_thread::sleep_for(std::chrono::milliseconds(300));

            AssetDatabase verify_db;
            REQUIRE(verify_db.initialize(db_path_str));
            size_t initial_count = verify_db.get_all_assets().size();
            verify_db.close();

            // Copy an existing asset to create a "new" file
            LOG_INFO("[TEST] Adding new asset file...");
            fs::path source_file = assets_dir / "racer.obj";
            fs::path test_file = assets_dir / "racer_copy.obj";

            if (fs::exists(test_file)) {
                fs::remove(test_file);
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }

            fs::copy_file(source_file, test_file);

            // Wait for FileWatcher and EventProcessor
            std::this_thread::sleep_for(std::chrono::milliseconds(500));

            // Verify asset was added
            REQUIRE(verify_db.initialize(db_path_str));
            auto assets = verify_db.get_all_assets();
            LOG_INFO("[TEST] Database now contains {} assets (expected {})", assets.size(), initial_count + 1);
            REQUIRE(assets.size() == initial_count + 1);

            bool found = false;
            for (const auto& asset : assets) {
                if (asset.name == "racer_copy.obj") {
                    found = true;
                    REQUIRE(asset.type == AssetType::_3D);
                    break;
                }
            }
            REQUIRE(found);
            verify_db.close();

            // Cleanup test file
            fs::remove(test_file);

            LOG_INFO("[TEST] ✓ Asset added successfully during execution");
            shutdown_requested = true;
        });

        int result = run(&shutdown_requested);
        test_thread.join();
        REQUIRE(result == 0);
    }

    SECTION("Removes assets deleted during execution") {
        std::atomic<bool> shutdown_requested(false);

        std::thread test_thread([&]{
            std::this_thread::sleep_for(std::chrono::milliseconds(300));

            // Create a temporary file first
            fs::path test_file = assets_dir / "temp_delete_test.obj";
            fs::copy_file(assets_dir / "racer.obj", test_file);
            std::this_thread::sleep_for(std::chrono::milliseconds(500));

            AssetDatabase verify_db;
            REQUIRE(verify_db.initialize(db_path_str));
            size_t count_with_file = verify_db.get_all_assets().size();
            verify_db.close();

            // Delete the file
            LOG_INFO("[TEST] Deleting asset file...");
            fs::remove(test_file);
            std::this_thread::sleep_for(std::chrono::milliseconds(500));

            // Verify it was removed from database
            REQUIRE(verify_db.initialize(db_path_str));
            auto assets = verify_db.get_all_assets();
            LOG_INFO("[TEST] Database now contains {} assets (expected {})", assets.size(), count_with_file - 1);
            REQUIRE(assets.size() == count_with_file - 1);

            bool still_exists = false;
            for (const auto& asset : assets) {
                if (asset.name == "temp_delete_test.obj") {
                    still_exists = true;
                    break;
                }
            }
            REQUIRE(!still_exists);
            verify_db.close();

            LOG_INFO("[TEST] ✓ Asset removed successfully from database");
            shutdown_requested = true;
        });

        int result = run(&shutdown_requested);
        test_thread.join();
        REQUIRE(result == 0);
    }

    SECTION("Creates thumbnails for 3D models") {
        // Thumbnails already created from previous tests
        std::atomic<bool> shutdown_requested(false);

        std::thread test_thread([&]{
            std::this_thread::sleep_for(std::chrono::milliseconds(500));

            // Check that 3D model thumbnails were created
            fs::path thumbnail_dir = Config::get_thumbnail_directory();
            REQUIRE(fs::exists(thumbnail_dir));

            bool found_racer_thumb = false;
            LOG_INFO("[TEST] Checking thumbnails in: {}", thumbnail_dir.string());

            for (const auto& entry : fs::directory_iterator(thumbnail_dir)) {
                std::string filename = entry.path().filename().string();
                LOG_INFO("[TEST]   Found thumbnail: {}", filename);

                // All racer.* files share same thumbnail (racer.png)
                if (filename == "racer.png") {
                    found_racer_thumb = true;
                    // Verify it's a real file with content
                    REQUIRE(fs::file_size(entry.path()) > 0);
                }
            }

            REQUIRE(found_racer_thumb);

            LOG_INFO("[TEST] ✓ 3D model thumbnails created successfully");
            shutdown_requested = true;
        });

        int result = run(&shutdown_requested);
        test_thread.join();
        REQUIRE(result == 0);
    }

    SECTION("Creates thumbnails for SVG files") {
        // SVG thumbnail already created from first test
        std::atomic<bool> shutdown_requested(false);

        std::thread test_thread([&]{
            std::this_thread::sleep_for(std::chrono::milliseconds(500));

            // Check that SVG thumbnail was created
            fs::path thumbnail_dir = Config::get_thumbnail_directory();
            REQUIRE(fs::exists(thumbnail_dir));

            bool found_svg_thumb = false;

            for (const auto& entry : fs::directory_iterator(thumbnail_dir)) {
                std::string filename = entry.path().filename().string();
                LOG_INFO("[TEST] Checking file: {}", filename);
                // SVG thumbnails are rasterized to PNG
                if (filename.find("zombie") != std::string::npos && filename.find(".png") != std::string::npos) {
                    found_svg_thumb = true;
                    LOG_INFO("[TEST] ✓ Found SVG thumbnail: {}", filename);
                }
            }

            REQUIRE(found_svg_thumb);

            LOG_INFO("[TEST] ✓ SVG thumbnail created successfully");
            shutdown_requested = true;
        });

        int result = run(&shutdown_requested);
        test_thread.join();
        REQUIRE(result == 0);
    }

    // One-time teardown: Clean up test environment
    unsetenv("TESTING");
    if (fs::exists(data_dir)) {
        fs::remove_all(data_dir);
    }
}