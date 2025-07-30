#include "event_processor.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <unordered_set>

#include "config.h"
#include "logger.h"
#include "texture_manager.h"

namespace fs = std::filesystem;

EventProcessor::EventProcessor(AssetDatabase& database, std::vector<Asset>& assets,
    std::atomic<bool>& search_update_needed, TextureManager& texture_manager, size_t batch_size)
    : database_(database), assets_(assets), search_update_needed_(search_update_needed),
    texture_manager_(texture_manager), batch_size_(batch_size), running_(false), processing_(false), processed_count_(0),
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
        case FileEventType::DirectoryCreated:
            created_events.push_back(event);
            break;
        case FileEventType::Modified:
            modified_events.push_back(event);
            break;
        case FileEventType::Deleted:
        case FileEventType::DirectoryDeleted:
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

        // Batch update assets vector
        std::lock_guard<std::mutex> lock(assets_mutex_);
        assets_.reserve(assets_.size() + files_to_insert.size());
        for (const auto& file : files_to_insert) {
            assets_.push_back(file);
        }
    }
}

void EventProcessor::process_modified_events(const std::vector<FileEvent>& events) {
    std::vector<Asset> files_to_update;
    files_to_update.reserve(events.size());

    // Process all files first and increment progress per file
    for (const auto& event : events) {
        try {
            // Skip directory modifications - content changes will be handled as individual file events
            if (!fs::exists(event.path) || fs::is_directory(event.path)) {
                total_events_processed_++;  // Count skipped directories
                continue;
            }

            Asset file_info = process_file(event.path, event.timestamp);
            files_to_update.push_back(file_info);
            total_events_processed_++;  // Increment per file processed

            // Queue texture invalidation for modified texture assets
            if (file_info.type == AssetType::Texture) {
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

        // Batch update assets vector
        std::lock_guard<std::mutex> lock(assets_mutex_);
        for (const auto& file : files_to_update) {
            int index = find_asset_index(file.full_path.u8string());
            if (index >= 0) {
                assets_[index] = file;
            }
            else {
                // Asset not found, add it
                assets_.push_back(file);
            }
        }
    }
}

void EventProcessor::process_deleted_events(const std::vector<FileEvent>& events) {
    std::vector<std::string> paths_to_delete;
    paths_to_delete.reserve(events.size());

    // Collect all paths to delete and increment progress per file
    for (const auto& event : events) {
        std::string path_utf8 = event.path.u8string();
        paths_to_delete.push_back(path_utf8);
        total_events_processed_++;  // Increment per file processed

        // Queue texture invalidation for deleted assets
        texture_manager_.queue_texture_invalidation(event.path);
    }

    // Batch delete from database
    if (!paths_to_delete.empty()) {
        database_.delete_assets_batch(paths_to_delete);

        // Batch remove from assets vector using partition + erase idiom
        std::lock_guard<std::mutex> lock(assets_mutex_);

        // Create a set for O(1) lookup
        std::unordered_set<std::string> paths_set(paths_to_delete.begin(), paths_to_delete.end());

        // Partition: move elements to delete to the end
        auto new_end = std::partition(assets_.begin(), assets_.end(),
            [&paths_set](const Asset& asset) {
                return paths_set.find(asset.full_path.u8string()) == paths_set.end();
            });

        // Erase elements from new_end to end
        assets_.erase(new_end, assets_.end());
    }
}

void EventProcessor::process_renamed_events(const std::vector<FileEvent>& events) {
    // For renames, we still need to handle them individually since they involve
    // both delete and create operations with different paths
    std::vector<std::string> old_paths;
    std::vector<Asset> new_files;

    old_paths.reserve(events.size());
    new_files.reserve(events.size());

    for (const auto& event : events) {
        try {
            old_paths.push_back(event.old_path.u8string());
            Asset file_info = process_file(event.path, event.timestamp);
            new_files.push_back(file_info);
            total_events_processed_++;  // Increment per file processed

            // Queue texture invalidation for both old and new paths
            texture_manager_.queue_texture_invalidation(event.old_path);
            // Only invalidate new path if it's a texture asset
            if (file_info.type == AssetType::Texture) {
                texture_manager_.queue_texture_invalidation(event.path);
            }
        }
        catch (const std::exception& e) {
            LOG_ERROR("Error processing renamed event for {} -> {}: {}", event.old_path.u8string(), event.path.u8string(), e.what());
            total_events_processed_++;  // Count failed attempts too
        }
    }

    // Batch delete old paths
    if (!old_paths.empty()) {
        database_.delete_assets_batch(old_paths);
    }

    // Batch insert new paths
    if (!new_files.empty()) {
        database_.insert_assets_batch(new_files);

        // Update assets vector
        std::lock_guard<std::mutex> lock(assets_mutex_);

        // Remove old paths
        std::unordered_set<std::string> old_paths_set(old_paths.begin(), old_paths.end());
        auto new_end = std::partition(assets_.begin(), assets_.end(),
            [&old_paths_set](const Asset& asset) {
                return old_paths_set.find(asset.full_path.u8string()) == old_paths_set.end();
            });
        assets_.erase(new_end, assets_.end());

        // Add new files
        assets_.reserve(assets_.size() + new_files.size());
        for (const auto& file : new_files) {
            assets_.push_back(file);
        }
    }
}

// Individual asset manipulation methods (still used by batch processing)

void EventProcessor::add_asset(const Asset& asset) {
    std::lock_guard<std::mutex> lock(assets_mutex_);

    // Check if asset already exists (avoid duplicates)
    int existing_index = find_asset_index(asset.full_path.u8string());
    if (existing_index >= 0) {
        assets_[existing_index] = asset; // Update existing
    }
    else {
        assets_.push_back(asset); // Add new
    }
}

void EventProcessor::update_asset(const Asset& asset) {
    std::lock_guard<std::mutex> lock(assets_mutex_);

    int index = find_asset_index(asset.full_path.u8string());
    if (index >= 0) {
        assets_[index] = asset;
    }
    else {
        // Asset not found, add it
        assets_.push_back(asset);
        LOG_WARN("Updated asset not found in memory, adding: {}", asset.full_path.u8string());
    }
}

void EventProcessor::remove_asset(const std::string& path) {
    std::lock_guard<std::mutex> lock(assets_mutex_);

    int index = find_asset_index(path);
    if (index >= 0) {
        assets_.erase(assets_.begin() + index);
    }
    else {
        LOG_WARN("Deleted asset not found in memory: {}", path);
    }
}

void EventProcessor::rename_asset(const std::string& old_path, const std::string& new_path) {
    std::lock_guard<std::mutex> lock(assets_mutex_);

    int index = find_asset_index(old_path);
    if (index >= 0) {
        assets_[index].full_path = fs::u8path(new_path);
        assets_[index].name = fs::path(new_path).filename().u8string();
    }
}

int EventProcessor::find_asset_index(const std::string& path) {
    // Linear search for now - can optimize with hash map later if needed
    for (size_t i = 0; i < assets_.size(); ++i) {
        if (assets_[i].full_path.u8string() == path) {
            return static_cast<int>(i);
        }
    }
    return -1;
}


Asset EventProcessor::process_file(const std::filesystem::path& full_path, const std::chrono::system_clock::time_point& timestamp) {
    Asset file_info;

    try {
        fs::path root(root_path_);

        // Basic file information (keep as filesystem::path for proper Unicode handling)
        file_info.full_path = full_path;
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
            // Directory-specific information
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
        file_info.full_path = full_path;
        file_info.name = full_path.filename().u8string();
        file_info.last_modified = timestamp;
    }

    return file_info;
}
