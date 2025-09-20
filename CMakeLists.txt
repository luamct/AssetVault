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

if(PLATFORM_MACOS)
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
    # macOS specific flags - strict warnings to catch all issues in our code
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wpedantic -Wno-unused-parameter -Wunused-variable -Wunused")
    # Enable shadow warnings but only for our code, not external libraries
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wshadow")
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
find_package(lunasvg CONFIG REQUIRED)

# SQLite is now managed by vcpkg with features: fts5, json1, dbstat

# Add GLAD (OpenGL Loading Library) - compiled from source
set(GLAD_DIR ${CMAKE_CURRENT_SOURCE_DIR}/external/glad)
add_library(glad STATIC ${GLAD_DIR}/src/glad.c)
target_include_directories(glad PUBLIC ${GLAD_DIR}/include)

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

# File watcher sources - native implementations only
if(PLATFORM_WINDOWS)
    set(FILE_WATCHER_SOURCES src/file_watcher.cpp src/file_watcher_windows.cpp)
elseif(PLATFORM_MACOS)
    set(FILE_WATCHER_SOURCES src/file_watcher.cpp src/file_watcher_macos.cpp)
else()
    # Linux/other platforms would need native implementation
    message(FATAL_ERROR "File watcher not implemented for this platform")
endif()

# Platform-specific definitions (still needed for other parts)
if(PLATFORM_WINDOWS)
    add_definitions(
        -DWIN32_LEAN_AND_MEAN
        -D_UNICODE
        -DUNICODE
        -D_WIN32_WINNT=0x0601
        -DNOMINMAX
        -DIMGUI_IMPL_OPENGL_LOADER_GLAD
    )
elseif(PLATFORM_MACOS)
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
    src/stb_vorbis_impl.cpp
    src/search.cpp
    src/ui.cpp
    ${FILE_WATCHER_SOURCES}
)

# Disable warnings for external library files
if(APPLE OR (UNIX AND NOT APPLE))
    set_source_files_properties(src/stb_vorbis_impl.cpp PROPERTIES
        COMPILE_FLAGS "-Wno-shadow"
    )
    set_source_files_properties(src/texture_manager.cpp PROPERTIES
        COMPILE_FLAGS "-Wno-deprecated-declarations"
    )
endif()

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

# Include directories
target_include_directories(AssetInventory PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
    ${CMAKE_CURRENT_SOURCE_DIR}/external
)

# Link libraries
target_link_libraries(AssetInventory PRIVATE
    OpenGL::GL
    unofficial::sqlite3::sqlite3
    imgui
    assimp::assimp
    glad
    glm::glm
    spdlog::spdlog
    lunasvg::lunasvg
)

# Platform-specific system libraries
if(PLATFORM_WINDOWS)
    target_link_libraries(AssetInventory PRIVATE 
        legacy_stdio_definitions
        # Additional Windows runtime libraries for static linking
        kernel32 user32 shell32 uuid ole32 oleaut32 advapi32 ws2_32
    )
elseif(PLATFORM_MACOS)
    # macOS frameworks
    find_library(COCOA_LIBRARY Cocoa)
    find_library(IOKIT_LIBRARY IOKit)  
    find_library(COREVIDEO_LIBRARY CoreVideo)
    find_library(COREFOUNDATION_LIBRARY CoreFoundation)
    find_library(CORESERVICES_LIBRARY CoreServices)
    
    target_link_libraries(AssetInventory PRIVATE
        ${COCOA_LIBRARY}
        ${IOKIT_LIBRARY}
        ${COREVIDEO_LIBRARY}
        ${COREFOUNDATION_LIBRARY}
        ${CORESERVICES_LIBRARY}
    )
endif()

# Create a shared library for test utilities to avoid recompilation
add_library(AssetInventoryCore STATIC
    src/asset.cpp
    src/database.cpp
    src/search.cpp
    src/utils.cpp
)

target_include_directories(AssetInventoryCore PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/external
)

