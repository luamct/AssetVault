#pragma once

#include "asset.h"
#include <string>

// Test helper functions for creating mock assets and other test utilities
Asset create_test_asset(
    const std::string& name,
    const std::string& extension,
    AssetType type,
    const std::string& path = ""
);