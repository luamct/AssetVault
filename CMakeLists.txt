cmake_minimum_required(VERSION 3.21)
project(AssetInventory VERSION 1.0.0 LANGUAGES C CXX)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Set C standard
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Platform detection
if(WIN32)
    set(PLATFORM_WINDOWS TRUE)
elseif(APPLE)
    set(PLATFORM_MACOS TRUE)
elseif(UNIX)
    set(PLATFORM_LINUX TRUE)
endif()

# Static linking configuration for distribution builds
if(PLATFORM_WINDOWS)
    # Use static runtime for Windows distribution builds
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    # Exclude conflicting runtime libraries
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /NODEFAULTLIB:LIBCMT")
elseif(PLATFORM_MACOS)
    # macOS deployment target for compatibility
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING "Minimum OS X deployment version")
endif()

# Platform-specific compiler settings
if(MSVC)
    # Suppress warning C4201 from GLM library (nameless struct/union)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /wd4201")
    # Enable UTF-8 encoding for spdlog Unicode support
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /utf-8")
    # Enable optimizations in release builds
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /O2 /DNDEBUG")
    endif()
elseif(APPLE)
    # macOS specific flags
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wno-unused-parameter")
    # Enable C++17 filesystem support
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++17")
elseif(UNIX AND NOT APPLE)
    # Linux specific flags
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wno-unused-parameter")
endif()

# Set output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# Find required system packages
find_package(OpenGL REQUIRED)

