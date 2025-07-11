#!/bin/bash

# Asset Inventory - Dependency Download Script
# Downloads all required dependencies for the project

set -e  # Exit on any error

echo "Downloading dependencies for Asset Inventory..."

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Function to print colored output
print_status() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

print_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# Function to download file with error handling
download_file() {
    local url="$1"
    local output="$2"
    local description="$3"

    print_status "Downloading $description..."
    if curl -L -o "$output" "$url" 2>/dev/null; then
        print_success "Downloaded $description"
    else
        print_error "Failed to download $description from $url"
        return 1
    fi
}

# Function to extract zip file
extract_zip() {
    local zip_file="$1"
    local extract_dir="$2"
    local description="$3"

    print_status "Extracting $description..."
    if unzip -q "$zip_file" -d "$extract_dir"; then
        print_success "Extracted $description"
    else
        print_error "Failed to extract $description"
        return 1
    fi
}

# Create external directory structure
print_status "Creating directory structure..."
mkdir -p external/{imgui,glfw,fonts,sqlite,glad/{include/{glad,KHR},src},assimp/{include,lib},glm}

# Download SQLite amalgamation
print_status "Downloading SQLite amalgamation..."
download_file "https://www.sqlite.org/2024/sqlite-amalgamation-3450100.zip" "sqlite-amalgamation.zip" "SQLite amalgamation"
extract_zip "sqlite-amalgamation.zip" "temp" "SQLite amalgamation"
cp -r temp/sqlite-amalgamation-3450100/* external/sqlite/
rm -rf temp sqlite-amalgamation.zip

# Download Dear ImGui
print_status "Downloading Dear ImGui..."
IMGUI_BASE="https://raw.githubusercontent.com/ocornut/imgui/master"
IMGUI_FILES=(
    "imgui.h"
    "imgui.cpp"
    "imgui_demo.cpp"
    "imgui_draw.cpp"
    "imgui_tables.cpp"
    "imgui_widgets.cpp"
    "imconfig.h"
    "imgui_internal.h"
    "imstb_rectpack.h"
    "imstb_textedit.h"
    "imstb_truetype.h"
)

for file in "${IMGUI_FILES[@]}"; do
    download_file "$IMGUI_BASE/$file" "external/imgui/$file" "ImGui $file"
done

# Download stb_image.h
download_file "https://raw.githubusercontent.com/nothings/stb/master/stb_image.h" "external/imgui/stb_image.h" "stb_image.h"

# Download ImGui backends
mkdir -p external/imgui/backends
IMGUI_BACKENDS=(
    "imgui_impl_glfw.cpp"
    "imgui_impl_glfw.h"
    "imgui_impl_opengl3.cpp"
    "imgui_impl_opengl3.h"
    "imgui_impl_opengl3_loader.h"
)

for file in "${IMGUI_BACKENDS[@]}"; do
    download_file "$IMGUI_BASE/backends/$file" "external/imgui/backends/$file" "ImGui backend $file"
done

# Download Roboto Font
print_status "Downloading Roboto Font..."
download_file "https://github.com/google/fonts/raw/main/apache/roboto/Roboto-Regular.ttf" "external/fonts/Roboto-Regular.ttf" "Roboto font"

# Download GLFW
print_status "Downloading GLFW..."
download_file "https://github.com/glfw/glfw/releases/download/3.3.8/glfw-3.3.8.zip" "glfw.zip" "GLFW"
extract_zip "glfw.zip" "temp" "GLFW"
cp -r temp/glfw-3.3.8/* external/glfw/
rm -rf temp glfw.zip

# Download GLAD
print_status "Downloading GLAD OpenGL Loading Library..."
GLAD_BASE="https://glad.dav1d.de/generated/tmpxft8q0glad"
download_file "$GLAD_BASE/glad/glad.h" "external/glad/include/glad/glad.h" "GLAD header"
download_file "$GLAD_BASE/src/glad.c" "external/glad/src/glad.c" "GLAD source"
download_file "$GLAD_BASE/include/KHR/khrplatform.h" "external/glad/include/KHR/khrplatform.h" "KHR platform header"

# Download GLM
print_status "Downloading GLM (OpenGL Mathematics)..."
download_file "https://github.com/g-truc/glm/archive/refs/tags/0.9.9.8.zip" "external/glm/glm.zip" "GLM"
extract_zip "external/glm/glm.zip" "external/glm" "GLM"
cp -r external/glm/glm-0.9.9.8/glm/* external/glm/
rm -rf external/glm/glm-0.9.9.8 external/glm/glm.zip

# Download Assimp (try precompiled first, fallback to source)
print_status "Downloading Assimp..."
ASSIMP_DOWNLOADED=false

# Try precompiled binaries first
if download_file "https://github.com/assimp/assimp/releases/download/v5.2.5/assimp-5.2.5-windows-vs2022.zip" "external/assimp/assimp-sdk.zip" "Assimp precompiled binaries" 2>/dev/null; then
    extract_zip "external/assimp/assimp-sdk.zip" "external/assimp" "Assimp precompiled binaries"

    # Try different possible directory structures
    if [ -d "external/assimp/assimp-sdk-5.2.5-windows-VS2022" ]; then
        cp -r external/assimp/assimp-sdk-5.2.5-windows-VS2022/include/* external/assimp/include/
        cp -r external/assimp/assimp-sdk-5.2.5-windows-VS2022/lib/x64/* external/assimp/lib/
        rm -rf external/assimp/assimp-sdk-5.2.5-windows-VS2022
        ASSIMP_DOWNLOADED=true
    elif [ -d "external/assimp/assimp-5.2.5-windows-vs2022" ]; then
        cp -r external/assimp/assimp-5.2.5-windows-vs2022/include/* external/assimp/include/
        cp -r external/assimp/assimp-5.2.5-windows-vs2022/lib/x64/* external/assimp/lib/
        rm -rf external/assimp/assimp-5.2.5-windows-vs2022
        ASSIMP_DOWNLOADED=true
    fi

    if [ "$ASSIMP_DOWNLOADED" = true ]; then
        rm -f external/assimp/assimp-sdk.zip
    fi
fi

# If precompiled failed, download source
if [ "$ASSIMP_DOWNLOADED" = false ]; then
    print_warning "Precompiled Assimp binaries not available, downloading source code..."
    download_file "https://github.com/assimp/assimp/archive/refs/tags/v5.2.5.zip" "external/assimp/assimp-source.zip" "Assimp source code"
    extract_zip "external/assimp/assimp-source.zip" "external/assimp" "Assimp source code"
    cp -r external/assimp/assimp-5.2.5/include/* external/assimp/include/
    rm -rf external/assimp/assimp-5.2.5 external/assimp/assimp-source.zip

    print_warning "Assimp source code downloaded. You may need to build it manually:"
    echo "  cd external/assimp/assimp-5.2.5"
    echo "  mkdir build && cd build"
    echo "  cmake .. -G \"Visual Studio 17 2022\" -A x64"
    echo "  cmake --build . --config Release"
    echo "  Copy the built libraries to external/assimp/lib/"
fi

# Create image assets directory if it doesn't exist
mkdir -p images

# Download default images if they don't exist
print_status "Checking for default images..."
DEFAULT_IMAGES=(
    "texture.png"
    "model.png"
    "sound.png"
    "font.png"
    "document.png"
    "folder.png"
)

for image in "${DEFAULT_IMAGES[@]}"; do
    if [ ! -f "images/$image" ]; then
        print_warning "Default image images/$image not found. Please add it manually."
    fi
done

print_success "All dependencies downloaded successfully!"
echo
echo "Dependencies installed:"
echo "  - SQLite: external/sqlite/"
echo "  - Dear ImGui: external/imgui/"
echo "  - GLFW: external/glfw/"
echo "  - GLAD: external/glad/"
echo "  - GLM: external/glm/"
echo "  - Assimp: external/assimp/"
echo "  - Roboto Font: external/fonts/"
echo
echo "Next steps:"
echo "  1. Build the project: cmake -B build -S . -G \"Visual Studio 17 2022\" -A x64"
echo "  2. Build: cmake --build build --target AssetInventory"
echo "  3. Run: ./build/Debug/AssetInventory.exe"
echo
print_success "Setup complete!"
