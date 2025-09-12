#include "event_processor.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <unordered_set>

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include "config.h"
#include "logger.h"
#include "search.h"
#include "texture_manager.h"
#include "utils.h"

namespace fs = std::filesystem;

EventProcessor::EventProcessor(AssetDatabase& database, std::map<std::string, Asset>& assets,
    std::mutex& assets_mutex, std::atomic<bool>& search_update_needed,
    TextureManager& texture_manager, SearchIndex& search_index, GLFWwindow* thumbnail_context)
    : database_(database), assets_(assets), assets_mutex_(assets_mutex), search_update_needed_(search_update_needed),
    texture_manager_(texture_manager), search_index_(search_index), batch_size_(Config::EVENT_PROCESSOR_BATCH_SIZE), running_(false), processing_(false), processed_count_(0),
    total_events_queued_(0), total_events_processed_(0),
    root_path_(Config::ASSET_ROOT_DIRECTORY), thumbnail_context_(thumbnail_context) {
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

    LOG_INFO("EventProcessor started with batch size: {}", Config::EVENT_PROCESSOR_BATCH_SIZE);
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
    // Set up OpenGL context once for this background thread
    if (setup_thumbnail_opengl_context()) {
        LOG_DEBUG("OpenGL thumbnail context initialized for EventProcessor thread");
    }
    else {
        LOG_ERROR("Failed to initialize OpenGL thumbnail context for EventProcessor thread");
    }

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
    std::vector<FileEvent> deleted_events;

    for (const auto& event : batch) {
        switch (event.type) {
        case FileEventType::Created:
            created_events.push_back(event);
            break;
        case FileEventType::Deleted:
            deleted_events.push_back(event);
            break;
        }
    }

    // Process each type of event in batch for better performance
    // Process deletes first, then creates (important for file modifications sent as Delete+Create)
    if (!deleted_events.empty()) {
        process_deleted_events(deleted_events);
    }
    if (!created_events.empty()) {
        process_created_events(created_events);
    }

    // Signal that search needs to be updated
    LOG_DEBUG("[BATCH] Signalling for a search update");
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

    // Single pass: process files and generate thumbnails for 3D assets
    for (const auto& event : events) {
        try {
            Asset file_info = process_file(event.path, event.timestamp);

            // Generate thumbnail for 3D assets immediately after processing
            if (file_info.type == AssetType::_3D) {
                fs::path thumbnail_path = file_info.get_thumbnail_path();
                texture_manager_.generate_3d_model_thumbnail(file_info.path, thumbnail_path);
            }

            files_to_insert.push_back(file_info);
            total_events_processed_++;
        }
        catch (const std::exception& e) {
            LOG_ERROR("Error processing created event for {}: {}", event.path, e.what());
            total_events_processed_++;
        }
    }

    // Batch operations: database insert and assets map update
    if (!files_to_insert.empty()) {
        database_.insert_assets_batch(files_to_insert);

        // Single pass: update assets map and search index
        std::lock_guard<std::mutex> lock(assets_mutex_);
        for (const auto& file : files_to_insert) {
            assets_[file.path] = file;
            search_index_.add_asset(file.id, file);
        }
    }
}

void EventProcessor::process_deleted_events(const std::vector<FileEvent>& events) {
    if (events.empty()) return;

    std::vector<std::string> paths_to_delete;
    std::vector<uint32_t> deleted_asset_ids;
    paths_to_delete.reserve(events.size());
    deleted_asset_ids.reserve(events.size());

    // Total events processing counter (will be updated in the main loop)
    // Texture cleanup will be queued with asset type information below

    // Single pass: collect paths, asset IDs, and handle thumbnail cleanup
    {
        std::lock_guard<std::mutex> lock(assets_mutex_);

        for (const auto& event : events) {
            std::string path = event.path;
            paths_to_delete.push_back(path);

            auto asset_it = assets_.find(path);
            if (asset_it != assets_.end()) {
                const Asset& asset = asset_it->second;

                // Collect asset ID for search index cleanup
                if (asset.id > 0) {
                    deleted_asset_ids.push_back(asset.id);
                }

                // Queue texture cleanup with asset type (includes thumbnail deletion for 3D assets)
                texture_manager_.queue_texture_cleanup(event.path, asset.type);

                // Remove from assets map immediately
                assets_.erase(asset_it);
            } else {
                // Asset not found in memory, queue cleanup with Unknown type (no thumbnail deletion)
                texture_manager_.queue_texture_cleanup(event.path, AssetType::Unknown);
            }

            total_events_processed_++;
        }

        // Remove from search index in the same critical section
        for (uint32_t asset_id : deleted_asset_ids) {
            search_index_.remove_asset(asset_id);
        }
    }

    // Batch delete from database (outside mutex)
    database_.delete_assets_batch(paths_to_delete);
}

// Individual asset manipulation methods (still used by batch processing)

void EventProcessor::add_asset(const Asset& asset) {
    std::lock_guard<std::mutex> lock(assets_mutex_);
    assets_[asset.path] = asset;
}

void EventProcessor::update_asset(const Asset& asset) {
    std::lock_guard<std::mutex> lock(assets_mutex_);
    assets_[asset.path] = asset;
}

void EventProcessor::remove_asset(const std::string& path) {
    std::lock_guard<std::mutex> lock(assets_mutex_);
    assets_.erase(path);
}

Asset EventProcessor::process_file(const std::string& full_path, const std::chrono::system_clock::time_point& timestamp) {
    Asset asset;

    try {
        fs::path root(root_path_);

        // Basic file information (path is already normalized)
        asset.path = full_path;
        fs::path path_obj = fs::u8path(full_path);
        asset.name = path_obj.filename().u8string();
        // File-specific information
        asset.extension = path_obj.extension().string();
        asset.type = get_asset_type(asset.extension);

        try {
            asset.size = fs::file_size(path_obj);
            // Store display time as modification time (for user-facing display)
            try {
                auto ftime = fs::last_write_time(path_obj);
                auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                    ftime - fs::file_time_type::clock::now() + std::chrono::system_clock::now()
                );
                asset.last_modified = sctp;
            }
            catch (const fs::filesystem_error& e) {
                // Fallback to provided timestamp for display
                asset.last_modified = timestamp;
                LOG_WARN("Using provided timestamp for display for {}: {}", full_path, e.what());
            }
        }
        catch (const fs::filesystem_error& e) {
            LOG_WARN("Could not get file info for {}: {}", asset.path, e.what());
            asset.size = 0;
            asset.last_modified = timestamp;
        }
    }
    catch (const fs::filesystem_error& e) {
        LOG_ERROR("Error creating file info for {}: {}", full_path, e.what());
        // Return minimal file info on error
        asset.path = full_path;
        fs::path path_obj = fs::u8path(full_path);
        asset.name = path_obj.filename().u8string();
        asset.last_modified = timestamp;
    }

    return asset;
}

bool EventProcessor::setup_thumbnail_opengl_context() {
    if (!thumbnail_context_) {
        LOG_ERROR("No thumbnail context available for OpenGL setup");
        return false;
    }

    // Make the thumbnail context current for this thread
    glfwMakeContextCurrent(thumbnail_context_);

    // Set up OpenGL state for proper 3D rendering (match main context configuration)
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);

    // Enable blending for transparency
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    LOG_DEBUG("OpenGL context set up for thumbnail generation thread");
    return true;
}
