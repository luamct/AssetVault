#pragma once

#include <atomic>
#include <condition_variable>
#include <filesystem>
#include <map>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>
#include <unordered_map>

#include "database.h"
#include "file_watcher.h"
#include "asset.h"

// Forward declarations
struct GLFWwindow;
class TextureManager;
class SearchIndex;

// Unified event processor for both initial scan and runtime file events
class EventProcessor {
public:
    EventProcessor(AssetDatabase& database, std::map<std::string, Asset>& assets,
        std::mutex& assets_mutex, std::atomic<bool>& search_update_needed, 
        TextureManager& texture_manager, SearchIndex& search_index, GLFWwindow* thumbnail_context, size_t batch_size = 100);
    ~EventProcessor();

    // Start/stop the background processing thread
    bool start();
    void stop();

    // Add events to the processing queue
    void queue_event(const FileEvent& event);
    void queue_events(const std::vector<FileEvent>& events);

    // Check if processor is currently busy
    bool is_processing() const { return processing_; }

    // Get processing statistics
    size_t get_queue_size() const;
    size_t get_processed_count() const { return processed_count_; }

    // Progress tracking methods
    size_t get_total_queued() const { return total_events_queued_.load(); }
    size_t get_total_processed() const { return total_events_processed_.load(); }
    float get_progress() const {
        size_t queued = total_events_queued_.load();
        size_t processed = total_events_processed_.load();
        return queued > 0 ? (float) processed / queued : 1.0f;
    }
    bool has_pending_work() const { return total_events_queued_ > total_events_processed_; }
    void reset_progress_counters() {
        if (total_events_queued_ == total_events_processed_) {
            total_events_queued_.store(0);
            total_events_processed_.store(0);
        }
    }

    
    // Check if asset exists at path (thread-safe)
    bool has_asset_at_path(const std::string& path);

private:
    // Background thread function
    void process_events();

    // Process a batch of events
    void process_event_batch(const std::vector<FileEvent>& batch);

    // Event processing methods
    void process_created_events(const std::vector<FileEvent>& events);
    void process_modified_events(const std::vector<FileEvent>& events);
    void process_deleted_events(const std::vector<FileEvent>& events);

    // Asset manipulation methods (thread-safe)
    void add_asset(const Asset& asset);
    void update_asset(const Asset& asset);
    void remove_asset(const std::string& path);
    
    // OpenGL context setup for thumbnail generation
    bool setup_thumbnail_opengl_context();

    // References to global state
    AssetDatabase& database_;
    std::map<std::string, Asset>& assets_;
    std::mutex& assets_mutex_;
    std::atomic<bool>& search_update_needed_;
    TextureManager& texture_manager_;
    SearchIndex& search_index_;

    // Processing thread and synchronization
    std::thread processing_thread_;
    mutable std::mutex queue_mutex_;
    std::condition_variable queue_condition_;
    std::queue<FileEvent> event_queue_;

    // Configuration and state
    size_t batch_size_;
    std::atomic<bool> running_;
    std::atomic<bool> processing_;
    std::atomic<size_t> processed_count_;

    // Progress tracking (per-file granularity)
    std::atomic<size_t> total_events_queued_;
    std::atomic<size_t> total_events_processed_;

    // Root path for asset scanning
    std::string root_path_;
    
    // OpenGL context for thumbnail generation
    GLFWwindow* thumbnail_context_;

    // Process individual file/directory into Asset
    Asset process_file(const std::string& full_path, const std::chrono::system_clock::time_point& timestamp);
};
