#include "test_helpers.h"
#include <filesystem>
#include <chrono>
#include <iostream>

Asset create_test_asset(
    const std::string& name,
    const std::string& extension,
    AssetType type,
    const std::string& path) {
    Asset asset;
    asset.name = name;
    asset.extension = extension;
    asset.type = type;
    asset.full_path = path.empty() ? (name + extension) : path;
    asset.size = 1024; // Default size
    asset.last_modified = std::chrono::system_clock::now();
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
            case FileEventType::Modified: 
                event_type_str = "Modified";
                break;
            case FileEventType::Deleted: 
                event_type_str = "Deleted";
                break;
            default: 
                event_type_str = "Unknown";
                break;
        }
        
        std::cout << "  " << event_type_str << ": " << event.path.string();
        
        std::cout << std::endl;
    }
    
    if (events.empty()) {
        std::cout << "  (no events)" << std::endl;
    }
}