target_link_libraries(AssetInventoryCore PUBLIC
    unofficial::sqlite3::sqlite3
    spdlog::spdlog
)

# Unit test executable
add_executable(SearchTest
    tests/test_search.cpp
    tests/test_helpers.cpp
)

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
    AssetInventoryCore
    glad 
    assimp::assimp
    glm::glm
    Catch2::Catch2WithMain
)

# Platform-specific libraries for SearchTest
if(PLATFORM_WINDOWS)
    target_link_libraries(SearchTest PRIVATE 
        legacy_stdio_definitions
        kernel32 user32 shell32 uuid ole32 oleaut32 advapi32 ws2_32
    )
endif()

# Platform-specific file watcher test executables
if(PLATFORM_MACOS)
    add_executable(FileWatcherMacOSTest
        tests/test_file_watcher_macos.cpp
        tests/test_helpers.cpp
        ${FILE_WATCHER_SOURCES}
    )

    # Include directories for macOS file watcher test
    target_include_directories(FileWatcherMacOSTest PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/tests
        ${CMAKE_CURRENT_SOURCE_DIR}/external
        ${GLAD_DIR}/include
        ${IMGUI_DIR}
    )

    # Link libraries for macOS file watcher test
    target_link_libraries(FileWatcherMacOSTest PRIVATE
        AssetInventoryCore
        glad
        assimp::assimp
        glm::glm
        Catch2::Catch2WithMain
        ${COREFOUNDATION_LIBRARY}
        ${CORESERVICES_LIBRARY}
    )
elseif(WIN32)
    add_executable(FileWatcherWindowsTest
        tests/test_file_watcher_windows.cpp
        tests/test_helpers.cpp
        ${FILE_WATCHER_SOURCES}
    )

    # Include directories for Windows file watcher test
    target_include_directories(FileWatcherWindowsTest PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/tests
        ${CMAKE_CURRENT_SOURCE_DIR}/external
        ${GLAD_DIR}/include
        ${IMGUI_DIR}
    )

    # Link libraries for Windows file watcher test
    target_link_libraries(FileWatcherWindowsTest PRIVATE
        AssetInventoryCore
        glad
        assimp::assimp
        glm::glm
        Catch2::Catch2WithMain
    )
    
    add_test(NAME FileWatcherWindowsTests COMMAND FileWatcherWindowsTest)
endif()

# Enable testing
enable_testing()
add_test(NAME SearchTests COMMAND SearchTest)

# Add Database test executable
add_executable(DbTest
    tests/test_db.cpp
)

target_include_directories(DbTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/tests
)

target_link_libraries(DbTest PRIVATE
    AssetInventoryCore
    Catch2::Catch2WithMain
)

add_test(NAME DbTests COMMAND DbTest)


# Add EventProcessor test executable
add_executable(EventProcessorTest
    tests/test_event_processor.cpp
)

target_include_directories(EventProcessorTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/tests
)

target_link_libraries(EventProcessorTest PRIVATE
    AssetInventoryCore
    Catch2::Catch2WithMain
)

add_test(NAME EventProcessorTests COMMAND EventProcessorTest)

# Add Utils test executable
add_executable(UtilsTest
    tests/test_utils.cpp
    tests/test_helpers.cpp
)

target_include_directories(UtilsTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/tests
)

target_link_libraries(UtilsTest PRIVATE
    AssetInventoryCore
    Catch2::Catch2WithMain
)

add_test(NAME UtilsTests COMMAND UtilsTest)

if(PLATFORM_MACOS)
    add_test(NAME FileWatcherMacOSTests COMMAND FileWatcherMacOSTest)
endif()

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
    
    COMMAND ${CMAKE_COMMAND} -E echo "Copying images directory to build directory"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_CURRENT_SOURCE_DIR}/images
    $<IF:$<BOOL:${PLATFORM_MACOS}>,$<TARGET_BUNDLE_DIR:AssetInventory>/Contents/Resources,$<TARGET_FILE_DIR:AssetInventory>>/images
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
