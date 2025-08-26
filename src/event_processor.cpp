#include "event_processor.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <unordered_set>

#include "config.h"
#include "logger.h"
#include "search.h"
#include "texture_manager.h"
#include "utils.h"

namespace fs = std::filesystem;

EventProcessor::EventProcessor(AssetDatabase& database, std::map<std::string, Asset>& assets,
    std::atomic<bool>& search_update_needed, TextureManager& texture_manager, 
    SearchIndex& search_index, size_t batch_size)
    : database_(database), assets_(assets), search_update_needed_(search_update_needed),
    texture_manager_(texture_manager), search_index_(search_index), batch_size_(batch_size), running_(false), processing_(false), processed_count_(0),
    total_events_queued_(0), total_events_processed_(0),
    root_path_(Config::ASSET_ROOT_DIRECTORY) {
}

EventProcessor::~EventProcessor() {
    stop();
}

bool EventProcessor::start() {
    if (running_) {
        return true; // Already running
    }

    running_ = true;
    processing_thread_ = std::thread(&EventProcessor::process_events, this);

    LOG_INFO("EventProcessor started with batch size: {}", batch_size_);
    return true;
}

void EventProcessor::stop() {
    if (!running_) {
        return; // Already stopped
    }

    running_ = false;
    queue_condition_.notify_all();

    if (processing_thread_.joinable()) {
        processing_thread_.join();
    }

    LOG_INFO("EventProcessor stopped. Total processed: {}", processed_count_.load());
}

bool EventProcessor::has_asset_at_path(const std::string& path) {
    std::lock_guard<std::mutex> lock(assets_mutex_);
    return assets_.find(path) != assets_.end();
}

void EventProcessor::queue_event(const FileEvent& event) {
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        event_queue_.push(event);
        total_events_queued_++;  // Track queued events
    }
    queue_condition_.notify_one();
}

void EventProcessor::queue_events(const std::vector<FileEvent>& events) {
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        for (const auto& event : events) {
            event_queue_.push(event);
        }
        total_events_queued_ += events.size();  // Track batch queued events
    }
    queue_condition_.notify_all();
}

size_t EventProcessor::get_queue_size() const {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    return event_queue_.size();
}

void EventProcessor::process_events() {
    std::vector<FileEvent> batch;
    batch.reserve(batch_size_);

    while (running_) {
        batch.clear();

        // Wait for events or shutdown signal
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            queue_condition_.wait(lock, [this] { return !event_queue_.empty() || !running_; });

            if (!running_) {
                break;
            }

            // Collect a batch of events
            while (!event_queue_.empty() && batch.size() < batch_size_) {
                batch.push_back(event_queue_.front());
                event_queue_.pop();
            }
        }

        if (!batch.empty()) {
            processing_ = true;
            process_event_batch(batch);
            processing_ = false;
            processed_count_ += batch.size();
        }
    }
}

void EventProcessor::process_event_batch(const std::vector<FileEvent>& batch) {
    // Static locals for global performance tracking (thread-safe since only one processing thread)
    static uint64_t total_processing_time_ms = 0;
    static size_t total_assets_processed = 0;

    auto start_time = std::chrono::high_resolution_clock::now();

    // Group events by type for potential batch optimizations
    std::vector<FileEvent> created_events;
    std::vector<FileEvent> modified_events;
    std::vector<FileEvent> deleted_events;
    std::vector<FileEvent> renamed_events;

    for (const auto& event : batch) {
        switch (event.type) {
        case FileEventType::Created:
            created_events.push_back(event);
            break;
        case FileEventType::Modified:
            modified_events.push_back(event);
            break;
        case FileEventType::Deleted:
            deleted_events.push_back(event);
            break;
        case FileEventType::Renamed:
            renamed_events.push_back(event);
            break;
        }
    }

    // Process each type of event in batch for better performance
    if (!created_events.empty()) {
        process_created_events(created_events);
    }
    if (!modified_events.empty()) {
        process_modified_events(modified_events);
    }
    if (!deleted_events.empty()) {
        process_deleted_events(deleted_events);
    }
    if (!renamed_events.empty()) {
        process_renamed_events(renamed_events);
    }

    // Signal that search needs to be updated
    search_update_needed_ = true;

    // Calculate timing metrics
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

    // Update global tracking
    total_processing_time_ms += duration.count();
    total_assets_processed += batch.size();

    // Calculate averages
    double global_avg = total_assets_processed > 0 ? (double) total_processing_time_ms / total_assets_processed : 0.0;

    // Enhanced logging with both batch and global metrics
    LOG_INFO("Batch of {} assets completed. Running average of {:.2f}ms per asset ({} total assets processed)", batch.size(), global_avg, total_assets_processed);
}

