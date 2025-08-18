#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>
#include <memory>
#include <thread>
#include <chrono>
#include <algorithm>

#include "file_watcher.h"
#include "asset.h"
#include "database.h"

// Mock asset database for testing
class MockAssetDatabase {
private:
    std::vector<std::filesystem::path> tracked_assets;
    
public:
    void add_asset(const std::filesystem::path& path) {
        tracked_assets.push_back(path);
    }
    
    void remove_asset(const std::filesystem::path& path) {
        auto it = std::find(tracked_assets.begin(), tracked_assets.end(), path);
        if (it != tracked_assets.end()) {
            tracked_assets.erase(it);
        }
    }
    
    bool has_asset(const std::filesystem::path& path) const {
        // Normalize paths for comparison (FSEvents adds /private prefix on macOS)
        auto normalized_path = std::filesystem::weakly_canonical(path);
        for (const auto& asset : tracked_assets) {
            if (std::filesystem::weakly_canonical(asset) == normalized_path) {
                return true;
            }
        }
        return false;
    }
    
    void clear() {
        tracked_assets.clear();
    }
};

// Test fixture for file watcher tests
class FileWatcherTestFixture {
public:
    std::filesystem::path test_dir;
    MockAssetDatabase mock_db;
    std::vector<FileEvent> captured_events;
    std::unique_ptr<FileWatcher> watcher;
    
    FileWatcherTestFixture() {
        // Create temporary test directory
        test_dir = std::filesystem::temp_directory_path() / "asset_inventory_test";
        std::filesystem::create_directories(test_dir);
        
        // Initialize file watcher
        watcher = std::make_unique<FileWatcher>();
    }
    
    ~FileWatcherTestFixture() {
        // Clean up
        if (watcher) {
            watcher->stop_watching();
        }
        std::filesystem::remove_all(test_dir);
    }
    
