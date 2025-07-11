# AssetInventory

A C++ desktop application for managing game assets, built with Dear ImGui, OpenGL, and SQLite.

## Features

- Asset indexing and management with real-time file system monitoring
- 3D model preview with OpenGL rendering
- Texture and asset type detection
- Cross-platform (Windows, Linux, macOS)
- Modern C++17 with CMake build system

## Prerequisites

- **Windows**: Visual Studio 2022 or Build Tools 2022
- **macOS**: Xcode Command Line Tools
- **Linux**: GCC 7+ or Clang 6+
- CMake 3.16+
- Git

## Quick Start

```bash
# Clone the repository
git clone https://github.com/luamct/AssetInventory.git
cd AssetInventory

# Download dependencies (cross-platform)
./download_deps.sh

# Configure and build
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug

# Run
./build/Debug/AssetInventory.exe
```

## Dependencies

The project uses the following dependencies (automatically downloaded by `download_deps.sh`):

- **Dear ImGui** - Immediate mode GUI
- **GLFW** - Window management and input
- **GLAD** - OpenGL loading library
- **OpenGL** - 3D graphics rendering
- **GLM** - Mathematics library
- **Assimp** - 3D model loading
- **SQLite** - Database storage
- **STB Image** - Image loading

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
- Generate thumbnails for textures
- Provide 3D preview for models
- Monitor for file changes in real-time

## Documentation

- [File Watcher System](readmes/FILE_WATCHER_README.md)
- [Database](readmes/DATABASE_README.md)
- [Dear Cursor Setup](readmes/dear_cursor.md)

## License

This project is open source. See LICENSE file for details.
