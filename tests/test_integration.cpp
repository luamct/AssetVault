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
    // Find test assets directory
    fs::path test_assets_dir = fs::current_path() / "tests" / "files" / "assets";
    if (!fs::exists(test_assets_dir)) {
        test_assets_dir = fs::current_path().parent_path() / "tests" / "files" / "assets";
        REQUIRE(fs::exists(test_assets_dir));
    }

    SECTION("Run real application and verify results") {
        // Set test environment variables
        setenv("TESTING", "1", 1);
        setenv("MAX_FRAMES", "60", 1);

        // Pre-configure the database with our test assets directory
        // When TESTING is set, the app will use a local "data" directory
        fs::path test_db_path = fs::path("data") / "assets.db";
        LOG_INFO("DB_PATH: {}", test_db_path.string());
        
        fs::create_directories(test_db_path.parent_path());

        {
            AssetDatabase setup_db;
            REQUIRE(setup_db.initialize(test_db_path.string()));
            REQUIRE(setup_db.upsert_config_value(Config::CONFIG_KEY_ASSETS_DIRECTORY, test_assets_dir.string()));
            setup_db.close();
        }

        // Run the actual main application executable as a subprocess
        // ctest runs from build directory, so use relative path from there
        fs::path exe_path;

#ifdef _WIN32
        // Windows: Debug build
        exe_path = fs::path("Debug") / "AssetInventory.exe";
#elif __APPLE__
        // macOS: AssetInventory.app bundle
        exe_path = fs::path("AssetInventory.app") / "Contents" / "MacOS" / "AssetInventory";
#endif

        INFO("Looking for AssetInventory at: " << exe_path.string());
        REQUIRE(fs::exists(exe_path));

        // Build command with proper quoting for paths with spaces
        std::string command = "\"" + exe_path.string() + "\" 2>&1";

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

        // Clean up test environment variables
        unsetenv("TESTING");
        unsetenv("MAX_FRAMES");
    }

    // Cleanup - remove the local "data" directory created during test
    if (fs::exists("data")) {
        fs::remove_all("data");
    }
}