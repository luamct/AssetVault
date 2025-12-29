#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <iostream>
#include <thread>
#include <chrono>
#include <cstdlib>
#include <system_error>
#include <set>
#include <functional>

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
#include "ui/ui.h"
#include "run.h"
#include "test_helpers.h"

namespace fs = std::filesystem;

namespace {
constexpr auto kAssetTimeout = std::chrono::seconds(6);
constexpr auto kThumbnailTimeout = std::chrono::seconds(8);
constexpr auto kWaitInterval = std::chrono::milliseconds(50);

bool wait_for_condition(const std::function<bool()>& predicate,
                         std::chrono::milliseconds timeout,
                         std::chrono::milliseconds interval = kWaitInterval) {
    auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate()) {
            return true;
        }
        std::this_thread::sleep_for(interval);
    }
    return predicate();
}

std::vector<Asset> read_assets(const std::string& db_path) {
    AssetDatabase verify_db;
    if (!verify_db.initialize(db_path)) {
        return {};
    }
    auto assets = verify_db.get_all_assets();
    verify_db.close();
    return assets;
}

bool wait_for_assets_count(const std::string& db_path, size_t expected, std::vector<Asset>& assets) {
    return wait_for_condition([&]{
        assets = read_assets(db_path);
        return assets.size() == expected;
    }, kAssetTimeout);
}

bool wait_for_assets_nonempty(const std::string& db_path, std::vector<Asset>& assets) {
    return wait_for_condition([&]{
        assets = read_assets(db_path);
        return !assets.empty();
    }, kAssetTimeout);
}

void initialize_test_database(const std::string& db_path, const std::string& assets_directory) {
    AssetDatabase setup_db;
    REQUIRE(setup_db.initialize(db_path));
    REQUIRE(setup_db.upsert_config_value(Config::CONFIG_KEY_ASSETS_DIRECTORY, assets_directory));
    setup_db.close();
}

struct ScopedTestEnvironment {
    ScopedTestEnvironment() {
        setenv("TESTING", "1", 1);
        data_dir = Config::get_data_directory();
        test_db = data_dir / "assets.db";
        if (fs::exists(test_db)) {
            fs::remove(test_db);
        }
        fs::create_directories(data_dir);
    }

    ~ScopedTestEnvironment() {
        unsetenv("TESTING");
        if (fs::exists(data_dir)) {
            fs::remove_all(data_dir);
        }
    }

    fs::path data_dir;
    fs::path test_db;
};
}  // namespace

struct ScopedFileRemoval {
    explicit ScopedFileRemoval(fs::path target) : path(std::move(target)) {}
    ~ScopedFileRemoval() {
        if (path.empty()) {
            return;
        }
        std::error_code ec;
        fs::remove(path, ec);
    }

    void dismiss() {
        path.clear();
    }

private:
    fs::path path;
};

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

template <typename StepFn>
void run_headless_step(StepFn&& step) {
    GLFWGuard glfw_guard;
    std::atomic<bool> shutdown_requested(false);
    bool test_passed = false;

    std::thread test_thread([&]{
        step(shutdown_requested, test_passed);
    });

    int result = run(&shutdown_requested);
    test_thread.join();
    REQUIRE(result == 0);
    REQUIRE(test_passed);
}

