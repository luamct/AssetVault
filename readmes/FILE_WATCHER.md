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
