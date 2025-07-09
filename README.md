# AssetInventory

A C++ desktop application for managing game assets, built with Dear ImGui.

## Features

- Asset indexing and management
- Real-time file system monitoring
- Cross-platform (Windows, Linux, macOS)
- Modern C++17

## Prerequisites

- Visual Studio Build Tools 2022
- CMake 3.16+
- Git

## Build

```bash
git clone https://github.com/luamct/AssetInventory.git
cd AssetInventory

# Configure and build
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug

# Run
./build/Debug/AssetInventory.exe
```

## Project Structure

```
src/           # Source files
├── main.cpp   # Main application and UI logic
├── database.cpp/h  # Database operations
├── index.cpp/h     # Asset indexing and file type detection
├── utils.cpp/h     # Utility functions
├── file_watcher*.cpp/h  # File system monitoring
└── theme.h    # UI theming
tests/         # Test files
external/      # Dependencies (ImGui, GLFW, SQLite)
assets/        # Game assets
.vscode/       # VS Code configuration
```

## Documentation

- [File Watcher System](readmes/FILE_WATCHER_README.md)
- [Database](readmes/DATABASE_README.md)
