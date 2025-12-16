# Asset Vault Dependencies

This document lists all the dependencies used in the Asset Vault project and where to find them.

## Core Libraries

### SQLite (Database)
- **Version**: 3.45.1
- **Source**: https://www.sqlite.org/2024/sqlite-amalgamation-3450100.zip
- **Location**: `external/sqlite/`
- **Description**: Embedded database engine for asset metadata storage

### Dear ImGui (GUI Framework)
- **Version**: Latest from master
- **Source**: https://github.com/ocornut/imgui
- **Location**: `external/imgui/`
- **Files needed**:
  - Core: `imgui.h`, `imgui.cpp`, `imgui_demo.cpp`, `imgui_draw.cpp`, `imgui_tables.cpp`, `imgui_widgets.cpp`
  - Config: `imconfig.h`, `imgui_internal.h`
  - STB headers: `imstb_rectpack.h`, `imstb_textedit.h`, `imstb_truetype.h`
  - Backends: `imgui_impl_glfw.cpp/.h`, `imgui_impl_opengl3.cpp/.h`, `imgui_impl_opengl3_loader.h`

### GLFW (Window Management)
- **Version**: 3.3.8
- **Source**: https://github.com/glfw/glfw/releases/download/3.3.8/glfw-3.3.8.zip
- **Location**: `external/glfw/`
- **Description**: Cross-platform window and input handling

### GLAD (OpenGL Loading)
- **Version**: Custom generated
- **Source**: https://glad.dav1d.de/
- **Location**: `external/glad/`
- **Files needed**: `glad.h`, `glad.c`, `khrplatform.h`
- **Description**: OpenGL function loader

### GLM (Mathematics)
- **Version**: 0.9.9.8
- **Source**: https://github.com/g-truc/glm/archive/refs/tags/0.9.9.8.zip
- **Location**: `external/glm/`
- **Description**: Header-only math library for graphics

### Assimp (3D Model Loading)
- **Version**: 5.2.5
- **Source**: https://github.com/assimp/assimp/releases/download/v5.2.5/assimp-5.2.5-windows-vs2022.zip
- **Location**: `external/assimp/`
- **Description**: 3D model loading library with precompiled Windows binaries

### NanoSVG (SVG Rendering)
- **Version**: Latest from master
- **Source**: https://github.com/memononen/nanosvg
- **Location**: `external/nanosvg/`
- **Files needed**: `nanosvg.h`, `nanosvgrast.h`
- **Description**: Header-only SVG parsing and rasterization

## Additional Dependencies

### STB Image (Image Loading)
- **Source**: https://github.com/nothings/stb/blob/master/stb_image.h
- **Location**: `external/imgui/stb_image.h`
- **Description**: Header-only image loading library

### STB Image Write (Image Saving)
- **Source**: https://github.com/nothings/stb/blob/master/stb_image_write.h
- **Location**: `external/stb_image_write.h`
- **Description**: Header-only image writing library

### Roboto Font
- **Source**: https://github.com/google/fonts/raw/main/apache/roboto/Roboto-Regular.ttf
- **Location**: `external/fonts/Roboto-Regular.ttf`
- **Description**: Default UI font

## Default Asset Icons

The following default icons should be placed in the `images/` directory:
- `texture.png` - For image/texture files
- `model.png` - For 3D model files
- `sound.png` - For audio files
- `font.png` - For font files
- `document.png` - For text/document files
- `folder.png` - For directories
- `unknown.png` - For unrecognized file types

## Installation Notes

All dependencies are pre-configured in the CMakeLists.txt file. The project expects:
- Precompiled Windows binaries for GLFW and Assimp
- Header-only libraries placed in their respective directories
- SQLite compiled as part of the build process

## Build Instructions

```bash
# Configure
cmake -B build -S . -G "Visual Studio 17 2022" -A x64

# Build
cmake --build build --config Debug

# Run
./build/Debug/AssetVault.exe
```