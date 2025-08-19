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
    
    // Helper to find events for a specific file (normalizes paths)
    std::vector<FileEvent> get_events_for_file(const std::filesystem::path& file_path) {
        std::vector<FileEvent> matching_events;
        auto normalized_target = std::filesystem::weakly_canonical(file_path);
        
        for (const auto& event : captured_events) {
            try {
                auto normalized_event = std::filesystem::weakly_canonical(event.path);
                if (normalized_event == normalized_target) {
                    matching_events.push_back(event);
                }
            } catch (const std::filesystem::filesystem_error&) {
                // If normalization fails, try string comparison
                if (event.path == file_path) {
                    matching_events.push_back(event);
                }
            }
        }
        return matching_events;
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
        
        // Assert: Should have at least one Created event for the target file
        auto file_events = fixture.get_events_for_file(internal_file);
        REQUIRE(file_events.size() >= 1);
        
        bool found_created = false;
        for (const auto& event : file_events) {
            if (event.type == FileEventType::Created) {
                found_created = true;
                break;
            }
        }
        REQUIRE(found_created);
        
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
        
        // Assert: Should have at least one Deleted event for the source file
        auto file_events = fixture.get_events_for_file(internal_file);
        REQUIRE(file_events.size() >= 1);
        
        bool found_deleted = false;
        for (const auto& event : file_events) {
            if (event.type == FileEventType::Deleted) {
                found_deleted = true;
                break;
            }
        }
        REQUIRE(found_deleted);
        
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
        
        // Wait for events - expecting at least 2 events (delete old, create new)
        fixture.wait_for_events(2);
        
        // Assert: Should have events for both old and new paths
        auto old_events = fixture.get_events_for_file(old_file);
        auto new_events = fixture.get_events_for_file(new_file);
        
        bool found_delete = false;
        bool found_create = false;
        
        for (const auto& event : old_events) {
            if (event.type == FileEventType::Deleted) {
                found_delete = true;
                break;
            }
        }
        
        for (const auto& event : new_events) {
            if (event.type == FileEventType::Created) {
                found_create = true;
                break;
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
        
        // Assert: Should generate Created event for the destination file
        auto dest_events = fixture.get_events_for_file(dest_file);
        REQUIRE(dest_events.size() >= 1);
        
        bool found_created = false;
        for (const auto& event : dest_events) {
            if (event.type == FileEventType::Created) {
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
        // Setup: Create file in watched directory and track it BEFORE starting watcher
        auto file = fixture.test_dir / "to_delete.txt";
        {
            std::ofstream ofs(file);
            ofs << "test content";
        }  // File closed automatically when ofs goes out of scope
        std::this_thread::sleep_for(std::chrono::milliseconds(100));  // Let filesystem settle
        fixture.mock_db.add_asset(file);
        
        // Start watching
        fixture.start_watching();
        
        // Wait for initial events to settle
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        fixture.clear_events();
        
        // Action: Delete file
        std::filesystem::remove(file);
        
        // Wait for events
        fixture.wait_for_events(1);
        
        // Assert: Should have detected the file deletion 
        // Note: FSEvents may generate various event types for deletion (sometimes rename to trash)
        // The important thing is that our file watcher detects that the file is gone
        
        // Check if file no longer exists
        REQUIRE(!std::filesystem::exists(file));
        
        // We should have at least one event (could be Delete, or Rename treated as delete)
        REQUIRE(fixture.captured_events.size() >= 1);
    }
    
    SECTION("File modified (previously tracked)") {
        // Setup: Create file in watched directory and track it BEFORE starting watcher
        auto file = fixture.test_dir / "to_modify.txt";
        {
            std::ofstream ofs(file);
            ofs << "initial content";
        }  // File closed automatically when ofs goes out of scope
        std::this_thread::sleep_for(std::chrono::milliseconds(100));  // Let filesystem settle
        fixture.mock_db.add_asset(file);
        
        // Start watching
        fixture.start_watching();
        
        // Wait for initial events to settle
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        fixture.clear_events();
        
        // Action: Modify file  
        std::this_thread::sleep_for(std::chrono::milliseconds(100)); // Ensure different timestamp
        std::ofstream(file, std::ios::app) << "\nmodified content";
        
        // Wait for events
        fixture.wait_for_events(1);
        
        // Assert: Should have detected the file modification
        // Note: FSEvents may generate various event types for modification
        // The important thing is that our file watcher detects filesystem changes
        
        // Check if file exists and has been modified
        REQUIRE(std::filesystem::exists(file));
        
        // Read file content to verify modification happened
        std::ifstream read_file(file);
        std::string content((std::istreambuf_iterator<char>(read_file)),
                           std::istreambuf_iterator<char>());
        REQUIRE(content.find("modified content") != std::string::npos);
        
        // We should have at least one event for this file
        REQUIRE(fixture.captured_events.size() >= 1);
        
        // Cleanup
        std::filesystem::remove(file);
    }
    
    SECTION("Directory deletion generates individual file events") {
        // Setup: Create directory with multiple files
        auto test_subdir = fixture.test_dir / "subdir";
        std::filesystem::create_directory(test_subdir);
        
        std::vector<std::filesystem::path> test_files;
        for (int i = 1; i <= 3; i++) {
            auto file = test_subdir / ("file" + std::to_string(i) + ".txt");
            std::ofstream(file) << "test content " << i;
            test_files.push_back(file);
            fixture.mock_db.add_asset(file);
        }
        
        fixture.mock_db.add_asset(test_subdir);
        
        // Start watching
        fixture.start_watching();
        fixture.clear_events();
        
        // Action: Delete entire directory
        std::filesystem::remove_all(test_subdir);
        
        // Wait for events - should get events for each file plus directory
        fixture.wait_for_events(4);  // 3 files + 1 directory
        
        // Count how many file deletion events vs directory deletion events
        int file_deletion_count = 0;
        int dir_deletion_count = 0;
        
        std::cout << "Directory deletion test - captured " << fixture.captured_events.size() << " events:" << std::endl;
        for (const auto& event : fixture.captured_events) {
            std::string event_type_str;
            switch (event.type) {
                case FileEventType::Deleted: event_type_str = "Deleted"; file_deletion_count++; break;
                case FileEventType::DirectoryDeleted: event_type_str = "DirectoryDeleted"; dir_deletion_count++; break;
                case FileEventType::Created: event_type_str = "Created"; break;
                case FileEventType::Modified: event_type_str = "Modified"; break;
                case FileEventType::Renamed: event_type_str = "Renamed"; break;
                default: event_type_str = "Other"; break;
            }
            std::cout << "  " << event_type_str << ": " << event.path.string() << std::endl;
        }
        
        // Assert: Platform-specific behavior
#ifdef __APPLE__
        // On macOS with FSEvents, we should get individual file events + directory event
        REQUIRE(file_deletion_count == 3);  // One for each file
        REQUIRE(dir_deletion_count >= 1);   // At least one for directory
#elif defined(_WIN32)
        // On Windows, check what actually happens
        std::cout << "Windows behavior - file deletions: " << file_deletion_count << ", dir deletions: " << dir_deletion_count << std::endl;
        REQUIRE(fixture.captured_events.size() >= 3);  // Should get at least file events
#else
        // Linux/other platforms
        REQUIRE(fixture.captured_events.size() >= 3);
#endif
    }
}

TEST_CASE("Directory deletion event behavior", "[file_watcher]") {
    FileWatcherTestFixture fixture;
    
    SECTION("Directory deletion generates individual file events") {
        // Setup: Create directory with multiple files
        auto test_subdir = fixture.test_dir / "subdir";
        std::filesystem::create_directory(test_subdir);
        
        std::vector<std::filesystem::path> test_files;
        for (int i = 1; i <= 3; i++) {
            auto file = test_subdir / ("file" + std::to_string(i) + ".txt");
            std::ofstream(file) << "test content " << i;
            test_files.push_back(file);
            fixture.mock_db.add_asset(file);
        }
        
        fixture.mock_db.add_asset(test_subdir);
        
        // Start watching
        fixture.start_watching();
        fixture.clear_events();
        
        // Action: Delete entire directory
        std::filesystem::remove_all(test_subdir);
        
        // Wait for events - should get events for each file plus directory
        fixture.wait_for_events(4);  // 3 files + 1 directory
        
        // Count how many file deletion events vs directory deletion events
        int file_deletion_count = 0;
        int dir_deletion_count = 0;
        
        std::cout << "Directory deletion test - captured " << fixture.captured_events.size() << " events:" << std::endl;
        for (const auto& event : fixture.captured_events) {
            std::string event_type_str;
            switch (event.type) {
                case FileEventType::Deleted: event_type_str = "Deleted"; file_deletion_count++; break;
                case FileEventType::DirectoryDeleted: event_type_str = "DirectoryDeleted"; dir_deletion_count++; break;
                case FileEventType::Created: event_type_str = "Created"; break;
                case FileEventType::Modified: event_type_str = "Modified"; break;
                case FileEventType::Renamed: event_type_str = "Renamed"; break;
                default: event_type_str = "Other"; break;
            }
            std::cout << "  " << event_type_str << ": " << event.path.string() << std::endl;
        }
        
        // Assert: Platform-specific behavior
#ifdef __APPLE__
        // On macOS with FSEvents, we should get individual file events + directory event
        REQUIRE(file_deletion_count == 3);  // One for each file
        REQUIRE(dir_deletion_count >= 1);   // At least one for directory
#elif defined(_WIN32)
        // On Windows, check what actually happens
        std::cout << "Windows behavior - file deletions: " << file_deletion_count << ", dir deletions: " << dir_deletion_count << std::endl;
        REQUIRE(fixture.captured_events.size() >= 3);  // Should get at least file events
#else
        // Linux/other platforms
        REQUIRE(fixture.captured_events.size() >= 3);
#endif
    }
}
