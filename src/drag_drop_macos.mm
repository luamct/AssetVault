#import <Cocoa/Cocoa.h>
#include "drag_drop.h"
#include "logger.h"

#define GLFW_EXPOSE_NATIVE_COCOA
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#include <imgui.h>

// Custom NSView category to handle dragging source
@interface NSView (DragDropExtension) <NSDraggingSource>
@end

@implementation NSView (DragDropExtension)

- (NSDragOperation)draggingSession:(NSDraggingSession *)session
    sourceOperationMaskForDraggingContext:(NSDraggingContext)context {
    // Support Copy operation for dragging files
    return NSDragOperationCopy;
}

- (void)draggingSession:(NSDraggingSession *)session
           endedAtPoint:(NSPoint)screenPoint
              operation:(NSDragOperation)operation {
    // Drag session ended - could add logging or cleanup here if needed
}

@end

// macOS implementation of DragDropManager
class MacOSDragDropManager : public DragDropManager {
private:
    NSWindow* ns_window_;
    NSView* content_view_;
    bool initialized_;

public:
    MacOSDragDropManager() : ns_window_(nil), content_view_(nil), initialized_(false) {}

    ~MacOSDragDropManager() override {
        // NSWindow and NSView are managed by GLFW, don't release
    }

    bool initialize(GLFWwindow* window) override {
        if (!window) {
            LOG_ERROR("[DragDrop] Invalid GLFW window");
            return false;
        }

        // Get native macOS window from GLFW
        ns_window_ = glfwGetCocoaWindow(window);
        if (!ns_window_) {
            LOG_ERROR("[DragDrop] Failed to get NSWindow from GLFW window");
            return false;
        }

        // Get content view
        content_view_ = [ns_window_ contentView];
        if (!content_view_) {
            LOG_ERROR("[DragDrop] Failed to get content view from NSWindow");
            return false;
        }

        initialized_ = true;
        LOG_INFO("[DragDrop] macOS drag-and-drop initialized successfully");
        return true;
    }

    bool begin_file_drag(const std::vector<std::string>& file_paths, const ImVec2& drag_origin) override {
        if (!initialized_) {
            LOG_WARN("[DragDrop] DragDropManager not initialized");
            return false;
        }

        if (file_paths.empty()) {
            LOG_WARN("[DragDrop] No files to drag");
            return false;
        }

        @autoreleasepool {
            // Create array to hold all dragging items
            NSMutableArray* draggingItems = [NSMutableArray arrayWithCapacity:file_paths.size()];

            // Convert ImGui screen coordinates to NSView coordinates once
            NSRect contentFrame = [content_view_ frame];
            CGFloat cocoa_y = contentFrame.size.height - drag_origin.y;
            NSRect draggingFrame = NSMakeRect(drag_origin.x, cocoa_y, 64, 64);

            // Create dragging item for each file
            for (const auto& file_path : file_paths) {
                NSString* nsFilePath = [NSString stringWithUTF8String:file_path.c_str()];
                NSURL* fileURL = [NSURL fileURLWithPath:nsFilePath];

                if (!fileURL) {
                    LOG_WARN("[DragDrop] Failed to create file URL for: {}", file_path);
                    continue;
                }

                NSDraggingItem* draggingItem = [[NSDraggingItem alloc] initWithPasteboardWriter:fileURL];
                [draggingItem setDraggingFrame:draggingFrame contents:[NSImage imageNamed:NSImageNameMultipleDocuments]];
                [draggingItems addObject:draggingItem];
            }

            if ([draggingItems count] == 0) {
                LOG_ERROR("[DragDrop] Failed to create any dragging items");
                return false;
            }

            // Create a synthetic mouse event for the drag session
            // Note: We need a valid NSEvent to start dragging
            NSEvent* currentEvent = [NSApp currentEvent];
            if (!currentEvent) {
                // Create a synthetic event if no current event exists
                NSPoint point = NSMakePoint(drag_origin.x, cocoa_y);
                currentEvent = [NSEvent mouseEventWithType:NSEventTypeLeftMouseDragged
                                                  location:point
                                             modifierFlags:0
                                                 timestamp:[[NSProcessInfo processInfo] systemUptime]
                                              windowNumber:[ns_window_ windowNumber]
                                                   context:nil
                                               eventNumber:0
                                                clickCount:1
                                                  pressure:1.0];
            }

            // Begin dragging session
            [content_view_ beginDraggingSessionWithItems:draggingItems
                                                   event:currentEvent
                                                  source:content_view_];

            LOG_DEBUG("[DragDrop] Started drag for {} file(s)", file_paths.size());
            return true;
        }
    }

    bool is_supported() const override {
        return true; // macOS always supports drag-and-drop
    }
};

// Factory function implementation for macOS
DragDropManager* create_drag_drop_manager() {
    return new MacOSDragDropManager();
}