    void start_watching() {
        auto event_callback = [this](const FileEvent& event) {
            captured_events.push_back(event);
        };
        
        auto asset_check = [this](const std::filesystem::path& path) {
            return mock_db.has_asset(path);
        };
        
        watcher->start_watching(test_dir.string(), event_callback, asset_check);
        
        // Give file watcher time to initialize
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    void wait_for_events(size_t expected_count = 1, int timeout_ms = 2000) {
        auto start = std::chrono::steady_clock::now();
        while (captured_events.size() < expected_count) {
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start).count();
            if (elapsed > timeout_ms) {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        // Additional wait for debouncing to complete
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    void clear_events() {
        captured_events.clear();
    }
};

TEST_CASE("File watcher rename event handling", "[file_watcher]") {
    FileWatcherTestFixture fixture;
    
    SECTION("File moved into watched directory (not previously tracked)") {
        // Setup: Create file outside watched directory
        auto external_file = std::filesystem::temp_directory_path() / "external_test.txt";
        std::ofstream(external_file) << "test content";
        
        // Start watching
        fixture.start_watching();
        fixture.clear_events();
        
        // Action: Move file into watched directory
        auto internal_file = fixture.test_dir / "moved_in.txt";
        std::filesystem::rename(external_file, internal_file);
        
        // Wait for events
        fixture.wait_for_events(1);
        
        // Assert: Should generate Created event
        REQUIRE(fixture.captured_events.size() == 1);
        REQUIRE(fixture.captured_events[0].type == FileEventType::Created);
        REQUIRE(fixture.captured_events[0].path == internal_file);
        
        // Cleanup
        std::filesystem::remove(internal_file);
    }
    
    SECTION("File moved out of watched directory (previously tracked)") {
        // Setup: Create file in watched directory and track it
        auto internal_file = fixture.test_dir / "tracked.txt";
        std::ofstream(internal_file) << "test content";
        fixture.mock_db.add_asset(internal_file);
        
        // Start watching
        fixture.start_watching();
        fixture.clear_events();
        
        // Action: Move file out of watched directory
        auto external_file = std::filesystem::temp_directory_path() / "moved_out.txt";
        std::filesystem::rename(internal_file, external_file);
        
        // Wait for events
        fixture.wait_for_events(1);
        
        // Assert: Should generate Deleted event
        REQUIRE(fixture.captured_events.size() == 1);
        REQUIRE(fixture.captured_events[0].type == FileEventType::Deleted);
        REQUIRE(fixture.captured_events[0].path == internal_file);
        
        // Cleanup
        std::filesystem::remove(external_file);
    }
    
    SECTION("File renamed within watched directory (previously tracked)") {
        // Setup: Create file in watched directory and track it
        auto old_file = fixture.test_dir / "old_name.txt";
        std::ofstream(old_file) << "test content";
        fixture.mock_db.add_asset(old_file);
        
        // Start watching
        fixture.start_watching();
        fixture.clear_events();
        
        // Action: Rename file within watched directory
        auto new_file = fixture.test_dir / "new_name.txt";
        std::filesystem::rename(old_file, new_file);
        
        // Wait for events - expecting 2 events (delete old, create new)
        fixture.wait_for_events(2);
        
        // Assert: Should generate Deleted for old path and Created for new path
        REQUIRE(fixture.captured_events.size() == 2);
        
        // Find the delete and create events (order may vary)
        bool found_delete = false;
        bool found_create = false;
        
        for (const auto& event : fixture.captured_events) {
            if (event.type == FileEventType::Deleted && event.path == old_file) {
                found_delete = true;
            } else if (event.type == FileEventType::Created && event.path == new_file) {
                found_create = true;
            }
        }
        
        REQUIRE(found_delete);
        REQUIRE(found_create);
        
        // Cleanup
        std::filesystem::remove(new_file);
    }
    
    SECTION("File copied into watched directory") {
        // Setup: Create source file
        auto source_file = std::filesystem::temp_directory_path() / "source.txt";
        std::ofstream(source_file) << "test content";
        
        // Start watching
        fixture.start_watching();
        fixture.clear_events();
        
        // Action: Copy file into watched directory
        auto dest_file = fixture.test_dir / "copied.txt";
        std::filesystem::copy_file(source_file, dest_file);
        
        // Wait for events
        fixture.wait_for_events(1);
        
        // Assert: Should generate Created event
        REQUIRE(fixture.captured_events.size() >= 1);
        bool found_created = false;
        for (const auto& event : fixture.captured_events) {
            if (event.type == FileEventType::Created && event.path == dest_file) {
                found_created = true;
                break;
            }
        }
        REQUIRE(found_created);
        
        // Cleanup
        std::filesystem::remove(source_file);
        std::filesystem::remove(dest_file);
    }
    
    SECTION("File deleted permanently (previously tracked)") {
        // Setup: Create file in watched directory and track it
        auto file = fixture.test_dir / "to_delete.txt";
        std::ofstream(file) << "test content";
        fixture.mock_db.add_asset(file);
        
        // Start watching
        fixture.start_watching();
        fixture.clear_events();
        
        // Action: Delete file
        std::filesystem::remove(file);
        
        // Wait for events
        fixture.wait_for_events(1);
        
        // Assert: Should generate Deleted event
        REQUIRE(fixture.captured_events.size() == 1);
        REQUIRE(fixture.captured_events[0].type == FileEventType::Deleted);
        REQUIRE(fixture.captured_events[0].path == file);
    }
    
    SECTION("File modified (previously tracked)") {
        // Setup: Create file in watched directory and track it
        auto file = fixture.test_dir / "to_modify.txt";
        std::ofstream(file) << "initial content";
        fixture.mock_db.add_asset(file);
        
        // Start watching
        fixture.start_watching();
        fixture.clear_events();
        
        // Action: Modify file
        std::this_thread::sleep_for(std::chrono::milliseconds(10)); // Ensure different timestamp
        std::ofstream(file, std::ios::app) << "\nmodified content";
        
        // Wait for events
        fixture.wait_for_events(1);
        
        // Assert: Should generate Modified event
        REQUIRE(fixture.captured_events.size() >= 1);
        bool found_modified = false;
        for (const auto& event : fixture.captured_events) {
            if (event.type == FileEventType::Modified && event.path == file) {
                found_modified = true;
                break;
            }
        }
        REQUIRE(found_modified);
        
        // Cleanup
        std::filesystem::remove(file);
    }
}
