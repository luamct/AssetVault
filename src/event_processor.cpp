#include "event_processor.h"

#include <algorithm>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <unordered_set>

#include "texture_manager.h"

namespace fs = std::filesystem;

EventProcessor::EventProcessor(AssetDatabase& database, std::vector<FileInfo>& assets,
    std::atomic<bool>& search_update_needed, size_t batch_size)
    : database_(database), assets_(assets), search_update_needed_(search_update_needed),
    batch_size_(batch_size), running_(false), processing_(false), processed_count_(0),
    total_events_queued_(0), total_events_processed_(0),
    indexer_("assets") {
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

    std::cout << "EventProcessor started with batch size: " << batch_size_ << std::endl;
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

    std::cout << "EventProcessor stopped. Total processed: " << processed_count_ << std::endl;
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

    // Note: We assume DB operations will succeed and increment before them for responsive progress

    // Calculate and log timing metrics
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    double total_ms = duration.count();
    double ms_per_asset = batch.size() > 0 ? total_ms / batch.size() : 0.0;

    std::cout << "Batch of " << batch.size() << " assets completed in " << total_ms << "ms ("
        << std::fixed << std::setprecision(2) << ms_per_asset << "ms per asset)" << std::endl;
}

// Batch processing methods for better performance

void EventProcessor::process_created_events(const std::vector<FileEvent>& events) {
    std::vector<FileInfo> files_to_insert;
    files_to_insert.reserve(events.size());

    // Process all files first and increment progress per file
    for (const auto& event : events) {
        try {
            FileInfo file_info = indexer_.process_file(event.path, event.timestamp);
            files_to_insert.push_back(file_info);
            total_events_processed_++;  // Increment per file processed
        }
        catch (const std::exception& e) {
            std::cerr << "Error processing created event for " << event.path << ": " << e.what() << std::endl;
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
    std::vector<FileInfo> files_to_update;
    files_to_update.reserve(events.size());

    // Process all files first and increment progress per file
    for (const auto& event : events) {
        try {
            // Skip directory modifications - content changes will be handled as individual file events
            if (!fs::exists(event.path) || fs::is_directory(event.path)) {
                total_events_processed_++;  // Count skipped directories
                continue;
            }

            FileInfo file_info = indexer_.process_file(event.path, event.timestamp);
            files_to_update.push_back(file_info);
            total_events_processed_++;  // Increment per file processed
        }
        catch (const std::exception& e) {
            std::cerr << "Error processing modified event for " << event.path << ": " << e.what() << std::endl;
            total_events_processed_++;  // Count failed attempts too
        }
    }

    // Batch update database
    if (!files_to_update.empty()) {
        database_.update_assets_batch(files_to_update);

        // Batch update assets vector
        std::lock_guard<std::mutex> lock(assets_mutex_);
        for (const auto& file : files_to_update) {
            int index = find_asset_index(file.full_path);
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
        paths_to_delete.push_back(event.path);
        total_events_processed_++;  // Increment per file processed
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
            [&paths_set](const FileInfo& asset) {
                return paths_set.find(asset.full_path) == paths_set.end();
            });

        // Erase elements from new_end to end
        assets_.erase(new_end, assets_.end());
    }
}

void EventProcessor::process_renamed_events(const std::vector<FileEvent>& events) {
    // For renames, we still need to handle them individually since they involve
    // both delete and create operations with different paths
    std::vector<std::string> old_paths;
    std::vector<FileInfo> new_files;

    old_paths.reserve(events.size());
    new_files.reserve(events.size());

    for (const auto& event : events) {
        try {
            old_paths.push_back(event.old_path);
            FileInfo file_info = indexer_.process_file(event.path, event.timestamp);
            new_files.push_back(file_info);
            total_events_processed_++;  // Increment per file processed
        }
        catch (const std::exception& e) {
            std::cerr << "Error processing renamed event for " << event.old_path << " -> " << event.path
                << ": " << e.what() << std::endl;
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
            [&old_paths_set](const FileInfo& asset) {
                return old_paths_set.find(asset.full_path) == old_paths_set.end();
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

void EventProcessor::add_asset(const FileInfo& asset) {
    std::lock_guard<std::mutex> lock(assets_mutex_);

    // Check if asset already exists (avoid duplicates)
    int existing_index = find_asset_index(asset.full_path);
    if (existing_index >= 0) {
        assets_[existing_index] = asset; // Update existing
    }
    else {
        assets_.push_back(asset); // Add new
    }
}

void EventProcessor::update_asset(const FileInfo& asset) {
    std::lock_guard<std::mutex> lock(assets_mutex_);

    int index = find_asset_index(asset.full_path);
    if (index >= 0) {
        assets_[index] = asset;
    }
    else {
        // Asset not found, add it
        assets_.push_back(asset);
        std::cerr << "Warning: Updated asset not found in memory, adding: " << asset.full_path << std::endl;
    }
}

void EventProcessor::remove_asset(const std::string& path) {
    std::lock_guard<std::mutex> lock(assets_mutex_);

    int index = find_asset_index(path);
    if (index >= 0) {
        assets_.erase(assets_.begin() + index);
    }
    else {
        std::cerr << "Warning: Deleted asset not found in memory: " << path << std::endl;
    }
}

void EventProcessor::rename_asset(const std::string& old_path, const std::string& new_path) {
    std::lock_guard<std::mutex> lock(assets_mutex_);

    int index = find_asset_index(old_path);
    if (index >= 0) {
        assets_[index].full_path = new_path;
        assets_[index].name = fs::path(new_path).filename().string();
        // Update relative path if needed
        fs::path root("assets");
        try {
            assets_[index].relative_path = fs::relative(new_path, root).string();
        }
        catch (const fs::filesystem_error&) {
            assets_[index].relative_path = new_path;
        }
    }
}

int EventProcessor::find_asset_index(const std::string& path) {
    // Linear search for now - can optimize with hash map later if needed
    for (size_t i = 0; i < assets_.size(); ++i) {
        if (assets_[i].full_path == path) {
            return static_cast<int>(i);
        }
    }
    return -1;
}
