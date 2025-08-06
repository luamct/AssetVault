# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## AI Assistant Guidelines

### Communication Principles
- **Be candid and objective** - avoid unnecessary praise unless warranted
- **Be critical of suggestions** - if something seems problematic, say so directly and provide better alternatives
- **Evaluate solutions based on maintainability, performance, and complexity**
- **Ask for confirmation when suggestions might be problematic**

### Understanding User Intent
- **Don't make code changes when user is asking questions** - distinguish between genuine questions and change requests
- **Questions that should NOT trigger code changes:**
  - "What's the best way to solve X?"
  - "How does this work?"
  - "Why is this implemented this way?"
- **Requests that SHOULD trigger code changes:**
  - "Let's implement/improve/refactor..."
  - "Fix this issue"
  - "Add feature X"
- **When in doubt, ask for clarification**

### Git and Version Control
- **NEVER perform git commands unless explicitly requested**
- **NEVER assume user wants to commit changes** - wait for explicit instruction
- Only perform git operations when specifically asked

### Visual Changes and UI
- **ALWAYS wait for user confirmation on visual/UI changes** - you cannot see the result
- **Never assume a visual fix worked** - let user verify
- Request user testing and confirmation for UI positioning, layout, or appearance changes

## Build Commands

### Unified vcpkg Build System
The project now uses vcpkg for unified cross-platform dependency management with static linking for distribution builds.

**Prerequisites:**
- Install vcpkg: `git clone https://github.com/microsoft/vcpkg $HOME/vcpkg && $HOME/vcpkg/bootstrap-vcpkg.sh`
- Set environment: `export VCPKG_ROOT="$HOME/vcpkg"`

**Build Commands:**
```bash
# Configure with platform preset (always run from project root)
export VCPKG_ROOT="$HOME/vcpkg"  # Set vcpkg path
cmake --preset <preset-name>      # Configure with preset (outputs to build/)
cmake --build build --config Release  # Build application

# Available presets:
# - windows-static: Windows build with static linking for distribution
# - macos: macOS build with app bundle support
# - linux-static: Linux build with static linking for AppImage

# Run application (from project root)
./build/AssetInventory.app/Contents/MacOS/AssetInventory  # macOS
./build/AssetInventory.exe                                # Windows
./build/AssetInventory                                    # Linux

# Run unit tests (from project root)
./build/SearchTest

# Clean build artifacts
rm -rf build/
```

### Directory Management
- **Always stay in project root directory** when running commands
- **Build in separate directory**: All builds happen in `build/` subdirectory
- **Use relative paths** instead of absolute paths when possible
- **vcpkg dependencies**: Automatically managed within build directory
- **Clean builds**: Simply delete the `build/` directory

## Architecture Overview

### Unified Event-Driven System
The application uses a unified event-driven architecture where both initial scanning and runtime file changes flow through the same processing pipeline:

- **EventProcessor** (`src/event_processor.h/cpp`): Central hub that processes all asset events in batches
- **Smart Initial Scan** (`src/main.cpp::perform_initial_scan()`): Compares filesystem state with database to generate events
- **FileWatcher** (`src/file_watcher*.cpp/h`): Monitors filesystem changes and generates runtime events
- **Batch Processing**: All events processed in configurable batches (default 100) for optimal performance

### Core Components
- **main.cpp**: Entry point with ImGui-based UI, smart initial scanning, and event coordination
- **event_processor.cpp/h**: Unified event processing with batch database operations and progress tracking
- **database.cpp/h**: SQLite-based asset storage with batch operations (insert_assets_batch, update_assets_batch, delete_assets_batch)
- **asset.cpp/h**: Asset type detection, file metadata extraction, and asset structure definitions
- **file_watcher*.cpp/h**: Cross-platform file system monitoring with real-time event generation
- **3d.cpp/h**: OpenGL-based 3D model rendering with multi-material support via Assimp
- **texture_manager.cpp/h**: Texture loading, caching, and thumbnail generation including SVG support
- **utils.cpp/h**: String formatting, file operations, and utility functions
- **theme.h**: Centralized UI color management system
- **config.h**: Configuration constants (window size, paths, performance settings)