// Batch processing methods for better performance

void EventProcessor::process_created_events(const std::vector<FileEvent>& events) {
    std::vector<Asset> files_to_insert;
    files_to_insert.reserve(events.size());

    // Process all files first and increment progress per file
    for (const auto& event : events) {
        try {
            Asset file_info = process_file(event.path, event.timestamp);
            files_to_insert.push_back(file_info);
            total_events_processed_++;  // Increment per file processed
        }
        catch (const std::exception& e) {
            LOG_ERROR("Error processing created event for {}: {}", event.path.u8string(), e.what());
            total_events_processed_++;  // Count failed attempts too
        }
    }

    // Batch insert to database
    if (!files_to_insert.empty()) {
        database_.insert_assets_batch(files_to_insert);

        // Batch update assets map
        std::lock_guard<std::mutex> lock(assets_mutex_);
        for (const auto& file : files_to_insert) {
            assets_[file.full_path.u8string()] = file;
            // Update search index for new asset
            search_index_.add_asset(file.id, file);
        }
    }
}

void EventProcessor::process_modified_events(const std::vector<FileEvent>& events) {
    std::vector<Asset> files_to_update;
    files_to_update.reserve(events.size());

    // Process all files first and increment progress per file
    for (const auto& event : events) {
        try {
            // Skip if file doesn't exist
            if (!fs::exists(event.path)) {
                total_events_processed_++;  // Count skipped
                continue;
            }

            Asset file_info = process_file(event.path, event.timestamp);
            
            // Preserve the existing asset ID if it exists
            {
                std::lock_guard<std::mutex> lock(assets_mutex_);
                auto it = assets_.find(event.path.u8string());
                if (it != assets_.end()) {
                    file_info.id = it->second.id;
                }
            }
            
            files_to_update.push_back(file_info);
            total_events_processed_++;  // Increment per file processed

            // Queue texture invalidation for modified texture assets
            if (file_info.type == AssetType::_2D) {
                texture_manager_.queue_texture_invalidation(event.path);
            }
        }
        catch (const std::exception& e) {
            LOG_ERROR("Error processing modified event for {}: {}", event.path.u8string(), e.what());
            total_events_processed_++;  // Count failed attempts too
        }
    }

    // Batch update database
    if (!files_to_update.empty()) {
        database_.update_assets_batch(files_to_update);

        // Batch update assets map
        std::lock_guard<std::mutex> lock(assets_mutex_);
        for (const auto& file : files_to_update) {
            assets_[file.full_path.u8string()] = file;
            // Update search index for modified asset
            search_index_.update_asset(file.id, file);
        }
    }
}

