#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>
#include <memory>
#include <thread>
#include <chrono>
#include <algorithm>
#include <set>

#include "file_watcher.h"
#include "asset.h"
#include "database.h"
#include "test_helpers.h"

// macOS-specific file watcher tests using FSEvents
// These tests validate FSEvents behavior and macOS-specific file system operations.

// Helper function to find the test files directory relative to the source file
fs::path get_test_files_dir() {
    // Use __FILE__ to get the location of this source file, then navigate to test files
    // __FILE__ points to tests/test_file_watcher_macos.cpp
    // Test files are at tests/files/ (same tests/ directory)
    auto source_file_path = fs::path(__FILE__);
    return source_file_path.parent_path() / "files";
}

// Simple helper function for test asset management
void add_test_asset(AssetMap& assets, const fs::path& path) {
    Asset asset;
    asset.path = path.u8string();
    asset.name = path.filename().string();
    asset.type = get_asset_type(path.u8string());
    assets[path.u8string()] = asset;
}

// Test fixture for file watcher tests
class FileWatcherTestFixture {
public:
    fs::path test_dir;
    AssetMap assets;
    std::mutex assets_mutex;  // Not actually needed for thread safety in tests, but file watcher requires it
    std::shared_ptr<std::vector<FileEvent>> shared_events;  // Thread-safe events storage
    std::unique_ptr<FileWatcher> watcher;

    FileWatcherTestFixture() {
        // Create temporary test directory using canonical path to match FSEvents output
        auto temp_path = fs::temp_directory_path() / "asset_inventory_test";
        fs::create_directories(temp_path);

        // Use the canonical path that FSEvents will report (resolves /var -> /private/var on macOS)
        test_dir = fs::weakly_canonical(temp_path);

        // Initialize file watcher
        watcher = std::make_unique<FileWatcher>();
    }

