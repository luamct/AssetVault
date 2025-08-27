#pragma once

#include "asset.h"
#include "file_watcher.h"
#include <string>
#include <vector>

// Test helper functions for creating mock assets and other test utilities
Asset create_test_asset(
    const std::string& name,
    const std::string& extension,
    AssetType type,
    const std::string& path = ""
);

// Helper function to print FileEvents for debugging tests
void print_file_events(const std::vector<FileEvent>& events, const std::string& label = "Events");