TEST_CASE("Integration: Real application execution", "[integration]") {
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

    ScopedTestEnvironment test_env;

    std::string db_path_str = Config::get_database_path().string();
    std::string assets_directory = test_assets_source.string();
    fs::path assets_dir(assets_directory);
    initialize_test_database(db_path_str, assets_directory);

    run_headless_step([&](std::atomic<bool>& shutdown_requested, bool& test_passed) {
        std::vector<Asset> assets;
        bool ready = wait_for_assets_count(db_path_str, 5, assets);

        LOG_INFO("[TEST] Found {} assets in database", assets.size());

        if (!ready) {  // racer.fbx, racer.glb, racer.obj, racer.png, zombie.svg
            LOG_ERROR("[TEST] Expected 5 assets, got {}", assets.size());
            for (const auto& asset : assets) {
                LOG_ERROR("[TEST] Unexpected asset entry: {} ({})", asset.name, asset.path);
            }
            shutdown_requested = true;
            return;
        }

        // Verify we found expected asset types
        std::set<std::string> expected_extensions = {".fbx", ".glb", ".obj", ".png", ".svg"};
        std::set<std::string> found_extensions;
        for (const auto& asset : assets) {
            found_extensions.insert(asset.extension);
        }
        for (const auto& expected : expected_extensions) {
            if (found_extensions.count(expected) == 0) {
                LOG_ERROR("[TEST] Missing expected asset type: {}", expected);
                shutdown_requested = true;
                return;
            }
        }

        LOG_INFO("[TEST] ✓ All existing files processed successfully");
        test_passed = true;
        shutdown_requested = true;
    });

    run_headless_step([&](std::atomic<bool>& shutdown_requested, bool& test_passed) {
        std::vector<Asset> assets;
        bool ready = wait_for_assets_nonempty(db_path_str, assets);

        LOG_INFO("[TEST] Database loaded with {} existing assets", assets.size());

        if (!ready) {
            LOG_ERROR("[TEST] Expected assets in database, got 0");
            shutdown_requested = true;
            return;
        }

        LOG_INFO("[TEST] ✓ Successfully loaded existing database");
        test_passed = true;
        shutdown_requested = true;
    });

    run_headless_step([&](std::atomic<bool>& shutdown_requested, bool& test_passed) {
        std::vector<Asset> assets;
        bool ready = wait_for_assets_count(db_path_str, 5, assets);

        if (!ready) {
            LOG_ERROR("[TEST] Expected 5 assets before add, got {}", assets.size());
            shutdown_requested = true;
            return;
        }

        size_t initial_count = assets.size();

        // Copy an existing asset to create a "new" file
        LOG_INFO("[TEST] Adding new asset file...");
        fs::path source_file = assets_dir / "racer.obj";
        fs::path test_file = assets_dir / "racer_copy.obj";
        ScopedFileRemoval ensure_cleanup(test_file);

        if (fs::exists(test_file)) {
            std::error_code ec;
            fs::remove(test_file, ec);
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        fs::copy_file(source_file, test_file);

        bool added = wait_for_assets_count(db_path_str, initial_count + 1, assets);

        LOG_INFO("[TEST] Database now contains {} assets (expected {})", assets.size(), initial_count + 1);

        if (!added) {
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

        // Cleanup test file
        fs::remove(test_file);
        ensure_cleanup.dismiss();

        LOG_INFO("[TEST] ✓ Asset added successfully during execution");
        test_passed = true;
        shutdown_requested = true;
    });

    run_headless_step([&](std::atomic<bool>& shutdown_requested, bool& test_passed) {
        std::vector<Asset> assets;
        bool ready = wait_for_assets_nonempty(db_path_str, assets);

        if (!ready) {
            LOG_ERROR("[TEST] Expected assets before delete, got {}", assets.size());
            shutdown_requested = true;
            return;
        }

        size_t base_count = assets.size();

        // Create a temporary file first
        fs::path test_file = assets_dir / "temp_delete_test.obj";
        ScopedFileRemoval ensure_cleanup(test_file);
        if (fs::exists(test_file)) {
            std::error_code ec;
            fs::remove(test_file, ec);
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        fs::copy_file(assets_dir / "racer.obj", test_file);

        bool created = wait_for_assets_count(db_path_str, base_count + 1, assets);

        if (!created) {
            LOG_ERROR("[TEST] Expected asset count to increase, got {}", assets.size());
            shutdown_requested = true;
            return;
        }

        // Delete the file
        LOG_INFO("[TEST] Deleting asset file...");
        fs::remove(test_file);
        ensure_cleanup.dismiss();
        bool removed = wait_for_assets_count(db_path_str, base_count, assets);

        LOG_INFO("[TEST] Database now contains {} assets (expected {})", assets.size(), base_count);

        if (!removed) {
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

        LOG_INFO("[TEST] ✓ Asset removed successfully from database");
        test_passed = true;
        shutdown_requested = true;
    });

    run_headless_step([&](std::atomic<bool>& shutdown_requested, bool& test_passed) {
        fs::path thumbnail_dir = Config::get_thumbnail_directory();
        std::set<std::string> expected_thumbnails = {"racer.obj.png", "racer.fbx.png", "racer.glb.png"};
        std::set<std::string> found_thumbnails;
        bool ready = wait_for_condition([&]{
            if (!fs::exists(thumbnail_dir)) {
                return false;
            }

            found_thumbnails.clear();
            for (const auto& entry : fs::directory_iterator(thumbnail_dir)) {
                std::string filename = entry.path().filename().string();
                if (expected_thumbnails.count(filename) > 0) {
                    if (fs::file_size(entry.path()) == 0) {
                        return false;
                    }
                    found_thumbnails.insert(filename);
                }
            }
            return found_thumbnails == expected_thumbnails;
        }, kThumbnailTimeout);

        LOG_INFO("[TEST] Checking thumbnails in: {}", thumbnail_dir.string());
        for (const auto& filename : found_thumbnails) {
            LOG_INFO("[TEST]   Found thumbnail: {}", filename);
        }

        if (!ready) {
            if (!fs::exists(thumbnail_dir)) {
                LOG_ERROR("[TEST] Thumbnail directory doesn't exist");
            } else {
                for (const auto& expected : expected_thumbnails) {
                    if (found_thumbnails.count(expected) == 0) {
                        LOG_ERROR("[TEST] {} thumbnail not found", expected);
                    }
                }
            }
            shutdown_requested = true;
            return;
        }

        LOG_INFO("[TEST] ✓ 3D model thumbnails created successfully");
        test_passed = true;
        shutdown_requested = true;
    });

    run_headless_step([&](std::atomic<bool>& shutdown_requested, bool& test_passed) {
        fs::path thumbnail_dir = Config::get_thumbnail_directory();
        bool found_svg_thumb = false;
        std::string found_name;
        bool ready = wait_for_condition([&]{
            if (!fs::exists(thumbnail_dir)) {
                return false;
            }

            for (const auto& entry : fs::directory_iterator(thumbnail_dir)) {
                std::string filename = entry.path().filename().string();
                if (filename.find("zombie") != std::string::npos &&
                    filename.find(".png") != std::string::npos) {
                    found_svg_thumb = true;
                    found_name = filename;
                    return true;
                }
            }
            return false;
        }, kThumbnailTimeout);

        if (!ready) {
            if (!fs::exists(thumbnail_dir)) {
                LOG_ERROR("[TEST] Thumbnail directory doesn't exist");
            } else {
                LOG_ERROR("[TEST] SVG thumbnail (zombie*.png) not found");
            }
            shutdown_requested = true;
            return;
        }

        LOG_INFO("[TEST] Checking file: {}", found_name);
        LOG_INFO("[TEST] ✓ Found SVG thumbnail: {}", found_name);

        LOG_INFO("[TEST] ✓ SVG thumbnail created successfully");
        test_passed = true;
        shutdown_requested = true;
    });

}