void EventProcessor::process_deleted_events(const std::vector<FileEvent>& events) {
    std::vector<std::string> paths_to_delete;
    paths_to_delete.reserve(events.size());
    
    // Collect all paths - file watchers handle the complexity of generating
    // appropriate events for each platform's behavior
    for (const auto& event : events) {
        std::string path_utf8 = event.path.u8string();
        paths_to_delete.push_back(path_utf8);
        
        total_events_processed_++;  // Increment per event processed
        
        // Queue texture invalidation for deleted assets
        texture_manager_.queue_texture_invalidation(event.path);
    }

    // Batch delete all paths from database
    if (!paths_to_delete.empty()) {
        // Get asset IDs before deletion for search index cleanup
        std::vector<uint32_t> deleted_asset_ids;
        {
            std::lock_guard<std::mutex> lock(assets_mutex_);
            for (const auto& path : paths_to_delete) {
                auto it = assets_.find(path);
                if (it != assets_.end() && it->second.id > 0) {
                    deleted_asset_ids.push_back(it->second.id);
                }
            }
        }

        database_.delete_assets_batch(paths_to_delete);

        // Remove from in-memory assets map and search index
        std::lock_guard<std::mutex> lock(assets_mutex_);
        for (size_t i = 0; i < paths_to_delete.size(); ++i) {
            const auto& path = paths_to_delete[i];
            assets_.erase(path);
            // Remove from search index if we have the ID
            if (i < deleted_asset_ids.size()) {
                search_index_.remove_asset(deleted_asset_ids[i]);
            }
        }
    }
}

void EventProcessor::process_renamed_events(const std::vector<FileEvent>& events) {
    // Handle renames based on whether destination is within watched directory
    std::vector<std::string> paths_to_delete;  // For moves outside watched directory
    std::vector<std::string> old_paths;        // For renames within watched directory
    std::vector<Asset> new_files;              // For renames within watched directory

    paths_to_delete.reserve(events.size());
    old_paths.reserve(events.size());
    new_files.reserve(events.size());

    for (const auto& event : events) {
        try {
            std::string event_path = event.path.u8string();
            
            // Check if the rename destination is within our watched directory
            bool destination_in_watched_dir = event_path.find(root_path_) == 0;
            
            if (destination_in_watched_dir && std::filesystem::exists(event.path)) {
                // File renamed/moved within watched directory - treat as proper rename
                LOG_DEBUG("Rename event: '{}' moved within watched directory, treating as rename", event_path);
                
                // Since FSEvents only gives us the destination path, we need to find the old path
                // by looking for assets that no longer exist
                // For now, treat this as creation since we don't have the old path
                Asset file_info = process_file(event.path, event.timestamp);
                new_files.push_back(file_info);
                
                // Queue texture invalidation for new path if it's a texture asset
                if (file_info.type == AssetType::_2D) {
                    texture_manager_.queue_texture_invalidation(event.path);
                }
            } else {
                // File moved outside watched directory (e.g., to Trash) - treat as deletion
                LOG_DEBUG("Rename event: '{}' moved outside watched directory, treating as deletion", event_path);
                paths_to_delete.push_back(event_path);
                
                // Queue texture invalidation for deleted asset
                texture_manager_.queue_texture_invalidation(event.path);
            }
            
            total_events_processed_++;  // Increment per file processed
        }
        catch (const std::exception& e) {
            LOG_ERROR("Error processing renamed event for {}: {}", event.path.u8string(), e.what());
            total_events_processed_++;  // Count failed attempts too
        }
    }

    // Handle deletions (moves outside watched directory)
    if (!paths_to_delete.empty()) {
        // Get asset IDs before deletion for search index cleanup
        std::vector<uint32_t> deleted_asset_ids;
        {
            std::lock_guard<std::mutex> lock(assets_mutex_);
            for (const auto& path : paths_to_delete) {
                auto it = assets_.find(path);
                if (it != assets_.end() && it->second.id > 0) {
                    deleted_asset_ids.push_back(it->second.id);
                }
            }
        }

        database_.delete_assets_batch(paths_to_delete);

        // Remove from in-memory assets map and search index
        std::lock_guard<std::mutex> lock(assets_mutex_);
        for (size_t i = 0; i < paths_to_delete.size(); ++i) {
            const auto& path = paths_to_delete[i];
            assets_.erase(path);
            // Remove from search index if we have the ID
            if (i < deleted_asset_ids.size()) {
                search_index_.remove_asset(deleted_asset_ids[i]);
            }
        }
    }

    // Handle creations (renames within watched directory)
    if (!new_files.empty()) {
        database_.insert_assets_batch(new_files);

        // Add to in-memory assets map
        std::lock_guard<std::mutex> lock(assets_mutex_);
        for (const auto& file : new_files) {
            assets_[file.full_path.u8string()] = file;
            // Add to search index for new asset
            search_index_.add_asset(file.id, file);
        }
    }
}