### Asset Type System
The `AssetType` enum in `asset.h` defines supported asset categories. When adding new types:
1. Add enum value to `AssetType` in `asset.h`
2. Update `get_asset_type_string()` in `asset.cpp` (returns lowercase strings for database storage)
3. Update `get_asset_type_from_string()` in `asset.cpp` (expects lowercase input)
4. Add texture icon mapping in `load_type_textures()` in `texture_manager.cpp`
5. Add file extension mapping in `get_asset_type()` in `asset.cpp`
- **Never manually convert strings to enums** - always use `get_asset_type_from_string()`
- **Type naming convention**: Database stores lowercase type names ("2d", "3d", "audio", etc.)

### Event Processing Flow
1. **Initial Scan** (main thread):
   - `perform_initial_scan()` compares filesystem state with database
   - Generates FileEvents: Created (file not in DB), Modified (newer timestamp), Deleted (in DB but not filesystem)
   - Publishes events to EventProcessor queue with timing measurement

2. **Runtime File Watching** (background thread):
   - FileWatcher detects filesystem changes
   - Generates FileEvents for Created/Modified/Deleted/Renamed operations
   - Queues events to EventProcessor

3. **Event Processing** (EventProcessor background thread):
   - Processes events in batches by type for optimal database performance
   - Updates database using batch operations (insert_assets_batch, update_assets_batch, delete_assets_batch)
   - Updates in-memory assets vector with thread-safe mutex protection
   - Provides real-time progress tracking with atomic counters

### Database Schema
SQLite database at `db/assets.db` stores asset metadata with the following characteristics:
- Smart incremental updates - only processes actual changes, not full rescans
- Batch database operations minimize round-trips for performance
- Thread-safe access with mutex protection during updates
- No expensive timestamp comparison system (removed for performance)

### 3D Rendering Pipeline
OpenGL-based rendering system with:
- Assimp integration for model loading (FBX, OBJ, GLTF, etc.)
- Multi-material support with texture loading
- Framebuffer-based preview rendering for thumbnails
- Automatic model bounds calculation for camera positioning
- Graceful handling of animation-only FBX files (no visible geometry)
- Failed model cache to prevent infinite retry loops

### Threading Model
- **Main UI thread**: ImGui rendering, user interaction, and initial scan coordination
- **EventProcessor background thread**: Unified event processing with batch operations and progress tracking
- **FileWatcher background thread**: Real-time filesystem monitoring with thread-safe event queue
- **Thread synchronization**: Mutex protection for shared data, atomic counters for progress, condition variables for event queuing

### Unit Testing System
Uses Catch2 header-only framework for fast, lightweight unit testing focused on core business logic.

**Running Tests:**
```bash
# Build and run all tests (cross-platform)
./build/SearchTest

# Run tests with specific tags
./build/SearchTest "[search]"

# Show verbose output
./build/SearchTest -s

# List all tests
./build/SearchTest --list-tests
```

**Current Test Coverage:**
- **Search System**: Complete coverage of `parse_search_query()` function
  - Type filter parsing with comma separation
  - Case insensitive input handling
  - Whitespace tolerance and trimming
  - Unknown type filtering
  - Edge cases and malformed input

**Adding New Tests:**
1. Add to existing `tests/test_search.cpp` for search-related functions
2. Create new test files for other modules following naming pattern `tests/test_<module>.cpp`
3. Update CMakeLists.txt to build new test executables
4. Use Arrange-Act-Assert pattern with descriptive test names

**Test Philosophy:**
- **Simple and fast**: Header-only framework, minimal setup
- **Focus on business logic**: Test core functions, not UI or rendering
- **Immediate feedback**: Catches bugs during development, not in production
- **Example**: Unit tests caught a critical regex parsing bug in search functionality

## Development Guidelines

### Code Quality Standards
- **Always fix compiler warnings** - don't leave them unresolved
- **Avoid global variables** unless for performance or multi-threading (they make testing harder)
- **Follow DRY principle** - create functions to eliminate duplicate code patterns
- **Use centralized color management** - all colors defined in `src/theme.h` using `Theme::` namespace
  - Never hardcode color values directly in code
  - Use existing constants (e.g., `Theme::BACKGROUND_LIGHT_BLUE_1`, `Theme::ACCENT_BLUE_1`)
  - Add new colors with descriptive names using numbered suffixes

