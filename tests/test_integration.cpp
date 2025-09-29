#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <chrono>
#include <thread>
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
#include "test_helpers.h"

namespace fs = std::filesystem;

TEST_CASE("Integration: Real application execution", "[integration]") {
    // Setup test environment with isolated directories
    fs::path temp_data_dir = create_temp_dir("integration_data");
    fs::path temp_cache_dir = create_temp_dir("integration_cache");

    // Find test assets directory
    fs::path test_assets_dir = fs::current_path() / "tests" / "files" / "assets";
    if (!fs::exists(test_assets_dir)) {
        test_assets_dir = fs::current_path().parent_path() / "tests" / "files" / "assets";
        REQUIRE(fs::exists(test_assets_dir));
    }

    SECTION("Run real application and verify results") {
        // Set up environment variables to control the application behavior
        std::string old_data_path, old_cache_path;
        bool had_data_path = false, had_cache_path = false;

        // Save existing environment variables
        if (const char* existing = std::getenv("XDG_DATA_HOME")) {
            old_data_path = existing;
            had_data_path = true;
        }
        if (const char* existing = std::getenv("XDG_CACHE_HOME")) {
            old_cache_path = existing;
            had_cache_path = true;
        }

        // Set test environment to use our temporary directories
        setenv("XDG_DATA_HOME", temp_data_dir.string().c_str(), 1);
        setenv("XDG_CACHE_HOME", temp_cache_dir.string().c_str(), 1);
        setenv("MAX_FRAMES", "60", 1);

        // Pre-configure the database with our test assets directory
        fs::path test_db_path = temp_data_dir / "AssetInventory" / "assets.db";
        LOG_INFO("DB_PATH: {}", test_db_path.string());
        
        fs::create_directories(test_db_path.parent_path());

        {
            AssetDatabase setup_db;
            REQUIRE(setup_db.initialize(test_db_path.string()));
            REQUIRE(setup_db.upsert_config_value(Config::CONFIG_KEY_ASSETS_DIRECTORY, test_assets_dir.string()));
            setup_db.close();
        }

        // Run the actual main application executable as a subprocess
        std::string app_path = "./AssetInventory.app/Contents/MacOS/AssetInventory";
        std::string command = app_path + " 2>&1";  // Capture both stdout and stderr

        LOG_INFO("Running command: {}", command);
        int result = std::system(command.c_str());
        REQUIRE(result == 0);

        // Verify the real application actually processed assets
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

        // Verify assets have proper metadata set by the real application
        for (const auto& asset : assets) {
            REQUIRE(!asset.name.empty());
            REQUIRE(!asset.path.empty());
            REQUIRE(!asset.extension.empty());
            REQUIRE(asset.size > 0);
            REQUIRE(asset.id > 0);
        }

        verify_db.close();

        // Restore original environment variables
        if (had_data_path) {
            setenv("XDG_DATA_HOME", old_data_path.c_str(), 1);
        } else {
            unsetenv("XDG_DATA_HOME");
        }
        if (had_cache_path) {
            setenv("XDG_CACHE_HOME", old_cache_path.c_str(), 1);
        } else {
            unsetenv("XDG_CACHE_HOME");
        }
        unsetenv("MAX_FRAMES");  // Always clean up test environment variable
    }

    // Cleanup
    cleanup_temp_dir(temp_data_dir);
    cleanup_temp_dir(temp_cache_dir);
}