// Individual asset manipulation methods (still used by batch processing)

void EventProcessor::add_asset(const Asset& asset) {
    std::lock_guard<std::mutex> lock(assets_mutex_);
    assets_[asset.full_path.u8string()] = asset;
}

void EventProcessor::update_asset(const Asset& asset) {
    std::lock_guard<std::mutex> lock(assets_mutex_);
    assets_[asset.full_path.u8string()] = asset;
}

void EventProcessor::remove_asset(const std::string& path) {
    std::lock_guard<std::mutex> lock(assets_mutex_);
    assets_.erase(path);
}

void EventProcessor::rename_asset(const std::string& old_path, const std::string& new_path) {
    std::lock_guard<std::mutex> lock(assets_mutex_);

    auto it = assets_.find(old_path);
    if (it != assets_.end()) {
        Asset asset = it->second;
        assets_.erase(it);
        asset.full_path = fs::u8path(new_path);
        asset.name = fs::path(new_path).filename().u8string();
        assets_[new_path] = asset;
    }
}



Asset EventProcessor::process_file(const std::filesystem::path& full_path, const std::chrono::system_clock::time_point& timestamp) {
    Asset file_info;

    try {
        fs::path root(root_path_);

        // Basic file information (normalize path separators for consistent storage)
        file_info.full_path = fs::u8path(normalize_path_separators(full_path.u8string()));
        file_info.name = full_path.filename().u8string();
        file_info.is_directory = fs::is_directory(full_path);

        if (!file_info.is_directory) {
            // File-specific information
            file_info.extension = full_path.extension().string();
            file_info.type = get_asset_type(file_info.extension);

            try {
                file_info.size = fs::file_size(full_path);
                // Store display time as modification time (for user-facing display)
                try {
                    auto ftime = fs::last_write_time(full_path);
                    auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                        ftime - fs::file_time_type::clock::now() + std::chrono::system_clock::now()
                    );
                    file_info.last_modified = sctp;
                }
                catch (const fs::filesystem_error& e) {
                    // Fallback to provided timestamp for display
                    file_info.last_modified = timestamp;
                    LOG_WARN("Using provided timestamp for display for {}: {}", full_path.u8string(), e.what());
                }
            }
            catch (const fs::filesystem_error& e) {
                LOG_WARN("Could not get file info for {}: {}", file_info.full_path.u8string(), e.what());
                file_info.size = 0;
                file_info.last_modified = timestamp;
            }


            // Note: SVG and 3D model thumbnail generation is handled on-demand in get_asset_texture()
            // to avoid OpenGL context issues when called from background threads
        }
        else {
            // Directory-specific information (should never reach here due to file watcher filtering)
            file_info.type = AssetType::Directory;
            file_info.extension = "";
            file_info.size = 0;

            // Store display time as modification time (for user-facing display)
            try {
                auto ftime = fs::last_write_time(full_path);
                auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                    ftime - fs::file_time_type::clock::now() + std::chrono::system_clock::now()
                );
                file_info.last_modified = sctp;
            }
            catch (const fs::filesystem_error& e) {
                LOG_WARN("Could not get modification time for directory {}: {}", file_info.full_path.u8string(), e.what());
                file_info.last_modified = timestamp; // Fallback to provided timestamp
            }
        }
    }
    catch (const fs::filesystem_error& e) {
        LOG_ERROR("Error creating file info for {}: {}", full_path.u8string(), e.what());
        // Return minimal file info on error
        file_info.full_path = fs::u8path(normalize_path_separators(full_path.u8string()));
        file_info.name = full_path.filename().u8string();
        file_info.last_modified = timestamp;
    }

    return file_info;
}
