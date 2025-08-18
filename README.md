# AssetInventory

A C++ desktop application for managing game assets, built with Dear ImGui, OpenGL, and SQLite.

## Features

- Asset indexing and management with real-time file system monitoring
- 3D model preview with OpenGL rendering
- Texture and image preview with thumbnail generation
- SVG support with pre-rasterized thumbnails (240px max dimension)
- Asset type detection and categorization
- Windows-focused development with Visual Studio 2022
- Modern C++17 with CMake build system

## Prerequisites

- **Windows**: Visual Studio 2022 or Build Tools 2022 (required)
- CMake 3.16+
- Git

## Quick Start

```bash
# Clone the repository
git clone https://github.com/luamct/AssetInventory.git
cd AssetInventory

# Configure and build (dependencies are pre-included)
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug

# Run
./build/Debug/AssetInventory.exe
```

## Dependencies

The project uses the following dependencies (pre-included):

- **Dear ImGui** - Immediate mode GUI
- **GLFW** - Window management and input  
- **GLAD** - OpenGL loading library
- **OpenGL** - 3D graphics rendering
- **GLM** - Mathematics library
- **Assimp** - 3D model loading
- **SQLite** - Database storage
- **STB Image** - Image loading and writing
- **NanoSVG** - SVG parsing and rasterization

See [Dependencies](readmes/DEPENDENCIES.md) for detailed information.

## Project Structure

```
src/                    # Source files
├── main.cpp           # Main application and UI logic
├── database.cpp/h     # Database operations
├── index.cpp/h        # Asset indexing and file type detection
├── utils.cpp/h        # Utility functions
├── file_watcher*.cpp/h # File system monitoring
├── 3d.cpp/h           # 3D rendering and model loading
└── theme.h            # UI theming
tests/                 # Test files
├── test_database.cpp  # Database tests
└── test_rendering.cpp # OpenGL rendering tests
external/              # Dependencies (auto-downloaded)
├── imgui/             # Dear ImGui
├── glfw/              # GLFW
├── glad/              # GLAD
├── glm/               # GLM
├── assimp/            # Assimp
├── sqlite/            # SQLite
└── fonts/             # Fonts
assets/                # Game assets (monitored)
images/                # Default asset type icons
db/                    # SQLite database files
```

## Development

### Building

```bash
# Debug build
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug

# Release build
cmake --build build --config Release
```

### Running Tests

```bash
# Database tests
cmake --build build --target DatabaseTest
./build/Debug/DatabaseTest.exe

# Rendering tests
cmake --build build --target RenderingTest
./build/Debug/RenderingTest.exe
```

### Adding Assets

Place your game assets in the `assets/` directory. The application will automatically:
- Index and categorize assets by type
- Generate thumbnails for images and textures
- Pre-rasterize SVG files to 240px PNG thumbnails with proper aspect ratios
- Provide 3D preview for models
- Monitor for file changes in real-time

## Documentation

- [Dependencies](readmes/DEPENDENCIES.md) - Complete list of dependencies and sources
- [File Watcher System](readmes/FILE_WATCHER.md) - Real-time file monitoring
- [Database](readmes/DATABASE.md) - SQLite asset storage system

## License

This project is open source. See LICENSE file for details.

## FileWatcher