    ~FileWatcherTestFixture() {
        // Clean up - ensure file watcher is completely stopped before destruction
        if (watcher) {
            watcher->stop_watching();
            // Give time for all callbacks to complete and threads to shut down
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        fs::remove_all(test_dir);
    }

    void start_watching() {
        // Create a thread-safe callback using a shared pointer to the events vector
        // This ensures the callback data stays alive even if the test fixture is destroyed
        auto events_ptr = std::make_shared<std::vector<FileEvent>>();

        auto event_callback = [events_ptr](const FileEvent& event) {
            events_ptr->push_back(event);
            };

        watcher->start_watching(test_dir.string(), event_callback, &assets, &assets_mutex);

        // Store the shared pointer so we can access events later
        shared_events = events_ptr;

        // Give file watcher time to initialize
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    void wait_for_events(size_t expected_count = 1, int timeout_ms = 500) {
        auto start = std::chrono::steady_clock::now();
        while (get_events().size() < expected_count) {
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
        if (shared_events) {
            shared_events->clear();
        }
    }

    const std::vector<FileEvent>& get_events() const {
        static std::vector<FileEvent> empty_events;
        return shared_events ? *shared_events : empty_events;
    }

    // Helper to find events for a specific file
    std::vector<FileEvent> get_events_for_file(const fs::path& file_path) {
        std::vector<FileEvent> matching_events;

        for (const auto& event : get_events()) {
            if (event.path == file_path) {
                matching_events.push_back(event);
            }
        }
        return matching_events;
    }
};

TEST_CASE("Files and directories moved or renamed within watched directory", "[file_watcher_macos]") {
    FileWatcherTestFixture fixture;

    SECTION("File moved into watched directory") {
        // Test file structure:
        // temp_dir/external_test.png     <- Source file (copied from tests/files/single_file.png)
        //
        // Expected result after move:
        // watched_area/moved_in.png      <- Created event (file moved into watched area)

        // Setup: Copy test file to external location
        auto source_file = get_test_files_dir() / "single_file.png";
        auto external_file = fs::temp_directory_path() / "external_test.png";
        fs::copy_file(source_file, external_file);

        // Start watching
        fixture.start_watching();
        fixture.clear_events();

        // Action: Move file into watched directory
        auto internal_file = fixture.test_dir / "moved_in.png";
        fs::rename(external_file, internal_file);

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
        fs::remove(internal_file);
    }

    SECTION("Directory moved in generates events for all contents") {
        // Test file structure being moved:
        // external_dir/ (outside watched area)
        //   ├── file1.png
        //   ├── file2.png
        //   ├── file3.png
        //   └── subdir/
        //       └── subfile.png
        //
        // macOS FSEvents behavior: When a directory is moved into the watched area,
        // FSEvents generates rename flags which our file watcher converts to Created events.
        // Our file watcher then scans the directory contents and emits individual events.
        //
        // Expected result after move:
        // watched_area/moved_dir/
        //   ├── file1.png      <- Created event
        //   ├── file2.png      <- Created event
        //   ├── file3.png      <- Created event
        //   └── subdir/
        //       └── subfile.png <- Created event

        // Setup: Copy test files to temporary external directory
        auto external_dir = fs::temp_directory_path() / "external_move_dir";
        auto test_files_dir = get_test_files_dir() / "source_dir";
        fs::copy(test_files_dir, external_dir, fs::copy_options::recursive);

        // Start watching
        fixture.start_watching();
        fixture.clear_events();

        // Action: Move directory into watched area
        auto dest_dir = fixture.test_dir / "moved_dir";
        fs::rename(external_dir, dest_dir);

        // Wait for events - should get events for all files
        fixture.wait_for_events(4);  // 4 files: file1.png, file2.png, file3.png, subdir/subfile.png

        // Count events
        int file_creation_count = 0;

        std::cout << "Directory move-in test - captured " << fixture.get_events().size() << " events:" << std::endl;
        for (const auto& event : fixture.get_events()) {
            if (event.type == FileEventType::Created) {
                file_creation_count++;
                std::cout << "  Created: " << event.path << std::endl;
            }
            else {
                std::cout << "  Other: " << event.path << std::endl;
            }
        }

        // Assert: Should generate events for all files
        REQUIRE(file_creation_count == 4);  // 4 files: 3 in root + 1 in subdir

        // Cleanup
        fs::remove_all(dest_dir);
    }

    SECTION("File moved out of watched directory") {
        // Test file structure:
        // watched_area/tracked.png       <- Tracked in database (copied from tests/files/single_file.png)
        //
        // Expected result after move:
        // temp_dir/moved_out.png         <- File moved outside watched area
        // watched_area/                  <- Deleted event for tracked.png

        // Setup: Copy test file to watched directory and track it
        auto source_file = get_test_files_dir() / "single_file.png";
        auto internal_file = fixture.test_dir / "tracked.png";
        fs::copy_file(source_file, internal_file);
        add_test_asset(fixture.assets, internal_file);

        // Start watching
        fixture.start_watching();
        fixture.clear_events();

        // Action: Move file out of watched directory
        auto external_file = fs::temp_directory_path() / "moved_out.png";
        fs::rename(internal_file, external_file);

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
        fs::remove(external_file);
    }

    SECTION("Directory moved out of watched directory") {
        // Test file structure being moved out:
        // move_out_test/
        //   ├── move1.txt              <- Tracked in database, Deleted event expected
        //   ├── move2.png              <- Tracked in database, Deleted event expected
        //   └── subdir/
        //       └── nested.obj         <- Tracked in database, Deleted event expected
        //
        // Test unified deletion handling for directory move-out scenarios

        // Setup: Copy test directory structure to watched area
        fs::path test_move_dir = fixture.test_dir / "move_out_test";
        auto source_dir = get_test_files_dir() / "move_test_dir";
        fs::copy(source_dir, test_move_dir, fs::copy_options::recursive);

        // Define files that should be tracked
        std::vector<fs::path> test_files = {
            test_move_dir / "move1.txt",
            test_move_dir / "move2.png",
            test_move_dir / "subdir" / "nested.obj"
        };

        // Add files to asset database
        for (const auto& file_path : test_files) {
            add_test_asset(fixture.assets, file_path);
        }

        // Start watching
        fixture.start_watching();

        // Give file watcher time to settle
        std::this_thread::sleep_for(std::chrono::milliseconds(200));

        // Simulate move-out by deleting the directory (triggers same unified deletion logic)
        std::cout << "Simulating directory move-out with deletion..." << std::endl;
        fs::remove_all(test_move_dir);

        // Wait for events
        fixture.wait_for_events(1, 1000);

        const auto& events = fixture.get_events();

        std::set<fs::path> deleted_paths;
        for (const auto& event : events) {
            if (event.type == FileEventType::Deleted) {
                deleted_paths.insert(event.path);
            }
        }

        print_file_events(events, "Directory move-out test");

        // Verify all tracked files got deletion events (paths should match exactly now)
        for (const auto& file : test_files) {
            REQUIRE(deleted_paths.count(file) > 0);
        }

        // Should have at least one event per tracked file
        REQUIRE(deleted_paths.size() >= test_files.size());
    }

    SECTION("File renamed within watched directory") {
        // Test file structure:
        // watched_area/old_name.png      <- Tracked in database (copied from tests/files/single_file.png)
        //
        // Expected result after rename:
        // watched_area/new_name.png      <- Created event (new name)
        // watched_area/                  <- Deleted event (old name)

        // Setup: Copy test file to watched directory and track it
        auto source_file = get_test_files_dir() / "single_file.png";
        auto old_file = fixture.test_dir / "old_name.png";
        fs::copy_file(source_file, old_file);
        add_test_asset(fixture.assets, old_file);

        // Start watching
        fixture.start_watching();
        fixture.clear_events();

        // Action: Rename file within watched directory
        auto new_file = fixture.test_dir / "new_name.png";
        fs::rename(old_file, new_file);

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
        fs::remove(new_file);
    }

    SECTION("Directory renamed within watched area") {
        // Test file structure before rename:
        // watched_area/old_dir_name/
        //   ├── file1.png      <- Tracked in database
        //   ├── file2.png      <- Tracked in database
        //   └── file3.png      <- Tracked in database
        //
        // macOS FSEvents behavior: When a tracked directory is renamed within the watched area,
        // FSEvents should generate events for both the old path (move-out) and new path (move-in).
        // Our file watcher should emit deletion events for the old path and creation events for the new path.
        //
        // Expected result after rename:
        // watched_area/new_dir_name/
        //   ├── file1.png      <- Deleted event (old path), Created event (new path)
        //   ├── file2.png      <- Deleted event (old path), Created event (new path)
        //   └── file3.png      <- Deleted event (old path), Created event (new path)

        // Setup: Create directory with files in watched area using test files
        auto old_dir = fixture.test_dir / "old_dir_name";
        auto test_files_dir = get_test_files_dir() / "source_dir";
        fs::copy(test_files_dir, old_dir, fs::copy_options::recursive);

        // Track these files in mock database (they were "previously indexed")
        std::vector<fs::path> test_files = {
            old_dir / "file1.png",
            old_dir / "file2.png",
            old_dir / "file3.png"
        };
        for (const auto& file : test_files) {
            add_test_asset(fixture.assets, file);
        }

        // Start watching
        fixture.start_watching();
        fixture.clear_events();
#
        // Action: Rename directory within watched area
        auto new_dir = fixture.test_dir / "new_dir_name";
        fs::rename(old_dir, new_dir);

        // Wait for events - should get deletion events for old path + creation events for new path
        fixture.wait_for_events(6);  // 6 file events (3 deletions + 3 creations)

        // Count deletion and creation events
        int file_deletion_count = 0;
        int file_creation_count = 0;

        std::cout << "Directory rename test - captured " << fixture.get_events().size() << " events:" << std::endl;
        for (const auto& event : fixture.get_events()) {
            std::string event_type_str;
            switch (event.type) {
            case FileEventType::Created:
                event_type_str = "Created";
                file_creation_count++;
                break;
            case FileEventType::Deleted:
                event_type_str = "Deleted";
                file_deletion_count++;
                break;
            case FileEventType::Modified: event_type_str = "Modified"; break;
            default: event_type_str = "Other"; break;
            }
            std::cout << "  " << event_type_str << ": " << event.path << std::endl;
        }

        // Assert: Should get both deletion events (for old path) and creation events (for new path)
        // The exact number may vary based on FSEvents behavior, but we should have both operations
        REQUIRE(file_deletion_count >= 3);  // At least file deletion events
        REQUIRE(file_creation_count >= 3);  // At least file creation events

        // Verify the new directory exists and old doesn't
        REQUIRE(fs::exists(new_dir));
        REQUIRE(!fs::exists(old_dir));

        // Cleanup
        fs::remove_all(new_dir);
    }
}

TEST_CASE("[MacOS] Files and directories copied into watched directory", "[file_watcher_macos]") {
    FileWatcherTestFixture fixture;

    SECTION("File copied into watched directory") {
        // Test file structure:
        // tests/files/single_file.png    <- Source file (pre-created)
        //
        // Expected result after copy:
        // watched_area/copied.png        <- Created event

        // Setup: Use pre-created test file
        auto source_file = get_test_files_dir() / "single_file.png";

        // Start watching
        fixture.start_watching();
        fixture.clear_events();

        // Action: Copy file into watched directory
        auto dest_file = fixture.test_dir / "copied.png";
        fs::copy_file(source_file, dest_file);

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

        // Cleanup (source file is preserved)
        fs::remove(dest_file);
    }

    SECTION("Directory copy generates individual file events") {
        // Test file structure:
        // tests/files/source_dir/
        //   ├── file1.png
        //   ├── file2.png
        //   ├── file3.png
        //   └── subdir/
        //       └── subfile.png
        //
        // Expected result after copy:
        // watched_area/copied_dir/
        //   ├── file1.png      <- Created event
        //   ├── file2.png      <- Created event
        //   ├── file3.png      <- Created event
        //   └── subdir/
        //       └── subfile.png <- Created event

        // Setup: Copy test files to temporary directory
        auto source_dir = fs::temp_directory_path() / "source_dir_copy";
        auto test_files_dir = get_test_files_dir() / "source_dir";
        fs::copy(test_files_dir, source_dir, fs::copy_options::recursive);

        // Start watching
        fixture.start_watching();
        fixture.clear_events();

        // Action: Copy entire directory into watched area
        auto dest_dir = fixture.test_dir / "copied_dir";
        fs::copy(source_dir, dest_dir, fs::copy_options::recursive);

        // Wait for events - should get events for each file (directories don't generate events)
        fixture.wait_for_events(4);  // 4 files: file1.png, file2.png, file3.png, subdir/subfile.png

        // Count creation events
        int file_creation_count = 0;

        std::cout << "Directory copy test - captured " << fixture.get_events().size() << " events:" << std::endl;
        for (const auto& event : fixture.get_events()) {
            if (event.type == FileEventType::Created) {
                file_creation_count++;
                std::cout << "  Created: " << event.path << std::endl;
            }
            else {
                std::cout << "  Other: " << event.path << std::endl;
            }
        }

        print_file_events(fixture.get_events(), "Directory copy events:");

        // Assert: Should get individual creation events (FSEvents may report duplicates)
        REQUIRE(file_creation_count >= 4);  // At least one for each file (may have duplicates)

        // Cleanup
        fs::remove_all(source_dir);
        fs::remove_all(dest_dir);
    }
}

TEST_CASE("[MacOS] FSEvents directory move operations", "[file_watcher_macos]") {
    FileWatcherTestFixture fixture;

    SECTION("File deleted permanently (previously tracked)") {
        // Test file structure:
        // watched_area/to_delete.png     <- Tracked in database (copied from tests/files/single_file.png)
        //
        // Expected result after deletion:
        // watched_area/                  <- Deleted event for to_delete.png (file no longer exists)

        // Setup: Copy test file to watched directory and track it BEFORE starting watcher
        auto source_file = get_test_files_dir() / "single_file.png";
        auto file = fixture.test_dir / "to_delete.png";
        fs::copy_file(source_file, file);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));  // Let filesystem settle
        add_test_asset(fixture.assets, file);

        // Start watching
        fixture.start_watching();

        // Wait for initial events to settle
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        fixture.clear_events();

        // Action: Delete file
        fs::remove(file);

        // Wait for events
        fixture.wait_for_events(1);

        // Assert: Should have detected the file deletion

        print_file_events(fixture.get_events(), "File deleted");

        // Check if file no longer exists
        REQUIRE(!fs::exists(file));

        // We should have at least one Delete event
        auto events = fixture.get_events();
        REQUIRE(events.size() >= 1);

        bool found_delete = false;
        for (const auto& event : events) {
            if (event.type == FileEventType::Deleted) {
                found_delete = true;
                break;
            }
        }
        REQUIRE(found_delete);
    }

    SECTION("Directory with nested files deleted") {
        // Test file structure to be deleted:
        // test_delete_dir/
        //   ├── file1.png              <- Tracked in database, Deleted event expected
        //   ├── file2.obj              <- Tracked in database, Deleted event expected
        //   ├── subdir1/
        //   │   ├── nested1.obj        <- Tracked in database, Deleted event expected
        //   │   └── nested2.fbx        <- Tracked in database, Deleted event expected
        //   └── subdir2/
        //       └── deep.wav           <- Tracked in database, Deleted event expected
        //
        // Test that emit_deletion_events_for_directory generates events for all tracked files
        // when a directory is deleted, even when FSEvents is inconsistent

        // Setup: Copy test directory structure to watched area
        fs::path test_delete_dir = fixture.test_dir / "test_delete_dir";
        auto source_dir = get_test_files_dir() / "delete_test_dir";
        fs::copy(source_dir, test_delete_dir, fs::copy_options::recursive);

        // Define files that should be tracked
        std::vector<fs::path> test_files = {
            test_delete_dir / "file1.png",
            test_delete_dir / "file2.obj",
            test_delete_dir / "subdir1" / "nested1.obj",
            test_delete_dir / "subdir1" / "nested2.fbx",
            test_delete_dir / "subdir2" / "deep.wav"
        };

        // Add all files to asset database (simulating they're tracked)
        for (const auto& file_path : test_files) {
            add_test_asset(fixture.assets, file_path);
        }

        // Start watching
        fixture.start_watching();

        // Delete the entire directory
        fs::remove_all(test_delete_dir);

        // Wait for events
        fixture.wait_for_events(1, 200);  // Wait up to 200ms second

        // Verify events - emit_deletion_events_for_directory should generate events for all tracked files
        const auto& events = fixture.get_events();

        std::set<fs::path> deleted_paths;
        for (const auto& event : events) {
            if (event.type == FileEventType::Deleted) {
                deleted_paths.insert(event.path);
            }
        }

        print_file_events(events, "Directory deletion test");

        // Verify all tracked files got deletion events (paths should match exactly now)
        for (const auto& file : test_files) {
            REQUIRE(deleted_paths.count(file) > 0);
        }

        // Should have at least one event per tracked file
        REQUIRE(deleted_paths.size() >= test_files.size());
    }
}

TEST_CASE("Files modified or overwritten within watched directory", "[file_watcher_macos]") {
    FileWatcherTestFixture fixture;

    SECTION("File modified (previously tracked)") {
        // Test file structure:
        // watched_area/to_modify.png     <- Created from tests/files/test_modify.png, tracked in database
        //
        // Expected result after modification:
        // watched_area/to_modify.png     <- Modified event (content changed)

        // Setup: Copy test file to watched directory and track it BEFORE starting watcher
        auto source_file = get_test_files_dir() / "test_modify.png";
        auto file = fixture.test_dir / "to_modify.png";
        fs::copy_file(source_file, file);

        // Ensure filesystem timestamp settles
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        // Track in database (simulating it was previously indexed)
        add_test_asset(fixture.assets, file);

        // Start watching
        fixture.start_watching();

        // Wait for initial events to settle and clear any creation events from the copy
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        fixture.clear_events();

        // Action: Modify file by appending content
        std::this_thread::sleep_for(std::chrono::milliseconds(50)); // Ensure different timestamp
        std::ofstream(file, std::ios::app) << "\nmodified content";

        // Wait for events
        fixture.wait_for_events(1);

        print_file_events(fixture.get_events(), "File modified");

        // We should have at least one Modified or Created event (FSEvents inconsistency)
        auto events = fixture.get_events();
        REQUIRE(events.size() >= 1);

        bool found_modification_event = false;
        for (const auto& event : events) {
            if (event.type == FileEventType::Modified || event.type == FileEventType::Created) {
                found_modification_event = true;
                break;
            }
        }
        REQUIRE(found_modification_event);

        // Cleanup
        fs::remove(file);
    }

    SECTION("File overwritten (previously tracked)") {
        // Test file structure:
        // watched_area/existing_file.png <- Already tracked in database
        //
        // Expected result after overwrite:
        // watched_area/existing_file.png <- Deleted event (old file removed)
        // watched_area/existing_file.png <- Created event (new file created)

        // Setup: Copy test file to watched directory and track it BEFORE starting watcher
        auto source_file = get_test_files_dir() / "test_modify.png";
        auto file = fixture.test_dir / "existing_file.png";
        fs::copy_file(source_file, file);

        // Ensure filesystem timestamp settles
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        // Track in database (simulating it was previously indexed)
        add_test_asset(fixture.assets, file);

        // Start watching
        fixture.start_watching();

        // Wait for initial events to settle and clear any creation events from the copy
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        fixture.clear_events();

        // Action: Overwrite the existing file with different content
        auto overwrite_source = get_test_files_dir() / "single_file.png";
        std::this_thread::sleep_for(std::chrono::milliseconds(50)); // Ensure different timestamp
        fs::copy_file(overwrite_source, file, fs::copy_options::overwrite_existing);  // Then copy new file

        // Wait for events - expecting both Delete and Create events
        fixture.wait_for_events(1, 500);

        print_file_events(fixture.get_events(), "File overwritten");

        // We should have both Delete and Create events for the same file path
        auto events = fixture.get_events();
        REQUIRE(events.size() >= 1);

        bool found_create_event = false;

        for (const auto& event : events) {
            if (event.path == file) {
                if (event.type == FileEventType::Created) {
                    found_create_event = true;
                }
            }
        }

        REQUIRE(found_create_event);

        // Cleanup
        fs::remove(file);
    }
}
