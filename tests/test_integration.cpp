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

// RAII guard to ensure GLFW cleanup even if tests fail
// This is a safety net in case run() doesn't clean up properly
struct GLFWGuard {
    GLFWGuard() {
        // Don't init here - run() handles it
        // This is just a cleanup guard
    }

    ~GLFWGuard() {
        // Ensure GLFW is terminated even if tests fail
        // Safe to call even if not initialized
        glfwTerminate();
    }
};

TEST_CASE("Integration: Real application execution", "[integration]") {
    // NOTE: These integration tests are designed to run IN ORDER and depend on each other.
    // Each test builds on the state left by the previous test to minimize setup overhead.
    // DO NOT run tests in isolation or in random order.

    // Find test assets directory using source file location
    // __FILE__ can be absolute or relative depending on compiler/platform
    fs::path source_file_path = fs::path(__FILE__);

    // If __FILE__ is relative, make it absolute from current directory
    if (source_file_path.is_relative()) {
        source_file_path = fs::current_path() / source_file_path;
    }

    // Normalize path to resolve any .. components (cross-platform safe)
    source_file_path = source_file_path.lexically_normal();

    fs::path test_assets_source = source_file_path.parent_path() / "files" / "assets";
    REQUIRE(fs::exists(test_assets_source));

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
        GLFWGuard glfw_guard;  // Ensures cleanup even if test fails
        std::atomic<bool> shutdown_requested(false);
        bool test_passed = false;

        std::thread test_thread([&]{
            // Wait for app initialization and initial scan
            std::this_thread::sleep_for(std::chrono::milliseconds(500));

            // Verify all existing files were found and processed
            AssetDatabase verify_db;
            if (!verify_db.initialize(db_path_str)) {
                LOG_ERROR("[TEST] Failed to initialize database");
                shutdown_requested = true;
                return;
            }

            auto assets = verify_db.get_all_assets();
            LOG_INFO("[TEST] Found {} assets in database", assets.size());

            if (assets.size() != 5) {  // racer.fbx, racer.glb, racer.obj, racer.png, zombie.svg
                LOG_ERROR("[TEST] Expected 5 assets, got {}", assets.size());
                shutdown_requested = true;
                return;
            }

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

            if (!found_fbx || !found_glb || !found_obj || !found_png || !found_svg) {
                LOG_ERROR("[TEST] Missing expected asset types");
                shutdown_requested = true;
                return;
            }

            verify_db.close();

            LOG_INFO("[TEST] ✓ All existing files processed successfully");
            test_passed = true;
            shutdown_requested = true;
        });

        int result = run(&shutdown_requested);
        test_thread.join();

        // Now safe to use REQUIRE in main thread
        REQUIRE(result == 0);
        REQUIRE(test_passed);
    }

    SECTION("Loads database which already contains assets") {
        GLFWGuard glfw_guard;
        std::atomic<bool> shutdown_requested(false);
        bool test_passed = false;

        std::thread test_thread([&]{
            std::this_thread::sleep_for(std::chrono::milliseconds(300));

            // Verify it loaded the existing database
            AssetDatabase verify_db;
            if (!verify_db.initialize(db_path_str)) {
                LOG_ERROR("[TEST] Failed to initialize database");
                shutdown_requested = true;
                return;
            }

            auto assets = verify_db.get_all_assets();
            LOG_INFO("[TEST] Database loaded with {} existing assets", assets.size());

            if (assets.size() == 0) {
                LOG_ERROR("[TEST] Expected assets in database, got 0");
                shutdown_requested = true;
                return;
            }

            verify_db.close();

            LOG_INFO("[TEST] ✓ Successfully loaded existing database");
            test_passed = true;
            shutdown_requested = true;
        });

        int result = run(&shutdown_requested);
        test_thread.join();
        REQUIRE(result == 0);
        REQUIRE(test_passed);
    }

    SECTION("Adds assets added during execution") {
        GLFWGuard glfw_guard;
        std::atomic<bool> shutdown_requested(false);
        bool test_passed = false;

        std::thread test_thread([&]{
            // Wait for app initialization
            std::this_thread::sleep_for(std::chrono::milliseconds(300));

            AssetDatabase verify_db;
            if (!verify_db.initialize(db_path_str)) {
                LOG_ERROR("[TEST] Failed to initialize database");
                shutdown_requested = true;
                return;
            }
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
            if (!verify_db.initialize(db_path_str)) {
                LOG_ERROR("[TEST] Failed to initialize database");
                shutdown_requested = true;
                return;
            }

            auto assets = verify_db.get_all_assets();
            LOG_INFO("[TEST] Database now contains {} assets (expected {})", assets.size(), initial_count + 1);

            if (assets.size() != initial_count + 1) {
                LOG_ERROR("[TEST] Asset count mismatch");
                shutdown_requested = true;
                return;
            }

            bool found = false;
            for (const auto& asset : assets) {
                if (asset.name == "racer_copy.obj") {
                    found = true;
                    if (asset.type != AssetType::_3D) {
                        LOG_ERROR("[TEST] Asset type mismatch");
                        shutdown_requested = true;
                        return;
                    }
                    break;
                }
            }

            if (!found) {
                LOG_ERROR("[TEST] racer_copy.obj not found in database");
                shutdown_requested = true;
                return;
            }

            verify_db.close();

            // Cleanup test file
            fs::remove(test_file);

            LOG_INFO("[TEST] ✓ Asset added successfully during execution");
            test_passed = true;
            shutdown_requested = true;
        });

        int result = run(&shutdown_requested);
        test_thread.join();
        REQUIRE(result == 0);
        REQUIRE(test_passed);
    }

    SECTION("Removes assets deleted during execution") {
        GLFWGuard glfw_guard;
        std::atomic<bool> shutdown_requested(false);
        bool test_passed = false;

        std::thread test_thread([&]{
            std::this_thread::sleep_for(std::chrono::milliseconds(300));

            // Create a temporary file first
            fs::path test_file = assets_dir / "temp_delete_test.obj";
            fs::copy_file(assets_dir / "racer.obj", test_file);
            std::this_thread::sleep_for(std::chrono::milliseconds(500));

            AssetDatabase verify_db;
            if (!verify_db.initialize(db_path_str)) {
                LOG_ERROR("[TEST] Failed to initialize database");
                shutdown_requested = true;
                return;
            }
            size_t count_with_file = verify_db.get_all_assets().size();
            verify_db.close();

            // Delete the file
            LOG_INFO("[TEST] Deleting asset file...");
            fs::remove(test_file);
            std::this_thread::sleep_for(std::chrono::milliseconds(500));

            // Verify it was removed from database
            if (!verify_db.initialize(db_path_str)) {
                LOG_ERROR("[TEST] Failed to initialize database");
                shutdown_requested = true;
                return;
            }

            auto assets = verify_db.get_all_assets();
            LOG_INFO("[TEST] Database now contains {} assets (expected {})", assets.size(), count_with_file - 1);

            if (assets.size() != count_with_file - 1) {
                LOG_ERROR("[TEST] Asset count mismatch after deletion");
                shutdown_requested = true;
                return;
            }

            bool still_exists = false;
            for (const auto& asset : assets) {
                if (asset.name == "temp_delete_test.obj") {
                    still_exists = true;
                    break;
                }
            }

            if (still_exists) {
                LOG_ERROR("[TEST] temp_delete_test.obj still exists in database");
                shutdown_requested = true;
                return;
            }

            verify_db.close();

            LOG_INFO("[TEST] ✓ Asset removed successfully from database");
            test_passed = true;
            shutdown_requested = true;
        });

        int result = run(&shutdown_requested);
        test_thread.join();
        REQUIRE(result == 0);
        REQUIRE(test_passed);
    }

    SECTION("Creates thumbnails for 3D models") {
        GLFWGuard glfw_guard;
        std::atomic<bool> shutdown_requested(false);
        bool test_passed = false;

        std::thread test_thread([&]{
            std::this_thread::sleep_for(std::chrono::milliseconds(500));

            // Check that 3D model thumbnails were created
            fs::path thumbnail_dir = Config::get_thumbnail_directory();
            if (!fs::exists(thumbnail_dir)) {
                LOG_ERROR("[TEST] Thumbnail directory doesn't exist");
                shutdown_requested = true;
                return;
            }

            bool found_racer_thumb = false;
            LOG_INFO("[TEST] Checking thumbnails in: {}", thumbnail_dir.string());

            for (const auto& entry : fs::directory_iterator(thumbnail_dir)) {
                std::string filename = entry.path().filename().string();
                LOG_INFO("[TEST]   Found thumbnail: {}", filename);

                // All racer.* files share same thumbnail (racer.png)
                if (filename == "racer.png") {
                    found_racer_thumb = true;
                    // Verify it's a real file with content
                    if (fs::file_size(entry.path()) == 0) {
                        LOG_ERROR("[TEST] racer.png thumbnail is empty");
                        shutdown_requested = true;
                        return;
                    }
                }
            }

            if (!found_racer_thumb) {
                LOG_ERROR("[TEST] racer.png thumbnail not found");
                shutdown_requested = true;
                return;
            }

            LOG_INFO("[TEST] ✓ 3D model thumbnails created successfully");
            test_passed = true;
            shutdown_requested = true;
        });

        int result = run(&shutdown_requested);
        test_thread.join();
        REQUIRE(result == 0);
        REQUIRE(test_passed);
    }

    SECTION("Creates thumbnails for SVG files") {
        GLFWGuard glfw_guard;
        std::atomic<bool> shutdown_requested(false);
        bool test_passed = false;

        std::thread test_thread([&]{
            std::this_thread::sleep_for(std::chrono::milliseconds(500));

            // Check that SVG thumbnail was created
            fs::path thumbnail_dir = Config::get_thumbnail_directory();
            if (!fs::exists(thumbnail_dir)) {
                LOG_ERROR("[TEST] Thumbnail directory doesn't exist");
                shutdown_requested = true;
                return;
            }

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

            if (!found_svg_thumb) {
                LOG_ERROR("[TEST] SVG thumbnail (zombie*.png) not found");
                shutdown_requested = true;
                return;
            }

            LOG_INFO("[TEST] ✓ SVG thumbnail created successfully");
            test_passed = true;
            shutdown_requested = true;
        });

        int result = run(&shutdown_requested);
        test_thread.join();
        REQUIRE(result == 0);
        REQUIRE(test_passed);
    }

    // One-time teardown: Clean up test environment
    unsetenv("TESTING");
    if (fs::exists(data_dir)) {
        fs::remove_all(data_dir);
    }
}