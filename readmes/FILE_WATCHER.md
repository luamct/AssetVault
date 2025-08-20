# File Watcher System

Cross-platform real-time file system monitoring for automatic asset synchronization using native OS APIs.

## Architecture

### Core Components

**Files:**
- `src/file_watcher.h` - Public interface and FileEvent structure
- `src/file_watcher.cpp` - Platform-agnostic wrapper
- `src/file_watcher_macos.cpp` - macOS FSEvents implementation
- `src/file_watcher_windows.cpp` - Windows ReadDirectoryChangesW implementation (future)

### FileEvent Structure

```cpp
struct FileEvent {
    FileEventType type;        // Created, Modified, Deleted, Renamed
    fs::path path;            // Primary file/directory path
    fs::path old_path;        // For rename events (source path)
    bool is_directory;        // True if path refers to a directory
    std::chrono::system_clock::time_point timestamp;
};
```

**Event Types:**
- `Created` - File or directory created (use `is_directory` flag to distinguish)
- `Modified` - File content changed (directories are not modified directly)
- `Deleted` - File or directory deleted (use `is_directory` flag to distinguish)
- `Renamed` - File or directory renamed/moved (both `path` and `old_path` populated)

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

| FSEvents Flag | Our Event Type | is_directory | Description |
|---------------|----------------|--------------|-------------|
| `kFSEventStreamEventFlagItemCreated` | `Created` | Based on `kFSEventStreamEventFlagItemIsDir` | File/directory created |
| `kFSEventStreamEventFlagItemModified` | `Modified` | `false` | File content changed |
| `kFSEventStreamEventFlagItemRemoved` | `Deleted` | Based on `kFSEventStreamEventFlagItemIsDir` | File/directory deleted |
| `kFSEventStreamEventFlagItemRenamed` | Special handling | Based on `kFSEventStreamEventFlagItemIsDir` | Move/rename operation |

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
  1. Generate Created event for directory (is_directory=true)
  2. Recursively scan directory contents
  3. Generate Created events for all child files/directories
```

**Directory Move OUT:**
```  
FSEvents: Single rename event for parent directory
Our Response:
  1. Query asset database for all tracked children
  2. Generate Deleted events for all tracked child files
  3. Generate Deleted event for directory (is_directory=true)
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

### Windows - ReadDirectoryChangesW API (Future Implementation)

**File:** `src/file_watcher_windows.cpp`

**Technology:** Windows `ReadDirectoryChangesW` API

**Expected Characteristics:**
- Different event semantics compared to FSEvents
- May not batch events the same way
- Different rename/move behavior patterns
- Will require platform-specific event mapping logic

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

1. **Event Debouncing**: Rapid file changes are debounced (100ms default)
2. **Batch Processing**: Events processed in configurable batches (default: 100)
3. **Smart Asset Tracking**: Uses in-memory asset map to avoid filesystem calls
4. **Immediate Processing**: Delete/rename events bypass debouncing for responsiveness

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
            if (event.is_directory) {
                std::cout << "Directory created: " << event.path << std::endl;
            } else {
                std::cout << "File created: " << event.path << std::endl;
            }
            break;
            
        case FileEventType::Modified:
            std::cout << "File modified: " << event.path << std::endl;
            break;
            
        case FileEventType::Deleted:
            if (event.is_directory) {
                std::cout << "Directory deleted: " << event.path << std::endl;
            } else {
                std::cout << "File deleted: " << event.path << std::endl;
            }
            break;
            
        case FileEventType::Renamed:
            std::cout << "Renamed: " << event.old_path << " -> " << event.path << std::endl;
            break;
    }
};

watcher.start_watching("/path/to/assets", callback, &assets, &assets_mutex);
```

## Testing

Comprehensive test suite in `tests/test_file_watcher_macos.cpp` covers:

- **File Operations**: Create, modify, delete, rename, copy, move
- **Directory Operations**: Create, delete, rename, move in/out, copy, trash
- **Race Conditions**: Thread safety, callback lifetime management
- **Edge Cases**: Path normalization, event coalescing, rapid changes

**Key Test Categories:**
- `macOS FSEvents rename event handling` - File move/rename operations
- `macOS FSEvents directory copy behavior` - Directory duplication
- `macOS FSEvents directory move operations` - Directory relocations
- `macOS FSEvents directory deletion behavior` - Permanent deletion

## Configuration

The file watcher automatically handles all relevant asset types based on file extensions. No additional configuration is required for basic operation.

**Performance Tuning:**
- `FILE_WATCHER_DEBOUNCE_MS` in `config.h` - Event debouncing delay
- Batch size can be configured in EventProcessor constructor

## Platform Differences Summary

| Aspect | macOS (FSEvents) | Windows (ReadDirectoryChangesW) |
|--------|------------------|------------------------------|
| **Event Batching** | ~100ms automatic batching | Immediate delivery (expected) |
| **Directory Moves** | Single event + manual scanning | Individual events (expected) |
| **Rename Detection** | Complex flag-based logic | Separate rename events (expected) |
| **Path Normalization** | Required (`/private` prefix) | Different requirements (expected) |
| **Trash Operations** | Treated as rename events | Different mechanism (expected) |
| **Thread Model** | Background thread + run loop | Overlapped I/O (expected) |

The unified `FileEvent` API abstracts these platform differences, providing consistent behavior across operating systems while leveraging each platform's native capabilities for optimal performance.