### EventProcessor System
The `EventProcessor` class is the core of the unified event system:
- **Initialization**: `EventProcessor(database, assets, search_update_needed, batch_size)`
- **Event queuing**: `queue_event()` for single events, `queue_events()` for batches
- **Progress tracking**: `get_progress()`, `get_total_queued()`, `get_total_processed()`
- **Thread safety**: All asset vector access must use `get_assets_mutex()` for locking

### Database Management
- SQLite database located at `db/assets.db`
- Smart incremental scanning - no database clearing on startup
- All database updates flow through EventProcessor batch operations
- Use batch methods: `insert_assets_batch()`, `update_assets_batch()`, `delete_assets_batch()`

### ImGui Configuration
- `imgui.ini` file disabled to avoid persistent window state
- Window positions and sizes not saved between sessions

### Build Configuration
- Uses C++17 standard with CMake 3.21+
- **Unified vcpkg dependency management** across all platforms
- **Static linking configuration** for self-contained distribution builds
- **Cross-platform support**: Windows (Visual Studio), macOS (Xcode/Make), Linux (Make)
- **App bundle support** on macOS with proper Info.plist configuration
- Some dependencies still built from source: ImGui (for customization), GLAD (OpenGL loader), fonts and resources

### Directory Structure
```
src/               # Source files with clear separation of concerns
  ├── main.cpp     # Main application entry point, UI logic, and smart initial scanning
  ├── event_processor.cpp/h # Unified event processing with batch operations and progress tracking
  ├── database.cpp/h # Database operations with batch methods for EventProcessor
  ├── asset.cpp/h  # Asset type detection, file metadata extraction, and processing logic
  ├── file_watcher*.cpp/h # Cross-platform file system monitoring with event generation
  ├── texture_manager.cpp/h # Texture loading, caching, and thumbnail generation including SVG
  ├── 3d.cpp/h     # 3D model rendering and viewport functionality
  ├── utils.cpp/h  # Utility functions for string formatting, file operations
  └── theme.h      # UI theming configuration
external/          # Minimal dependencies that can't use vcpkg (8MB)
  ├── imgui/       # UI framework (needs backend compilation)
  ├── glad/        # OpenGL function loading
  ├── nanosvg/     # SVG rendering (not in vcpkg)
  ├── fonts/       # Application resources
  ├── miniaudio.h  # Audio metadata (not in vcpkg)
  ├── stb_image_write.h # Image writing (single header)
  └── README.md    # Explains why each dependency remains
images/            # UI icons and textures
db/                # SQLite database files
tests/             # Unit tests with Catch2 framework
readmes/           # Component-specific documentation
vcpkg.json         # Dependency manifest with features
CMakePresets.json  # Platform-specific build configurations
Info.plist.in      # macOS app bundle template
```

**Note**: `vcpkg_installed/` directory is created during builds and should be added to .gitignore.

### Platform Support
- **Windows**: Visual Studio 2022 with static linking for distribution
- **macOS**: Native app bundle with FSEvents file watching (ARM64 and x64)
- **Linux**: Static linking for AppImage distribution
- **Cross-platform file watching**: Platform-specific implementations (Windows API, FSEvents, inotify)
- **OpenGL compatibility**: Version 330 (Windows/Linux) and 410 core (macOS)
- **Architecture support**: Both ARM64 and x64 builds

## Critical Development Notes

### Documentation Maintenance
- **When new development guidelines are provided, add them to this file**
- Keep this file updated with any new preferences or conventions
- This ensures consistency across all future sessions

### Dependency Management
**vcpkg-managed dependencies** (automatically installed during build):
- **GLFW**: Window management and OpenGL context
- **Assimp**: 3D model loading (FBX, OBJ, GLTF, etc.)
- **SQLite**: Database with FTS5, JSON1, and DBSTAT features
- **GLM**: Math library for 3D operations
- **spdlog**: Logging framework
- **Catch2**: Unit testing framework

