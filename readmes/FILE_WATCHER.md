# File Watcher System  

Real-time file system monitoring for automatic asset synchronization using native Windows APIs.

## Implementation

**File:** `src/file_watcher_windows.cpp`

**Technology:** Windows `ReadDirectoryChangesW` API

**Performance:**
- Ultra-low latency (< 1ms response time)  
- Kernel-level notifications (no polling overhead)
- Minimal CPU usage (only active when changes occur)
- Supports all file system events (create, modify, delete, rename)
- Recursive directory monitoring

**How it works:**
- Uses Windows' native file system change notifications
- Creates overlapped I/O operation that waits for file system events
- Events delivered directly from kernel to application
- No polling or file system scanning required

## Integration

The file watcher integrates with the Asset Inventory system for:
- Automatic detection and indexing of new assets
- Real-time UI updates when files change  
- Thread-safe callbacks executed in main thread context

## Usage

```cpp
#include "file_watcher.h"

FileWatcher watcher;

// Start watching assets directory
watcher.StartWatching("assets", [](const FileEvent& event) {
    switch (event.type) {
        case FileEventType::Created:
            // Handle new file
            break;
        case FileEventType::Modified:
            // Handle modified file
            break;
        case FileEventType::Deleted:
            // Handle deleted file
            break;
    }
});
```

## Configuration

The system automatically detects and indexes relevant asset types:
- Images: `.png`, `.jpg`, `.jpeg`, `.tga`, `.bmp`
- 3D Models: `.fbx`, `.obj`, `.dae`, `.3ds`, `.blend`
- SVGs: `.svg` (pre-rasterized to 240px thumbnails)
- Audio: `.wav`, `.mp3`, `.ogg`
- Fonts: `.ttf`, `.otf`

## System Behavior

This project uses low level system dependent APIs, each with its own quirks that needed 
to be handled accordingly.

### FSEvents (MacOS)

- File copied
- File moved into
- File deleted (to trash)
- File deleted permanentely
- File Renamed
- Directory copied
- Directory moved into
- Directory deleted (to trash)
- Directory deleted permanentely
- Directory Renamed

Lets rework the FSEvents implementation a bit now that I understand how it behaves. Here are the actions I want to support:                                               
   + File copied: Seems simple, check creation flag
   + File moved into path: Renames trigger two events, one for the old name and one for the new, but only if both locations are within the observed path. This case here only one event will be triggered, but in order to distinguish it from the other renames we'll have to access the list of assets and check for the path on it. If it's there, then we create a file delete for that path, otherwise we trigger a create file for that path
   - File renamed within path
   + File moved outside path: Covered by the logic described above
   + File deleted (to trash): Triggered as a rename (moved to trash), covered by the logic described above
   + File deleted permanentely: Simple, checks deletion flag 
   

Now I want to cover directory operations:
- Directory copied: I believe it's already covered, since every file and directories will be triggered individually
- Directory moved in: FSEvent only triggers a single renamed event for the parent directory, so we'll have to list all files under the directory and emit Create events for each, including directories
- Directory moved out: Similar to above, but since we don't have the files on disk, we have to check the assets variable to list all child files and emit delete events for each
- Directory Renamed within: Two events are triggered for this operation, and each event should fall into one of the previous cases 
- Directory deleted (to trash): These are treated as moved out, so the logic above handles it
- Directory deleted permanentely: Events are triggered for each file, so its simple

Like the previous implementation for files, checking the assets variable should cover most cases without the need for doing file operations, with the exception of a moved in folder, since the files are not tracked and also don't trigger events individually