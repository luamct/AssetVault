cmake_minimum_required(VERSION 3.21)

# Enable Objective-C++ for macOS platform features (drag-and-drop, etc.)
if(APPLE)
    project(AssetVault VERSION 1.0.0 LANGUAGES C CXX OBJCXX)
else()
    project(AssetVault VERSION 1.0.0 LANGUAGES C CXX)
endif()

option(ASSET_VAULT_STATIC_MSVC_RUNTIME "Link the MSVC runtime statically" OFF)
set(ASSET_VAULT_SANITIZERS "" CACHE STRING "Enable sanitizers: address, undefined, leak, thread")

if(MSVC AND ASSET_VAULT_STATIC_MSVC_RUNTIME)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" CACHE STRING "" FORCE)
endif()

# ============================================================================
# Project Configuration
# ============================================================================

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
    # Suppress warnings from external header-only libraries
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-missing-field-initializers")
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

# ============================================================================
# Dependencies
# ============================================================================

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
find_package(Python3 COMPONENTS Interpreter REQUIRED)

set(SHADER_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/unified.vert
    ${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/unified.frag
)

set(EMBEDDED_ASSETS_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/src/builder)
set(EMBEDDED_ASSETS_CPP ${EMBEDDED_ASSETS_OUTPUT_DIR}/embedded_assets_generated.cpp)

set(EMBEDDED_FONT_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/external/fonts/Roboto-Regular.ttf
    ${CMAKE_CURRENT_SOURCE_DIR}/external/fonts/ArchivoBlack-Regular.ttf
)

file(GLOB EMBEDDED_IMAGE_FILES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/images/*)
list(SORT EMBEDDED_IMAGE_FILES)

file(GLOB EMBEDDED_FONT_FILES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/external/fonts/*.ttf
    ${CMAKE_CURRENT_SOURCE_DIR}/external/fonts/*.otf)
list(SORT EMBEDDED_FONT_FILES)

set(EMBEDDED_ASSET_FILES ${SHADER_FILES})
list(APPEND EMBEDDED_ASSET_FILES ${EMBEDDED_IMAGE_FILES})
list(APPEND EMBEDDED_ASSET_FILES ${EMBEDDED_FONT_FILES})
list(SORT EMBEDDED_ASSET_FILES)

add_custom_command(
    OUTPUT ${EMBEDDED_ASSETS_CPP}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${EMBEDDED_ASSETS_OUTPUT_DIR}
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/scripts/embed_assets.py
            ${EMBEDDED_ASSETS_CPP} ${CMAKE_CURRENT_SOURCE_DIR} ${EMBEDDED_ASSET_FILES}
    DEPENDS ${EMBEDDED_ASSET_FILES} ${CMAKE_CURRENT_SOURCE_DIR}/scripts/embed_assets.py
    COMMENT "Generating embedded asset data"
    VERBATIM
)

set_source_files_properties(${EMBEDDED_ASSETS_CPP} PROPERTIES GENERATED TRUE)

# ============================================================================
# External Libraries (source-built)
# ============================================================================

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
if(PLATFORM_WINDOWS)
    target_compile_definitions(imgui PRIVATE GLFW_STATIC)
endif()

# File watcher sources - native implementations only
if(PLATFORM_WINDOWS)
    set(FILE_WATCHER_SOURCES src/file_watcher.cpp src/file_watcher_windows.cpp)
elseif(PLATFORM_MACOS)
    set(FILE_WATCHER_SOURCES src/file_watcher.cpp src/file_watcher_macos.cpp)
else()
    # Linux/other platforms would need native implementation
    message(FATAL_ERROR "File watcher not implemented for this platform")
endif()

# Drag-and-drop sources - native implementations only
if(PLATFORM_WINDOWS)
    set(DRAG_DROP_SOURCES src/drag_drop_windows.cpp)
elseif(PLATFORM_MACOS)
    set(DRAG_DROP_SOURCES src/drag_drop_macos.mm)
else()
    # Linux implementation not yet available
    set(DRAG_DROP_SOURCES)
endif()

# Platform-specific definitions
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

if(PLATFORM_WINDOWS)
    set(WINDOWS_SYSTEM_LIBS
        legacy_stdio_definitions
        kernel32
        user32
        gdi32
        shell32
        uuid
        ole32
        oleaut32
        advapi32
        ws2_32
        winmm
        imm32
        version
        setupapi
    )
endif()

# ============================================================================
# Core Library
# ============================================================================

# Core library - shared by main executable and all tests
add_library(AssetVaultCore STATIC
    src/asset.cpp
    src/database.cpp
    src/services.cpp
    src/config.cpp
    src/event_processor.cpp
    src/search.cpp
    src/texture_manager.cpp
    src/3d.cpp
    src/animation.cpp
    src/fonts.cpp
    src/utils.cpp
    src/ui/ui.cpp
    src/ui/components.cpp
    src/ui/search_panel.cpp
    src/ui/results_panel.cpp
    src/ui/preview_panel.cpp
    src/ui/tree_panel.cpp
    src/audio_manager.cpp
    src/stb_vorbis_impl.cpp
    src/builder/embedded_assets.cpp
    ${FILE_WATCHER_SOURCES}
    ${DRAG_DROP_SOURCES}
    ${EMBEDDED_ASSETS_CPP}
)

target_include_directories(AssetVaultCore PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/external
    ${CMAKE_CURRENT_SOURCE_DIR}/external/glad/include
    ${CMAKE_CURRENT_SOURCE_DIR}/external/imgui
    ${CMAKE_CURRENT_SOURCE_DIR}/external/imgui/backends
)

target_link_libraries(AssetVaultCore PUBLIC
    unofficial::sqlite3::sqlite3
    spdlog::spdlog
    glfw
    assimp::assimp
    glm::glm
    lunasvg::lunasvg
    imgui
    glad
)

if(PLATFORM_WINDOWS)
    target_compile_definitions(AssetVaultCore PUBLIC GLFW_STATIC)
endif()

# Disable warnings for external library files in core
if(APPLE OR (UNIX AND NOT APPLE))
    set_source_files_properties(src/stb_vorbis_impl.cpp PROPERTIES
        COMPILE_FLAGS "-Wno-shadow"
    )
    set_source_files_properties(src/texture_manager.cpp PROPERTIES
        COMPILE_FLAGS "-Wno-deprecated-declarations"
    )
endif()

# Common test configuration
set(TEST_INCLUDE_DIRS
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/tests
)

set(TEST_LIBRARIES
    AssetVaultCore
    Catch2::Catch2WithMain
)

# ============================================================================
# Main Executable
# ============================================================================

# Main executable - only main.cpp, links to AssetVaultCore for everything else
if(PLATFORM_WINDOWS)
    # WIN32 suppresses the console window; use a WinMain-style entry point
    add_executable(AssetVault WIN32
        src/main.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/resources/app_icon.rc
    )
else()
    add_executable(AssetVault
        src/main.cpp
    )
endif()

# Configure for app bundle on macOS
if(PLATFORM_MACOS)
    set(APP_ICON_PNG ${CMAKE_CURRENT_SOURCE_DIR}/images/vault.png)
    set(APP_ICONSET_DIR ${CMAKE_CURRENT_BINARY_DIR}/vault.iconset)
    set(APP_ICON_ICNS ${CMAKE_CURRENT_BINARY_DIR}/vault.icns)

    add_custom_command(
        OUTPUT ${APP_ICON_ICNS}
        COMMAND ${CMAKE_COMMAND} -E rm -rf ${APP_ICONSET_DIR}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${APP_ICONSET_DIR}
        COMMAND sips -z 16 16     ${APP_ICON_PNG} --out ${APP_ICONSET_DIR}/icon_16x16.png
        COMMAND sips -z 32 32     ${APP_ICON_PNG} --out ${APP_ICONSET_DIR}/icon_16x16@2x.png
        COMMAND sips -z 32 32     ${APP_ICON_PNG} --out ${APP_ICONSET_DIR}/icon_32x32.png
        COMMAND sips -z 64 64     ${APP_ICON_PNG} --out ${APP_ICONSET_DIR}/icon_32x32@2x.png
        COMMAND sips -z 128 128   ${APP_ICON_PNG} --out ${APP_ICONSET_DIR}/icon_128x128.png
        COMMAND sips -z 256 256   ${APP_ICON_PNG} --out ${APP_ICONSET_DIR}/icon_128x128@2x.png
        COMMAND sips -z 256 256   ${APP_ICON_PNG} --out ${APP_ICONSET_DIR}/icon_256x256.png
        COMMAND sips -z 512 512   ${APP_ICON_PNG} --out ${APP_ICONSET_DIR}/icon_256x256@2x.png
        COMMAND sips -z 512 512   ${APP_ICON_PNG} --out ${APP_ICONSET_DIR}/icon_512x512.png
        COMMAND sips -z 1024 1024 ${APP_ICON_PNG} --out ${APP_ICONSET_DIR}/icon_512x512@2x.png
        COMMAND iconutil -c icns ${APP_ICONSET_DIR} -o ${APP_ICON_ICNS}
        DEPENDS ${APP_ICON_PNG}
        COMMENT "Generating app icon (icns) from ${APP_ICON_PNG}"
        VERBATIM
    )

    set_source_files_properties(${APP_ICON_ICNS} PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")

    set_target_properties(AssetVault PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in
        MACOSX_BUNDLE_BUNDLE_NAME "Asset Vault"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.gamedev.assetvault"
        MACOSX_BUNDLE_BUNDLE_VERSION "1.0.0"
        MACOSX_BUNDLE_LONG_VERSION_STRING "1.0.0"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "1.0.0"
        MACOSX_BUNDLE_ICON_FILE "vault.icns"
    )
endif()

# Include directories
target_include_directories(AssetVault PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
    ${CMAKE_CURRENT_SOURCE_DIR}/external
)

# Link libraries - AssetVaultCore provides most dependencies
target_link_libraries(AssetVault PRIVATE
    AssetVaultCore
    OpenGL::GL
)

if(PLATFORM_MACOS)
    target_sources(AssetVault PRIVATE ${APP_ICON_ICNS})
endif()

# Platform-specific system libraries
if(PLATFORM_WINDOWS)
    target_link_libraries(AssetVault PRIVATE ${WINDOWS_SYSTEM_LIBS})
elseif(PLATFORM_MACOS)
    # macOS frameworks
    find_library(COCOA_LIBRARY Cocoa)
    find_library(IOKIT_LIBRARY IOKit)  
    find_library(COREVIDEO_LIBRARY CoreVideo)
    find_library(COREFOUNDATION_LIBRARY CoreFoundation)
    find_library(CORESERVICES_LIBRARY CoreServices)
    
    target_link_libraries(AssetVault PRIVATE
        ${COCOA_LIBRARY}
        ${IOKIT_LIBRARY}
        ${COREVIDEO_LIBRARY}
        ${COREFOUNDATION_LIBRARY}
        ${CORESERVICES_LIBRARY}
    )
endif()

# ============================================================================
# Tests
# ============================================================================

enable_testing()

# Unit test executable
add_executable(SearchTest
    tests/test_search.cpp
    tests/test_helpers.cpp
)

target_include_directories(SearchTest PRIVATE ${TEST_INCLUDE_DIRS})
target_link_libraries(SearchTest PRIVATE ${TEST_LIBRARIES})
if(PLATFORM_WINDOWS)
    target_link_libraries(SearchTest PRIVATE ${WINDOWS_SYSTEM_LIBS})
endif()
add_test(NAME SearchTests COMMAND SearchTest)

# Platform-specific file watcher test executables
if(PLATFORM_MACOS)
    add_executable(FileWatcherMacOSTest tests/test_file_watcher_macos.cpp tests/test_helpers.cpp)
    target_include_directories(FileWatcherMacOSTest PRIVATE ${TEST_INCLUDE_DIRS})
    target_link_libraries(FileWatcherMacOSTest PRIVATE
        ${TEST_LIBRARIES}
        ${COREFOUNDATION_LIBRARY}
        ${CORESERVICES_LIBRARY}
    )
    add_test(NAME FileWatcherMacOSTests COMMAND FileWatcherMacOSTest)
elseif(WIN32)
    add_executable(FileWatcherWindowsTest tests/test_file_watcher_windows.cpp tests/test_helpers.cpp)
    target_include_directories(FileWatcherWindowsTest PRIVATE ${TEST_INCLUDE_DIRS})
    target_link_libraries(FileWatcherWindowsTest PRIVATE ${TEST_LIBRARIES})
    target_link_libraries(FileWatcherWindowsTest PRIVATE ${WINDOWS_SYSTEM_LIBS})
    add_test(NAME FileWatcherWindowsTests COMMAND FileWatcherWindowsTest)
endif()

# Database test executable
add_executable(DbTest tests/test_db.cpp tests/test_helpers.cpp)
target_include_directories(DbTest PRIVATE ${TEST_INCLUDE_DIRS})
target_link_libraries(DbTest PRIVATE ${TEST_LIBRARIES})
if(PLATFORM_WINDOWS)
    target_link_libraries(DbTest PRIVATE ${WINDOWS_SYSTEM_LIBS})
endif()
add_test(NAME DbTests COMMAND DbTest)

# Add EventProcessor test executable
add_executable(EventProcessorTest tests/test_event_processor.cpp tests/test_helpers.cpp)
target_include_directories(EventProcessorTest PRIVATE ${TEST_INCLUDE_DIRS})
target_link_libraries(EventProcessorTest PRIVATE ${TEST_LIBRARIES})
if(PLATFORM_WINDOWS)
    target_link_libraries(EventProcessorTest PRIVATE ${WINDOWS_SYSTEM_LIBS})
endif()
add_test(NAME EventProcessorTests COMMAND EventProcessorTest)

# Add Utils test executable
add_executable(UtilsTest tests/test_utils.cpp tests/test_helpers.cpp)
target_include_directories(UtilsTest PRIVATE ${TEST_INCLUDE_DIRS})
target_link_libraries(UtilsTest PRIVATE ${TEST_LIBRARIES})
if(PLATFORM_WINDOWS)
    target_link_libraries(UtilsTest PRIVATE ${WINDOWS_SYSTEM_LIBS})
endif()
add_test(NAME UtilsTests COMMAND UtilsTest)

# Add UI test executable
add_executable(UITest tests/test_ui.cpp tests/test_helpers.cpp)
target_include_directories(UITest PRIVATE ${TEST_INCLUDE_DIRS})
target_link_libraries(UITest PRIVATE ${TEST_LIBRARIES})
if(PLATFORM_WINDOWS)
    target_link_libraries(UITest PRIVATE ${WINDOWS_SYSTEM_LIBS})
endif()
add_test(NAME UITests COMMAND UITest)

# Add Integration test executable
add_executable(IntegrationTest
    tests/test_integration.cpp
    tests/test_helpers.cpp
    src/main.cpp
)
target_include_directories(IntegrationTest PRIVATE ${TEST_INCLUDE_DIRS})
target_link_libraries(IntegrationTest PRIVATE ${TEST_LIBRARIES})

# Define MAX_FRAMES_TESTS for integration testing
target_compile_definitions(IntegrationTest PRIVATE
    MAX_FRAMES_TESTS=60
    ASSET_VAULT_ENTRYPOINT=AssetVaultIntegrationMain
)

# Platform-specific libraries for IntegrationTest
if(PLATFORM_WINDOWS)
    target_link_libraries(IntegrationTest PRIVATE ${WINDOWS_SYSTEM_LIBS})
elseif(PLATFORM_MACOS)
    target_link_libraries(IntegrationTest PRIVATE
        ${COCOA_LIBRARY}
        ${IOKIT_LIBRARY}
        ${COREVIDEO_LIBRARY}
        ${COREFOUNDATION_LIBRARY}
        ${CORESERVICES_LIBRARY}
    )
endif()

add_test(NAME IntegrationTests COMMAND IntegrationTest)
set_tests_properties(IntegrationTests PROPERTIES WORKING_DIRECTORY $<TARGET_FILE_DIR:IntegrationTest>)

# Assimp import probe (manual execution for diagnostics)
add_executable(AssimpImportProbe tests/assimp_import_probe.cpp)
target_link_libraries(AssimpImportProbe PRIVATE assimp::assimp)

# Set compiler warnings for our code
function(assetvault_enable_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE
            /W4                    # Enable all warnings for our code
            /wd4456               # Suppress variable name shadowing warnings
            /wd4244               # Suppress conversion warnings from external libraries
            /wd4996               # Suppress deprecated function warnings
            /wd4702               # Suppress unreachable code warnings from external libs
            /external:I ${CMAKE_CURRENT_SOURCE_DIR}/external
            /external:W0          # Disable all warnings for external headers
        )
    elseif(APPLE)
        target_compile_options(${target} PRIVATE
            -Wall
            -Wextra
            -Wno-unused-parameter
            -Wno-missing-field-initializers
        )
    elseif(UNIX AND NOT APPLE)
        target_compile_options(${target} PRIVATE
            -Wall
            -Wextra
            -Wno-unused-parameter
        )
    endif()
endfunction()

function(assetvault_enable_sanitizers target)
    if(ASSET_VAULT_SANITIZERS STREQUAL "")
        return()
    endif()

    if(MSVC)
        list(FIND ASSET_VAULT_SANITIZERS "address" ASAN_INDEX)
        if(ASAN_INDEX GREATER -1)
            target_compile_options(${target} PRIVATE /fsanitize=address)
            target_link_options(${target} PRIVATE /fsanitize=address)
        else()
            message(WARNING "MSVC only supports address sanitizer; ignoring unsupported sanitizers.")
        endif()
    else()
        set(sanitizer_list ${ASSET_VAULT_SANITIZERS})
        if(APPLE)
            list(REMOVE_ITEM sanitizer_list thread)
        endif()
        if(sanitizer_list)
            list(JOIN sanitizer_list "," sanitizer_flags)
            target_compile_options(${target} PRIVATE -fsanitize=${sanitizer_flags} -fno-omit-frame-pointer -g)
            target_link_options(${target} PRIVATE -fsanitize=${sanitizer_flags})
        endif()
    endif()
endfunction()

foreach(target
    AssetVaultCore
    AssetVault
    SearchTest
    FileWatcherMacOSTest
    FileWatcherWindowsTest
    DbTest
    EventProcessorTest
    UtilsTest
    UITest
    IntegrationTest
    AssimpImportProbe
)
    if(TARGET ${target})
        assetvault_enable_warnings(${target})
        assetvault_enable_sanitizers(${target})
    endif()
endforeach()

# Add shaders as sources so CMake tracks them (won't compile them, just track for changes)
target_sources(AssetVault PRIVATE ${SHADER_FILES})
set_source_files_properties(${SHADER_FILES} PROPERTIES HEADER_FILE_ONLY TRUE)

# ============================================================================
# Installation & Packaging
# ============================================================================
if(PLATFORM_MACOS)
    # Install app bundle
    install(TARGETS AssetVault
        BUNDLE DESTINATION .
        RUNTIME DESTINATION bin
    )
    
    # Bundle any required dylibs (vcpkg handles most of this automatically)
    install(CODE "
        include(BundleUtilities)
        fixup_bundle(\"\${CMAKE_INSTALL_PREFIX}/AssetVault.app\" \"\" \"\")
    ")
endif()

# Print build configuration summary
message(STATUS "=== AssetVault Build Configuration ===")
message(STATUS "Platform: ${CMAKE_SYSTEM_NAME}")
message(STATUS "Compiler: ${CMAKE_CXX_COMPILER_ID}")
message(STATUS "Build Type: ${CMAKE_BUILD_TYPE}")
if(PLATFORM_WINDOWS)
    message(STATUS "Runtime Library: ${CMAKE_MSVC_RUNTIME_LIBRARY}")
    message(STATUS "Triplet: ${VCPKG_TARGET_TRIPLET}")
endif()
message(STATUS "vcpkg Integration: ${VCPKG_TOOLCHAIN}")
message(STATUS "==========================================")