**Source-built dependencies** (in `external/` directory - 8MB total):
- **ImGui** (3.9MB): UI framework requiring backend compilation
- **GLAD** (180KB): OpenGL loader for specific version requirements
- **NanoSVG** (124KB): SVG rendering (not in vcpkg)
- **miniaudio.h** (3.8MB): Audio metadata (not in vcpkg)
- **stb_image_write.h** (72KB): Image writing (single header)
- **fonts/** (144KB): Application resources

**Configuration files**:
- `vcpkg.json`: Dependency manifest with features
- `vcpkg-configuration.json`: Registry configuration
- `CMakePresets.json`: Platform-specific build presets

### Asset Management
- Smart incremental scanning compares filesystem state with database - no clearing on startup
- Unified event-driven processing handles both initial scan and runtime file changes
- EventProcessor provides batch operations and real-time progress tracking
- File watcher provides real-time updates without requiring manual refresh
- Texture cache system prevents redundant loading of image assets
- Asset thumbnails are generated dynamically for supported formats including SVG pre-rasterization

### Event-Driven Architecture Patterns
- **All asset changes flow through EventProcessor** - never directly manipulate database or assets vector
- **Batch processing preferred** - use `queue_events()` for multiple events rather than individual `queue_event()` calls
- **Thread-safe asset access** - always acquire `get_assets_mutex()` when reading/filtering assets
- **Smart scanning logic** - initial scan generates events based on filesystem vs database comparison
- **Progress tracking** - use EventProcessor atomic counters for real-time progress updates
- **Static method optimization** - methods that don't use instance state should be static (e.g., `generate_svg_thumbnail`)

### File Event Processing Guidelines
- **Created events**: Generated when files exist in filesystem but not in database
- **Modified events**: Generated when files exist in both but have newer timestamps
- **Deleted events**: Generated when files exist in database but not in filesystem
- **Event publishing timing**: Include debug output for event publishing performance measurement
- **FileEvent constructor**: Always use parameterized constructor `FileEvent(type, path)` - no default constructor available

### 3D Model Loading Guidelines
- **Failed model cache**: TextureManager maintains `failed_models_cache_` to prevent infinite retry loops
- **Animation-only files**: FBX files with no geometry are handled gracefully
- **Error handling**: Essential errors (Assimp failures, OpenGL errors) are preserved, verbose debugging removed
- **Incomplete scenes**: FBX files with animations are accepted even if marked incomplete by Assimp

### Git Integration and Version Control
- **Never perform git operations unless explicitly requested by user**
- **Never assume user wants to commit changes** - always wait for explicit instruction

### Current Architecture Status
- **Complete vcpkg migration**: All standard libraries managed through vcpkg (GLFW, Assimp, SQLite, GLM, spdlog, Catch2)
- **Minimal external/**: Reduced from 27MB to 8MB, only essential non-vcpkg dependencies remain
- **SQLite features**: Using vcpkg with FTS5, JSON1, and DBSTAT features enabled
- **Cross-platform compatibility**: Windows, macOS (ARM64/x64), and Linux support
- **OpenGL version handling**: Platform-specific shader compatibility (330 vs 410 core)
- **macOS native integration**: FSEvents file watching and proper app bundle structure
- **Timestamp optimization**: Removed expensive `created_or_modified_seconds` field and related Windows FILETIME operations
- **3D debugging cleanup**: Verbose model loading output removed, only essential errors/warnings remain
- **Failed model handling**: Added caching system to prevent infinite retry loops for problematic FBX files
- **Unicode support**: Full UTF-8 filesystem path handling for international asset names
- **Performance**: Initial scan optimized, no more 110-second delays on large asset directories
- **Distribution ready**: Static linking ensures self-contained executables
- **Warning-free builds**: Resolved macOS version compatibility warnings by updating deployment target to 15.0, suppressed deprecated API warnings with pragmas

### Supported Asset Types
- **Texture**: .png, .jpg, .jpeg, .gif, .bmp, .tga, .hdr, .svg (with pre-rasterization)
- **Model**: .fbx, .obj, .gltf, .glb, .dae, .blend, .3ds, .ply, .x, .md2, .md3, .mdl, .pk3
- **Sound**: .wav, .mp3, .ogg, .flac, .aac, .m4a (detection only, no playback yet)
- **Video**: .mp4, .avi, .mov, .mkv, .wmv, .webm (detection only)
- **Script**: .cs, .js, .lua, .py, .sh, .bat, .json, .xml, .yml, .yaml
- **Document**: .txt, .md, .pdf, .doc, .docx
- **Auxiliary**: .mtl, .log, .cache, .tmp, .bak (excluded from search)

# important-instruction-reminders
Do what has been asked; nothing more, nothing less.
NEVER create files unless they're absolutely necessary for achieving your goal.
ALWAYS prefer editing an existing file to creating a new one.
NEVER proactively create documentation files (*.md) or README files. Only create documentation files if explicitly requested by the User.
