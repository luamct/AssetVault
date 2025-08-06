#!/bin/bash

# Script to remove Windows precompiled binaries when migrating to vcpkg
# Run this script to clean up the external directory

echo "Removing Windows precompiled binaries..."

# Remove GLFW Windows binaries
if [ -d "external/glfw/lib-vc2022" ]; then
    echo "Removing external/glfw/lib-vc2022/"
    rm -rf external/glfw/lib-vc2022/
fi

# Remove Assimp Windows binaries  
if [ -d "external/assimp/lib" ]; then
    echo "Removing external/assimp/lib/"
    rm -rf external/assimp/lib/
fi

if [ -d "external/assimp/bin" ]; then
    echo "Removing external/assimp/bin/"
    rm -rf external/assimp/bin/
fi

# Keep headers for now as reference, but they'll be replaced by vcpkg headers
echo "Windows binaries cleaned up. Headers kept as reference."
echo "After verifying vcpkg build works, you can remove:"
echo "  - external/glfw/ (entirely - headers will come from vcpkg)"  
echo "  - external/assimp/ (entirely - headers will come from vcpkg)"