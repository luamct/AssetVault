cmake_minimum_required(VERSION 3.16)
project(AssetInventory VERSION 1.0.0 LANGUAGES C CXX)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Set C standard
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Set runtime library for MSVC to avoid linking conflicts
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
    # Explicitly exclude the static runtime library that's causing conflicts
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /NODEFAULTLIB:LIBCMT")
    # Suppress LNK4098 warning about runtime library conflicts
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /IGNORE:4098")
endif()

# MSVC-specific settings
if(MSVC)
    # Suppress warning C4201 from GLM library (nameless struct/union)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /wd4201")
    # Enable optimizations in release builds
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /O2 /DNDEBUG")
    endif()
endif()

# Set output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# Find required packages
find_package(OpenGL REQUIRED)

# SQLite setup - compile from source
set(SQLITE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/external/sqlite)

# Create SQLite library
add_library(sqlite3 STATIC ${SQLITE_DIR}/sqlite3.c)

# Set SQLite runtime library to match main application
if(MSVC)
    set_property(TARGET sqlite3 PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()


# Set SQLite include directories
target_include_directories(sqlite3 PRIVATE ${SQLITE_DIR})

# Set SQLite compile definitions for better performance
target_compile_definitions(sqlite3 PRIVATE
    SQLITE_ENABLE_FTS5
    SQLITE_ENABLE_JSON1
    SQLITE_ENABLE_RTREE
    SQLITE_ENABLE_UNLOCK_NOTIFY
    SQLITE_ENABLE_DBSTAT_VTAB
    SQLITE_ENABLE_LOAD_EXTENSION=1
    SQLITE_THREADSAFE=1
    SQLITE_USE_URI=1
    SQLITE_ENABLE_COLUMN_METADATA
)

# Use precompiled GLFW binaries
set(GLFW_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/glfw/include")
set(GLFW_LIBRARY "${CMAKE_CURRENT_SOURCE_DIR}/external/glfw/lib-vc2022/glfw3.lib")

# Use precompiled Assimp binaries
set(ASSIMP_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/assimp/include")
set(ASSIMP_LIBRARY "${CMAKE_CURRENT_SOURCE_DIR}/external/assimp/lib/x64/assimp-vc143-mt.lib")

# Create Assimp imported target (static library)
add_library(assimp STATIC IMPORTED)
set_target_properties(assimp PROPERTIES
    IMPORTED_LOCATION ${ASSIMP_LIBRARY}
    INTERFACE_INCLUDE_DIRECTORIES ${ASSIMP_INCLUDE_DIR}
)

message(STATUS "Using precompiled Assimp: include at ${ASSIMP_INCLUDE_DIR}, lib at ${ASSIMP_LIBRARY}")

# Set Assimp runtime library to match main application
if(MSVC)
    set_property(TARGET assimp PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

# GLM (header-only library)
set(GLM_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external")
message(STATUS "Using GLM: include at ${GLM_INCLUDE_DIR}")

# NanoSVG (header-only library)
set(NANOSVG_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/nanosvg")
message(STATUS "Using NanoSVG: include at ${NANOSVG_INCLUDE_DIR}")

# Create GLFW imported target (static library)
add_library(glfw STATIC IMPORTED)
set_target_properties(glfw PROPERTIES
    IMPORTED_LOCATION ${GLFW_LIBRARY}
    INTERFACE_INCLUDE_DIRECTORIES ${GLFW_INCLUDE_DIR}
)

message(STATUS "Using precompiled GLFW: include at ${GLFW_INCLUDE_DIR}, lib at ${GLFW_LIBRARY}")

# Set GLFW runtime library to match main application
if(MSVC)
    set_property(TARGET glfw PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

# Add GLAD (OpenGL Loading Library)
set(GLAD_DIR ${CMAKE_CURRENT_SOURCE_DIR}/external/glad)
set(GLAD_SOURCES ${GLAD_DIR}/src/glad.c)

# Create GLAD library target
add_library(glad STATIC ${GLAD_SOURCES})
target_include_directories(glad PUBLIC ${GLAD_DIR}/include)

# Set GLAD runtime library to match main application
if(MSVC)
    set_property(TARGET glad PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

# Add Dear ImGui
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

# Create ImGui library target
add_library(imgui STATIC ${IMGUI_SOURCES})
target_include_directories(imgui PRIVATE ${IMGUI_DIR} ${IMGUI_DIR}/backends ${GLFW_INCLUDE_DIR})

# Set ImGui runtime library to match main application
if(MSVC)
    set_property(TARGET imgui PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
    # Suppress warnings for ImGui
    target_compile_options(imgui PRIVATE)
endif()

# File watcher sources
set(FILE_WATCHER_SOURCES
    src/file_watcher.cpp
)

# Platform-specific file watcher sources
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

# Add executable
add_executable(AssetInventory
    src/main.cpp
    src/asset.cpp
    src/database.cpp
    src/utils.cpp
    src/3d.cpp
    src/texture_manager.cpp
    src/event_processor.cpp
    ${FILE_WATCHER_SOURCES}
)

# Set runtime library for all executables
if(MSVC)
    set_property(TARGET AssetInventory PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()



# Add OpenGL rendering test executable
add_executable(RenderingTest
    tests/test_rendering.cpp
)

if(MSVC)
    set_property(TARGET RenderingTest PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

# Include directories for rendering test
target_include_directories(RenderingTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
    ${GLFW_INCLUDE_DIR}
    ${GLAD_DIR}/include
)

# Link libraries for rendering test
target_link_libraries(RenderingTest PRIVATE
    glfw
    OpenGL::GL
    glad
    imgui
)

# Set compiler flags for our own code
if(MSVC)
    target_compile_options(AssetInventory PRIVATE
        /W4                    # Enable all warnings for our code
        /wd4456               # Suppress variable name shadowing warnings (common in header-only libs)
        /wd4244               # Suppress conversion warnings from external libraries
        /wd4996               # Suppress deprecated function warnings (like fopen)
        /wd4702               # Suppress unreachable code warnings from external libs
        /external:I ${CMAKE_CURRENT_SOURCE_DIR}/external  # Treat external directory as system headers
        /external:W0          # Disable all warnings for external headers
    )
endif()

# Suppress warnings for external libraries
if(MSVC)
    # SQLite (C library) - suppress all warnings
    target_compile_options(sqlite3 PRIVATE)
endif()

# Include directories
target_include_directories(AssetInventory PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
    ${SQLITE_DIR}
    ${GLFW_INCLUDE_DIR}
    ${ASSIMP_INCLUDE_DIR}
    ${GLM_INCLUDE_DIR}
    ${GLAD_DIR}/include
    ${NANOSVG_INCLUDE_DIR}
)



# Link libraries
target_link_libraries(AssetInventory PRIVATE
    glfw
    OpenGL::GL
    sqlite3
    imgui
    assimp
    glad
)

# Add filesystem library for MSVC
if(MSVC)
    target_link_libraries(AssetInventory PRIVATE legacy_stdio_definitions)
endif()



# Copy files with proper spacing
add_custom_command(TARGET AssetInventory POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E echo "Copying Roboto font to build directory"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    ${CMAKE_CURRENT_SOURCE_DIR}/external/fonts/Roboto-Regular.ttf
    ${CMAKE_BINARY_DIR}/external/fonts/Roboto-Regular.ttf
    COMMAND ${CMAKE_COMMAND} -E echo "Copying Assimp DLL to executable directory"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    ${CMAKE_CURRENT_SOURCE_DIR}/external/assimp/bin/x64/assimp-vc143-mt.dll
    $<TARGET_FILE_DIR:AssetInventory>/assimp-vc143-mt.dll
    COMMAND ${CMAKE_COMMAND} -E echo "Copying GLFW DLL to executable directory"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    ${CMAKE_CURRENT_SOURCE_DIR}/external/glfw/lib-vc2022/glfw3.dll
    $<TARGET_FILE_DIR:AssetInventory>/glfw3.dll
)
