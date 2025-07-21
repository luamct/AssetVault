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

### Primary Build (CMake)
```bash
# Clean build (always run from project root)
rm -rf build && mkdir build && cmake -B build && cmake --build build

# Run application
./build/Debug/AssetInventory.exe

# Run tests
cmake --build build --target DatabaseTest
./build/Debug/DatabaseTest.exe
cmake --build build --target RenderingTest
./build/Debug/RenderingTest.exe
```

### WSL Build (Recommended)
```bash
# Clean build with Ninja + Clang
./build_wsl.bat

# Manual WSL build
wsl -d Ubuntu-22.04 --cd /home/luam/gamedev/AssetInventory -- cmake -B build -S . -G Ninja -DCMAKE_BUILD_TYPE=Debug
wsl -d Ubuntu-22.04 --cd /home/luam/gamedev/AssetInventory -- cmake --build build

# Run application
wsl -d Ubuntu-22.04 --cd /home/luam/gamedev/AssetInventory -- ./build/AssetInventory
```

### Dependency Management
```bash
# Download all dependencies
./download_deps.bat

# Download 3D dependencies specifically
./download_3d_deps.bat
```

### Directory Management
- **Always stay in project root directory** (`D:/GameDev/AssetInventory`) when running commands
- **Use relative paths** instead of absolute paths when possible
- **Never run commands from subdirectories** like `build/`

## Architecture Overview

### Core Components
- **main.cpp**: Entry point with ImGui-based UI, asset grid rendering, and texture management
- **database.cpp/h**: SQLite-based asset storage with full CRUD operations and batch processing
- **index.cpp/h**: Asset type detection, file scanning, and metadata extraction
- **file_watcher*.cpp/h**: Cross-platform file system monitoring with real-time updates
- **3d.cpp/h**: OpenGL-based 3D model rendering with multi-material support via Assimp
- **utils.cpp/h**: String formatting, file operations, and utility functions
- **theme.h**: Centralized UI color management system

### Asset Type System
The `AssetType` enum in `index.h` defines supported asset categories. When adding new types:
1. Add enum value to `AssetType` in `index.h`
2. Update `get_asset_type_string()` in `index.cpp`
3. Update `get_asset_type_from_string()` in `index.cpp`
4. Add texture icon mapping in `load_type_textures()` in `main.cpp`
5. Add file extension mapping in `get_asset_type()` in `index.cpp` if needed

### Database Schema
SQLite database at `db/assets.db` stores asset metadata with the following key operations:
- Initial scan clears database to avoid duplicates
- Real-time file monitoring updates database automatically
- Batch operations for performance during large scans
- Full-text search capabilities for asset discovery

### 3D Rendering Pipeline
OpenGL-based rendering system with:
- Assimp integration for model loading (FBX, OBJ, GLTF, etc.)
- Multi-material support with texture loading
- Framebuffer-based preview rendering
- Automatic model bounds calculation for camera positioning

### Threading Model
- Main UI thread handles ImGui rendering and user interaction
- Background thread for initial asset scanning with progress reporting
- File watcher runs on separate thread with thread-safe event queue
- Atomic variables for cross-thread communication

## Development Guidelines

### Code Quality Standards
- **Always fix compiler warnings** - don't leave them unresolved
- **Avoid global variables** unless for performance or multi-threading (they make testing harder)
- **Follow DRY principle** - create functions to eliminate duplicate code patterns
- **Use centralized color management** - all colors defined in `src/theme.h` using `Theme::` namespace
  - Never hardcode color values directly in code
  - Use existing constants (e.g., `Theme::BACKGROUND_LIGHT_BLUE_1`, `Theme::ACCENT_BLUE_1`)
  - Add new colors with descriptive names using numbered suffixes

### Asset Type System Management
When adding new `AssetType` enum values:
1. Add to enum in `index.h`
2. Add case to `get_asset_type_string()` in `index.cpp`
3. Add case to `get_asset_type_from_string()` in `index.cpp`
4. Add texture icon mapping in `load_type_textures()` in `main.cpp`
5. Add file extension mapping in `get_asset_type()` in `index.cpp` if needed
- **Never manually convert strings to enums** - always use `get_asset_type_from_string()`

### Database Management
- SQLite database located at `db/assets.db`
- Database cleared on startup to avoid duplicates
- Application performs initial scans and watches for file changes

### ImGui Configuration
- `imgui.ini` file disabled to avoid persistent window state
- Window positions and sizes not saved between sessions

### Build Configuration
- Uses C++17 standard with CMake 3.16+
- MSVC runtime library settings configured to avoid linker conflicts
- External dependencies are precompiled binaries in `external/` directory
- Generates `compile_commands.json` for Clangd integration

### Directory Structure
```
src/               # Source files with clear separation of concerns
  ├── main.cpp     # Main application entry point and UI logic
  ├── utils.cpp/h  # Utility functions for string formatting, file operations
  ├── database.cpp/h # Database operations and asset storage
  ├── index.cpp/h  # Asset indexing and file type detection
  ├── 3d.cpp/h     # 3D model rendering and viewport functionality
  ├── file_watcher*.cpp/h # File system monitoring
  └── theme.h      # UI theming configuration
external/          # Downloaded dependencies (GLFW, ImGui, Assimp, SQLite, etc.)
assets/            # Asset files for monitoring and indexing
db/                # SQLite database files
build/             # Generated build output
tests/             # Unit tests for database and rendering
readmes/           # Component-specific documentation
```

### Platform Support
- **Windows**: Visual Studio 2022 with precompiled binaries
- **WSL/Linux**: Clang + Ninja build system (recommended for development)
- **WSL Integration**: Terminal uses WSL Ubuntu-22.04, IntelliSense uses Clang configuration
- Cross-platform file watching with platform-specific implementations

## Critical Development Notes

### Documentation Maintenance
- **When new development guidelines are provided, add them to this file**
- Keep this file updated with any new preferences or conventions
- This ensures consistency across all future sessions

### Dependency Management
- Use `download_deps.bat` and `download_3d_deps.bat` for automated setup
- Don't manually download dependencies unless necessary
- Batch files handle GLFW, ImGui, SQLite, and 3D libraries

### Asset Management
- Database is cleared on application startup to ensure consistency
- File watcher provides real-time updates without requiring manual refresh
- Texture cache system prevents redundant loading of image assets
- Asset thumbnails are generated dynamically for supported formats

### WSL Development Setup
- **Install WSL**: `wsl --install -d Ubuntu-22.04`
- **Setup script**: `wsl -d Ubuntu-22.04 -- bash -c "cd /home/luam/gamedev/AssetInventory && chmod +x setup_wsl.sh && ./setup_wsl.sh"`
- **VS Code Integration**: Terminal uses WSL Ubuntu-22.04, build tasks use "WSL: CMake Build (Ninja)"
- **C++ Configuration**: Set to "WSL-Clang" for best IntelliSense

### Git Integration and Version Control
- **Never perform git operations unless explicitly requested by user**
- **Never assume user wants to commit changes** - always wait for explicit instruction
- Build system generates `compile_commands.json` for IDE integration