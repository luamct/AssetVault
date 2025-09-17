#include "test_helpers.h"
#include <filesystem>
#include <chrono>
#include <iostream>
#include "utils.h"

Asset create_test_asset(
    const std::string& name,
    const std::string& extension,
    AssetType type,
    const std::string& path,
    const std::string& assets_root) {
    Asset asset;
    asset.name = name;
    asset.extension = extension;
    asset.type = type;
    asset.path = path.empty() ? (name + extension) : path;
    asset.size = 1024; // Default size
    asset.last_modified = std::chrono::system_clock::now();
    asset.relative_path = assets_root.empty()
        ? asset.path
        : get_relative_asset_path(asset.path, assets_root);
    return asset;
}

void print_file_events(const std::vector<FileEvent>& events, const std::string& label) {
    std::cout << label << " - captured " << events.size() << " events:" << std::endl;
    
    for (const auto& event : events) {
        std::string event_type_str;
        switch (event.type) {
            case FileEventType::Created: 
                event_type_str = "Created";
                break;
            case FileEventType::Deleted: 
                event_type_str = "Deleted";
                break;
            default: 
                event_type_str = "Unknown";
                break;
        }
        
        std::cout << "  " << event_type_str << ": " << event.path;
        
        std::cout << std::endl;
    }
    
    if (events.empty()) {
        std::cout << "  (no events)" << std::endl;
    }
}
