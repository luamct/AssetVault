# External Dependencies

This directory contains dependencies that cannot be managed through vcpkg for specific reasons:

## Why these dependencies remain here:

### imgui/ (3.9MB)
- **Reason**: Requires compilation with our specific backends (GLFW + OpenGL3)
- **Type**: Source files compiled as static library
- **Note**: The backend files need to be compiled together with our application

### glad/ (180KB)
- **Reason**: OpenGL function loader generated for our specific OpenGL version requirements
- **Type**: Source files compiled as static library
- **Note**: Tightly coupled to our OpenGL 3.3/4.1 core profile usage

### nanosvg/ (124KB)
- **Reason**: Not available in vcpkg, header-only library
- **Type**: Header-only
- **Usage**: SVG rendering for asset thumbnails

### miniaudio.h (3.8MB)
- **Reason**: Not available in vcpkg, header-only library
- **Type**: Single header file
- **Usage**: Audio file metadata extraction (future audio playback)

### stb_image_write.h (72KB)
- **Reason**: Single header from STB collection, vcpkg provides full STB package
- **Type**: Single header file
- **Usage**: Image file writing for thumbnail generation

### fonts/ (144KB)
- **Reason**: Application resources, not a library
- **Type**: Font files
- **Usage**: UI rendering with ImGui

## Dependencies moved to vcpkg:
- GLFW (window management)
- Assimp (3D model loading)
- SQLite (database)
- GLM (math library)
- spdlog (logging)
- Catch2 (unit testing)

Total size reduced from ~27MB to ~8MB through vcpkg migration.