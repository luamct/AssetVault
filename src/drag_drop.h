#pragma once

#include <string>
#include <vector>

// Forward declarations
struct GLFWwindow;
struct ImVec2;

// Cross-platform drag-and-drop manager
// Allows dragging files from the application to external OS applications (Finder, Explorer, etc.)
class DragDropManager {
public:
    virtual ~DragDropManager() = default;

    // Initialize with GLFW window to get native platform window handle
    virtual bool initialize(GLFWwindow* window) = 0;

    // Begin a drag operation for one or more files
    // Parameters:
    //   - file_paths: Absolute paths to the files to drag (supports multiple files)
    //   - drag_origin: Screen position where drag started (for drag image positioning)
    // Returns: true if drag was initiated successfully
    virtual bool begin_file_drag(const std::vector<std::string>& file_paths, const ImVec2& drag_origin) = 0;

    // Check if drag-and-drop is supported on this platform
    virtual bool is_supported() const = 0;
};

// Factory function to create platform-specific implementation
DragDropManager* create_drag_drop_manager();