# Find vcpkg-managed dependencies
find_package(glfw3 CONFIG REQUIRED)
find_package(assimp CONFIG REQUIRED)
find_package(unofficial-sqlite3 CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(Catch2 CONFIG REQUIRED)

# SQLite is now managed by vcpkg with features: fts5, json1, dbstat

# Add GLAD (OpenGL Loading Library) - compiled from source
set(GLAD_DIR ${CMAKE_CURRENT_SOURCE_DIR}/external/glad)
add_library(glad STATIC ${GLAD_DIR}/src/glad.c)
target_include_directories(glad PUBLIC ${GLAD_DIR}/include)

if(MSVC)
    set_property(TARGET glad PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# Add Dear ImGui - compiled from source for customization
set(IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/external/imgui)
set(IMGUI_SOURCES
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
    ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
)

add_library(imgui STATIC ${IMGUI_SOURCES})
target_include_directories(imgui PRIVATE ${IMGUI_DIR} ${IMGUI_DIR}/backends)
target_link_libraries(imgui PRIVATE glfw)

if(MSVC)
    set_property(TARGET imgui PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# File watcher sources - platform specific
set(FILE_WATCHER_SOURCES src/file_watcher.cpp)

if(PLATFORM_WINDOWS)
    list(APPEND FILE_WATCHER_SOURCES src/file_watcher_windows.cpp)
    # Windows-specific definitions
    add_definitions(
        -DWIN32_LEAN_AND_MEAN
        -D_UNICODE
        -DUNICODE
        -D_WIN32_WINNT=0x0601
        -DNOMINMAX
        -DIMGUI_IMPL_OPENGL_LOADER_GLAD
    )
elseif(PLATFORM_MACOS)
    list(APPEND FILE_WATCHER_SOURCES src/file_watcher_macos.cpp)
    add_definitions(-DIMGUI_IMPL_OPENGL_LOADER_GLAD)
endif()

# Main executable
add_executable(AssetInventory
    src/main.cpp
    src/asset.cpp
    src/database.cpp
    src/utils.cpp
    src/3d.cpp
    src/texture_manager.cpp
    src/event_processor.cpp
    src/audio_manager.cpp
    src/search.cpp
    ${FILE_WATCHER_SOURCES}
)

# Configure for app bundle on macOS
if(PLATFORM_MACOS)
    set_target_properties(AssetInventory PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in
        MACOSX_BUNDLE_BUNDLE_NAME "Asset Inventory"
        MACOSX_BUNDLE_DISPLAY_NAME "Asset Inventory"
        MACOSX_BUNDLE_IDENTIFIER "com.gamedev.assetinventory"
        MACOSX_BUNDLE_VERSION "1.0.0"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "1.0.0"
    )
endif()

# Set runtime library for main executable
if(MSVC)
    set_property(TARGET AssetInventory PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# Include directories
target_include_directories(AssetInventory PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
    ${CMAKE_CURRENT_SOURCE_DIR}/external
    ${CMAKE_CURRENT_SOURCE_DIR}/external/nanosvg
)

# Link libraries
target_link_libraries(AssetInventory PRIVATE
    glfw
    OpenGL::GL
    unofficial::sqlite3::sqlite3
    imgui
    assimp::assimp
    glad
    glm::glm
    spdlog::spdlog
)

# Platform-specific system libraries
if(PLATFORM_WINDOWS)
    target_link_libraries(AssetInventory PRIVATE legacy_stdio_definitions)
elseif(PLATFORM_MACOS)
    # macOS frameworks
    find_library(COCOA_LIBRARY Cocoa)
    find_library(IOKIT_LIBRARY IOKit)  
    find_library(COREVIDEO_LIBRARY CoreVideo)
    find_library(COREFOUNDATION_LIBRARY CoreFoundation)
    
    target_link_libraries(AssetInventory PRIVATE
        ${COCOA_LIBRARY}
        ${IOKIT_LIBRARY}
        ${COREVIDEO_LIBRARY}
        ${COREFOUNDATION_LIBRARY}
    )
endif()

# Unit test executable
add_executable(SearchTest
    tests/test_search.cpp
    tests/test_helpers.cpp
    src/search.cpp
    src/asset.cpp
    src/utils.cpp
)

if(MSVC)
    set_property(TARGET SearchTest PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# Include directories for test
target_include_directories(SearchTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/tests
    ${CMAKE_CURRENT_SOURCE_DIR}/external
    ${GLAD_DIR}/include
    ${IMGUI_DIR}
)

# Link libraries for test
target_link_libraries(SearchTest PRIVATE 
    glad 
    assimp::assimp
    glm::glm
    spdlog::spdlog
    Catch2::Catch2WithMain
)

# Enable testing
enable_testing()
add_test(NAME SearchTests COMMAND SearchTest)

# Set compiler warnings for our code
if(MSVC)
    target_compile_options(AssetInventory PRIVATE
        /W4                    # Enable all warnings for our code
        /wd4456               # Suppress variable name shadowing warnings
        /wd4244               # Suppress conversion warnings from external libraries
        /wd4996               # Suppress deprecated function warnings
        /wd4702               # Suppress unreachable code warnings from external libs
        /external:I ${CMAKE_CURRENT_SOURCE_DIR}/external
        /external:W0          # Disable all warnings for external headers
    )
elseif(APPLE)
    target_compile_options(AssetInventory PRIVATE
        -Wall
        -Wextra
        -Wno-unused-parameter
        -Wno-missing-field-initializers
    )
endif()

# Resource copying
add_custom_command(TARGET AssetInventory POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E echo "Copying Roboto font to build directory"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    ${CMAKE_CURRENT_SOURCE_DIR}/external/fonts/Roboto-Regular.ttf
    $<IF:$<BOOL:${PLATFORM_MACOS}>,$<TARGET_BUNDLE_DIR:AssetInventory>/Contents/Resources,$<TARGET_FILE_DIR:AssetInventory>>/external/fonts/Roboto-Regular.ttf
)

# Distribution packaging configuration
if(PLATFORM_MACOS)
    # Install app bundle
    install(TARGETS AssetInventory
        BUNDLE DESTINATION .
        RUNTIME DESTINATION bin
    )
    
    # Bundle any required dylibs (vcpkg handles most of this automatically)
    install(CODE "
        include(BundleUtilities)
        fixup_bundle(\"\${CMAKE_INSTALL_PREFIX}/AssetInventory.app\" \"\" \"\")
    ")
endif()

# Print build configuration summary
message(STATUS "=== AssetInventory Build Configuration ===")
message(STATUS "Platform: ${CMAKE_SYSTEM_NAME}")
message(STATUS "Compiler: ${CMAKE_CXX_COMPILER_ID}")
message(STATUS "Build Type: ${CMAKE_BUILD_TYPE}")
if(PLATFORM_WINDOWS)
    message(STATUS "Runtime Library: ${CMAKE_MSVC_RUNTIME_LIBRARY}")
    message(STATUS "Triplet: ${VCPKG_TARGET_TRIPLET}")
endif()
message(STATUS "vcpkg Integration: ${VCPKG_TOOLCHAIN}")
message(STATUS "==========================================")