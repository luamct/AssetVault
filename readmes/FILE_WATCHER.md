# File Watcher System

Cross-platform real-time file system monitoring for automatic asset synchronization using native OS APIs.

## Architecture

### Core Components

**Files:**
- `src/file_watcher.h` - Public interface and FileEvent structure
- `src/file_watcher.cpp` - Platform-agnostic wrapper
- `src/file_watcher_macos.cpp` - macOS FSEvents implementation
- `src/file_watcher_windows.cpp` - Windows ReadDirectoryChangesW implementation
- `src/utils.cpp` - Contains optimized `find_assets_under_directory` function

### FileEvent Structure

```cpp
struct FileEvent {
    FileEventType type;        // Created, Modified, Deleted, Renamed
    fs::path path;            // File path (directories are filtered out)
    fs::path old_path;        // For rename events (source path)
    std::chrono::system_clock::time_point timestamp;
};
```

**Event Types:**
- `Created` - File created (directories are not tracked)
- `Modified` - File content changed
- `Deleted` - File deleted
- `Renamed` - File renamed/moved (both `path` and `old_path` populated)

**Note:** Directory events are handled internally by the file watcher but are not propagated as FileEvents. When a directory is created, moved, or deleted, the file watcher generates appropriate events for all files within that directory.

## Platform Implementations

### macOS - FSEvents API

**File:** `src/file_watcher_macos.cpp`

**Technology:** Apple's FSEvents (File System Events) API

**Performance:**
- Kernel-level notifications with ~100ms batching for efficiency
- No polling overhead - events delivered directly from kernel
- Recursive directory monitoring with single API call
- Event coalescing and debouncing to prevent spam

**Key Characteristics:**
- Events are **batched** and delivered every ~100ms
- **Path normalization** required (FSEvents may add `/private` prefix)
- **Complex flag combinations** - events can have multiple flags set simultaneously
- **Rename events** are used for moves, trash operations, and actual renames

#### FSEvents Event Mapping

FSEvents provides low-level flags that map to our FileEvent types:

| FSEvents Flag | Our Event Type | Description |
|---------------|----------------|-------------|
| `kFSEventStreamEventFlagItemCreated` | `Created` | File created (directories trigger file events for contents) |
| `kFSEventStreamEventFlagItemModified` | `Modified` | File content changed |
| `kFSEventStreamEventFlagItemRemoved` | `Deleted` | File deleted (directories trigger deletion for all contents) |
| `kFSEventStreamEventFlagItemRenamed` | Special handling | Move/rename operation |

#### Rename Event Handling

FSEvents rename events represent various operations:

1. **File/Directory Moved INTO watched area:**
   - Single rename event for destination path
   - Asset not found in database → Generate `Created` event
   
2. **File/Directory Moved OUT OF watched area:**
   - Single rename event for source path  
   - Asset found in database → Generate `Deleted` event
   
3. **File/Directory Renamed WITHIN watched area:**
   - **Two separate rename events** - one for old path, one for new path
   - Old path: Found in database → Generate `Deleted` event
   - New path: Not in database → Generate `Created` event

4. **File/Directory Moved to Trash:**
   - Treated as rename/move-out operation
   - Asset found in database → Generate `Deleted` event

#### Directory Operation Handling

**Directory Move IN:**
```
FSEvents: Single rename event for parent directory
Our Response: 
  1. Recursively scan directory contents
  2. Generate Created events for all child files (directories not tracked)
```

**Directory Move OUT:**
```  
FSEvents: Single rename event for parent directory
Our Response:
  1. Use optimized find_assets_under_directory() for O(log n) lookup
  2. Generate Deleted events for all tracked child files
```

**Directory Rename WITHIN:**
```
FSEvents: Two rename events (old path + new path)
Our Response:
  1. Old path → Directory Move OUT logic
  2. New path → Directory Move IN logic
```

**Directory Permanent Deletion:**
```
FSEvents: Individual deletion events for each file + directory
Our Response: Process each deletion event individually
```

### Windows - ReadDirectoryChangesW API

**File:** `src/file_watcher_windows.cpp`

**Technology:** Windows `ReadDirectoryChangesW` API with overlapped I/O

**Performance:**
- Asynchronous I/O with event-driven notifications
- No polling overhead - events delivered via completion port
- Recursive directory monitoring with single API call
- Immediate event delivery (no automatic batching like FSEvents)

