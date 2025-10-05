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
#include "services.h"

namespace fs = std::filesystem;

EventProcessor::EventProcessor(SafeAssets& safe_assets,
    std::atomic<bool>& search_update_needed,
    const std::string& assets_directory, GLFWwindow* thumbnail_context)
    : safe_assets_(safe_assets), search_update_needed_(search_update_needed),
    batch_size_(Config::EVENT_PROCESSOR_BATCH_SIZE), running_(false), processing_(false), processed_count_(0),
    total_events_queued_(0), total_events_processed_(0),
    thumbnail_context_(thumbnail_context), assets_directory_(assets_directory) {
}

EventProcessor::~EventProcessor() {
    stop();
}

bool EventProcessor::start(const std::string& assets_directory) {
    if (running_) {
        return true; // Already running
    }

    assets_directory_ = assets_directory;
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
    auto [lock, assets] = safe_assets_.read();
    return assets.find(path) != assets.end();
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

void EventProcessor::clear_queue() {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    // Clear the queue by creating a new empty queue
    std::queue<FileEvent> empty_queue;
    event_queue_.swap(empty_queue);

    // Reset progress counters to avoid stale progress display
    total_events_queued_.store(0);
    total_events_processed_.store(0);
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
    LOG_INFO("Batch of {} assets completed. Running average of {:.2f}ms per asset ({} total assets processed)", 
        batch.size(), global_avg, total_assets_processed);
}

// Batch processing creation events
void EventProcessor::process_created_events(const std::vector<FileEvent>& events) {
    std::vector<Asset> files_to_insert;
    files_to_insert.reserve(events.size());

    // Single pass: process files and generate thumbnails for 3D assets
    for (const auto& event : events) {
        try {
            Asset file_info = process_file(event.path);

            // Generate thumbnails immediately after processing
            if (file_info.type == AssetType::_3D) {
                fs::path thumbnail_path = get_thumbnail_path(file_info.relative_path);
                Services::texture_manager().generate_3d_model_thumbnail(file_info.path, thumbnail_path);
            }
            else if (file_info.type == AssetType::_2D && file_info.extension == ".svg") {
                fs::path thumbnail_path = get_thumbnail_path(file_info.relative_path);
                Services::texture_manager().generate_svg_thumbnail(file_info.path, thumbnail_path);
            }

            // Add asset to database (exceptions prevent reaching this point on failure)
            files_to_insert.push_back(file_info);
        }
        catch (const std::exception& e) {
            // Unified retry logic for all exceptions during asset processing
            if (event.retry_count < Config::MAX_ASSET_CREATION_RETRIES) {
                FileEvent retry_event = event;
                retry_event.retry_count++;
                queue_event(retry_event);
                LOG_WARN("Re-queuing asset for retry (attempt {}/{}): {} - {}",
                         retry_event.retry_count,
                         Config::MAX_ASSET_CREATION_RETRIES,
                         event.path, e.what());
            } else {
                LOG_ERROR("Failed to process asset after {} retries: {} - {}",
                          Config::MAX_ASSET_CREATION_RETRIES,
                          event.path, e.what());
            }
        }

        total_events_processed_++;
    }

    // Batch operations: database insert and assets map update
    if (!files_to_insert.empty()) {
        Services::database().insert_assets_batch(files_to_insert);

        // Single pass: update assets map and search index
        auto [lock, assets] = safe_assets_.write();
        for (const auto& file : files_to_insert) {
            assets[file.path] = file;
            Services::search_index().add_asset(file.id, file);
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
        auto [lock, assets] = safe_assets_.write();

        for (const auto& event : events) {
            std::string path = event.path;
            paths_to_delete.push_back(path);

            // Always queue texture/thumbnail cleanup for this path
            Services::texture_manager().queue_texture_cleanup(path);

            auto asset_it = assets.find(path);
            if (asset_it != assets.end()) {
                const Asset& asset = asset_it->second;

                // Collect asset ID for search index cleanup
                if (asset.id > 0) {
                    deleted_asset_ids.push_back(asset.id);
                }

                // Remove from assets map immediately
                assets.erase(asset_it);
            }

            total_events_processed_++;
        }

        // Remove from search index in the same critical section
        for (uint32_t asset_id : deleted_asset_ids) {
            Services::search_index().remove_asset(asset_id);
        }
    }

    // Batch delete from database (outside mutex)
    Services::database().delete_assets_batch(paths_to_delete);
}

Asset EventProcessor::process_file(const std::string& full_path) {
    fs::path path_obj = fs::u8path(full_path);

    // Check if file exists first - throw exception for non-existent files
    if (!fs::exists(path_obj)) {
        throw std::runtime_error("File does not exist: " + full_path);
    }

    // Basic file information (path is already normalized)
    Asset asset;
    asset.path = full_path;
    asset.relative_path = get_relative_path(asset.path, assets_directory_);
    asset.name = path_obj.filename().u8string();
    // File-specific information
    asset.extension = path_obj.extension().string();
    asset.type = get_asset_type(asset.extension);

    // File size and modification time (let exceptions bubble up)
    asset.size = fs::file_size(path_obj);
    auto ftime = fs::last_write_time(path_obj);
    auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
        ftime - fs::file_time_type::clock::now() + std::chrono::system_clock::now()
    );
    asset.last_modified = sctp;

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
