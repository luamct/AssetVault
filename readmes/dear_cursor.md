# Dear Cursor - Project Instructions

## Communication Guidelines

### Critical Feedback
- **Be candid and objective** - don't be overly positive when it's not warranted
- **Avoid unnecessary praise** like "Good catch!", "Great idea!", etc. unless truly deserved
- **Be critical of suggestions** - if something seems like a bad idea, say so directly
- **Ask for confirmation** when you think a suggestion might be problematic
- **Provide better alternatives** when rejecting an approach
- **Be honest about trade-offs** - explain why one solution might be better than another

### Technical Discussions
- **Evaluate solutions objectively** based on their merits, not just to be agreeable
- **Point out potential issues** even if they might not be immediately obvious
- **Consider maintainability, performance, and complexity** when discussing approaches
- **Don't hesitate to say "this is overkill"** or "this is unnecessarily complex" when appropriate

## General Guidelines

### Directory Management
- **Stay in the project root directory** (`D:/GameDev/AssetInventory`) when running commands
- Use **relative paths** instead of absolute paths when possible
- Run commands from the project root, not from subdirectories like `build/`

### Code Quality
- **Always try to fix warnings** - don't leave them unresolved
- Pay attention to compiler and linker warnings
- Address any build issues before proceeding

### Dependencies
- Use `download_deps.bat` to automate dependency setup
- Don't manually download dependencies unless necessary
- The batch file handles downloading GLFW, ImGui, and SQLite

### Build Process
- Use CMake for building the project
- Clean builds when needed: `rm -rf build && mkdir build && cmake -B build && cmake --build build`
- The project uses MSVC runtime library settings to avoid linker conflicts

### File Organization
- Source files are in `src/`
  - `main.cpp` - Main application entry point and UI logic
  - `utils.cpp/h` - Utility functions for string formatting, file operations
  - `database.cpp/h` - Database operations and asset storage
  - `index.cpp/h` - Asset indexing and file type detection
  - `file_watcher*.cpp/h` - File system monitoring
  - `theme.h` - UI theming configuration
- External dependencies are in `external/`
- Assets are in `assets/`
- Database files are in `db/`
- Documentation is in `readmes/`

### Database
- SQLite database is located at `db/assets.db`
- The application performs initial scans and watches for file changes
- Database is cleared on startup to avoid duplicates

### ImGui Configuration
- `imgui.ini` file is disabled to avoid persistent window state
- Window positions and sizes are not saved between sessions

### Documentation Maintenance
- **Whenever new development guidelines are provided, add them to this file**
- Keep this file updated with any new preferences or conventions
- This ensures consistency across all future sessions

## WSL Development Setup

### Initial WSL Setup
1. **Install WSL and Ubuntu-22.04** (if not already done):
   ```bash
   wsl --install -d Ubuntu-22.04
   ```

2. **Run the WSL setup script** from Windows:
   ```bash
   wsl -d Ubuntu-22.04 -- bash -c "cd /home/luam/gamedev/AssetInventory && chmod +x setup_wsl.sh && ./setup_wsl.sh"
   ```

### WSL Build Commands

```bash
# Clean build with WSL + Ninja + Clang (recommended)
./build_wsl.bat

# Or manually in WSL terminal:
wsl -d Ubuntu-22.04 --cd /home/luam/gamedev/AssetInventory -- bash -c "rm -rf build && mkdir build && cmake -B build -S . -G Ninja -DCMAKE_BUILD_TYPE=Debug && cmake --build build"

# Run the application
wsl -d Ubuntu-22.04 --cd /home/luam/gamedev/AssetInventory -- ./build/AssetInventory
```

### VS Code/Cursor WSL Integration
- **Terminal**: Automatically uses WSL Ubuntu-22.04
- **IntelliSense**: Uses Clang with WSL configuration
- **Build Tasks**: Use "WSL: CMake Build (Ninja)" for optimal builds
- **C++ Configuration**: Set to "WSL-Clang" for best IntelliSense

## Common Commands

### Windows (Legacy)
```bash
# Clean build (stays in root directory)
rm -rf build && mkdir build && cmake -B build && cmake --build build

# Run from project root
./build/AssetInventory.exe

# Download dependencies
./download_deps.bat
```

### WSL (Recommended)
```bash
# Clean build with Ninja
./build_wsl.bat

# Or manual WSL commands
wsl -d Ubuntu-22.04 --cd /home/luam/gamedev/AssetInventory -- cmake -B build -S . -G Ninja -DCMAKE_BUILD_TYPE=Debug
wsl -d Ubuntu-22.04 --cd /home/luam/gamedev/AssetInventory -- cmake --build build
```

## Project Structure
```
AssetInventory/
├── src/           # Source files
├── external/      # Dependencies (GLFW, ImGui, SQLite)
├── assets/        # Asset files to monitor
├── db/           # Database files
├── build/        # Build output (generated)
├── readmes/      # Documentation
└── CMakeLists.txt
```