**Key Characteristics:**
- Events delivered immediately as they occur
- **Path separator normalization** required (`\` converted to `/` for consistency)
- **FILE_ACTION codes** map directly to our event types
- **Rename events** come in pairs (OLD_NAME followed by NEW_NAME)

#### Windows Event Mapping

| Windows Action | Our Event Type | Description |
|----------------|----------------|-------------|
| `FILE_ACTION_ADDED` | `Created` | File created (directories trigger scanning for contents) |
| `FILE_ACTION_MODIFIED` | `Modified` | File content or attributes changed |
| `FILE_ACTION_REMOVED` | `Deleted` | File deleted |
| `FILE_ACTION_RENAMED_OLD_NAME` | `Deleted` | Old name in rename (for directories, triggers batch deletion) |
| `FILE_ACTION_RENAMED_NEW_NAME` | `Created` | New name in rename (for directories, triggers scanning) |

#### Directory Operation Handling

Similar to macOS, Windows file watcher handles directory operations by:
- Using `find_assets_under_directory()` for efficient O(log n) lookups when directories are deleted/renamed
- Scanning directory contents when new directories appear
- Filtering out directory events themselves (only file events are propagated)

## Integration with Asset System

### Event Processing Flow

1. **FileWatcher** detects filesystem changes via platform API
2. **Platform-specific logic** maps native events to `FileEvent` objects
3. **EventProcessor** receives events and processes them in batches
4. **Database operations** update asset inventory based on events
5. **UI updates** reflect changes in real-time

### Thread Safety

- **FileWatcher**: Runs on background thread, thread-safe event callbacks
- **Asset Database Access**: Uses provided mutex for thread-safe asset lookups
- **Event Queuing**: Thread-safe queue between file watcher and event processor

### Performance Optimizations

1. **Event Debouncing**: Rapid file changes are debounced (100ms default on macOS)
2. **Batch Processing**: Events processed in configurable batches (default: 100)
3. **Smart Asset Tracking**: Uses in-memory asset map to avoid filesystem calls
4. **Immediate Processing**: Delete/rename events bypass debouncing for responsiveness
5. **O(log n) Directory Lookups**: `find_assets_under_directory()` uses binary search on sorted AssetMap
6. **Path Normalization**: Consistent forward slash usage enables efficient string comparisons

## Usage

```cpp
#include "file_watcher.h"

// Initialize file watcher
FileWatcher watcher;
AssetMap assets;
std::mutex assets_mutex;

// Start watching with callback
auto callback = [](const FileEvent& event) {
    switch (event.type) {
        case FileEventType::Created:
            std::cout << "File created: " << event.path << std::endl;
            break;
            
        case FileEventType::Modified:
            std::cout << "File modified: " << event.path << std::endl;
            break;
            
        case FileEventType::Deleted:
            std::cout << "File deleted: " << event.path << std::endl;
            break;
            
        case FileEventType::Renamed:
            std::cout << "File renamed: " << event.old_path << " -> " << event.path << std::endl;
            break;
    }
};

watcher.start_watching("/path/to/assets", callback, &assets, &assets_mutex);
```

## Testing

Comprehensive test suites for both platforms:
- `tests/test_file_watcher_macos.cpp` - macOS-specific tests
- `tests/test_file_watcher_windows.cpp` - Windows-specific tests
- `tests/test_utils.cpp` - Tests for `find_assets_under_directory()` optimization

**Coverage includes:**
- **File Operations**: Create, modify, delete, rename, copy, move
- **Directory Operations**: Create, delete, rename, move in/out, copy
- **Performance**: O(log n) vs O(n) lookup verification
- **Path Normalization**: Cross-platform path separator handling
- **Edge Cases**: Atomic saves, metadata changes, rapid file changes

**Key Test Categories:**
- File and directory move/rename operations
- Directory copy with nested contents
- Directory deletion with asset cleanup
- Path separator normalization (Windows `\` to `/`)
- Binary search performance validation

## Configuration

The file watcher automatically handles all relevant asset types based on file extensions. No additional configuration is required for basic operation.

**Performance Tuning:**
- `FILE_WATCHER_DEBOUNCE_MS` in `config.h` - Event debouncing delay
- Batch size can be configured in EventProcessor constructor

## Platform Differences Summary

| Aspect | macOS (FSEvents) | Windows (ReadDirectoryChangesW) |
|--------|------------------|------------------------------|
| **Event Batching** | ~100ms automatic batching | Immediate delivery |
| **Directory Moves** | Single event + manual scanning | FILE_ACTION codes + scanning |
| **Rename Detection** | Complex flag-based logic | OLD_NAME/NEW_NAME pairs |
| **Path Normalization** | `/private` prefix handling | `\` to `/` conversion |
| **Trash Operations** | Treated as rename events | Standard deletion |
| **Thread Model** | Background thread + run loop | Overlapped I/O with events |
| **Directory Lookups** | O(log n) via `find_assets_under_directory()` | O(log n) via `find_assets_under_directory()` |

The unified `FileEvent` API abstracts these platform differences, providing consistent behavior across operating systems while leveraging each platform's native capabilities for optimal performance. Directory events are handled internally and converted to file events, ensuring the application only needs to handle file-level changes.