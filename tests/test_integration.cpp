#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <iostream>


#include <glad/glad.h>
#include <GLFW/glfw3.h>

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
    // Find test assets directory
    fs::path test_assets_source = fs::current_path() / "tests" / "files" / "assets";
    if (!fs::exists(test_assets_source)) {
        test_assets_source = fs::current_path().parent_path() / "tests" / "files" / "assets";
        REQUIRE(fs::exists(test_assets_source));
    }

    SECTION("Basic run() execution with pre-populated assets") {
        // Create temporary folder with test assets already in place
        fs::path temp_assets_dir = fs::temp_directory_path() / "AssetInventory_integration_test";
        if (fs::exists(temp_assets_dir)) {
            fs::remove_all(temp_assets_dir);
        }
        fs::create_directories(temp_assets_dir);

        // Copy test assets before starting the application
        LOG_INFO("Copying test assets to temporary directory...");
        fs::copy(test_assets_source, temp_assets_dir, fs::copy_options::recursive);
        LOG_INFO("Copied test assets to: {}", temp_assets_dir.string());

        // Set test environment variables for headless mode
        setenv("TESTING", "1", 1);

        // Configure the database with our test directory
        fs::path test_db_path = fs::path("data") / "assets.db";
        fs::create_directories(test_db_path.parent_path());

        {
            AssetDatabase setup_db;
            REQUIRE(setup_db.initialize(test_db_path.string()));
            REQUIRE(setup_db.upsert_config_value(Config::CONFIG_KEY_ASSETS_DIRECTORY, temp_assets_dir.string()));
            setup_db.close();
        }

        // Run the application on the main thread
        LOG_INFO("Starting application...");
        int result = run();
        REQUIRE(result == 0);
        LOG_INFO("Application completed successfully");

        // Verify the application processed all the test assets
        AssetDatabase verify_db;
        REQUIRE(verify_db.initialize(test_db_path.string()));

        auto assets = verify_db.get_all_assets();
        REQUIRE(assets.size() > 0);

        // Verify we found the expected test assets
        bool found_fbx = false;
        bool found_obj = false;
        bool found_glb = false;
        bool found_png = false;

        for (const auto& asset : assets) {
            if (asset.extension == ".fbx") found_fbx = true;
            if (asset.extension == ".obj") found_obj = true;
            if (asset.extension == ".glb") found_glb = true;
            if (asset.extension == ".png") found_png = true;
        }

        REQUIRE(found_fbx);
        REQUIRE(found_obj);
        REQUIRE(found_glb);
        REQUIRE(found_png);

        // Verify assets have proper metadata set by the application
        for (const auto& asset : assets) {
            REQUIRE(!asset.name.empty());
            REQUIRE(!asset.path.empty());
            REQUIRE(!asset.extension.empty());
            REQUIRE(asset.size > 0);
            REQUIRE(asset.id > 0);
        }

        verify_db.close();

        // Clean up temporary folder
        if (fs::exists(temp_assets_dir)) {
            fs::remove_all(temp_assets_dir);
        }

        // Clean up test environment variables
        unsetenv("TESTING");
    }

    // Cleanup - remove the local "data" directory created during test
    if (fs::exists("data")) {
        fs::remove_all("data");
    }
}