#pragma once

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

#include "database.h"
#include "file_watcher.h"
#include "index.h"

// Forward declarations
class TextureManager;

// Unified event processor for both initial scan and runtime file events
class EventProcessor {
public:
    EventProcessor(AssetDatabase& database, std::vector<FileInfo>& assets, 
                  std::atomic<bool>& search_update_needed, size_t batch_size = 100);
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
        return queued > 0 ? (float)processed / queued : 1.0f;
    }
    bool has_pending_work() const { return total_events_queued_ > total_events_processed_; }
    void reset_progress_counters() {
        if (total_events_queued_ == total_events_processed_) {
            total_events_queued_.store(0);
            total_events_processed_.store(0);
        }
    }

    // Access to assets mutex for thread-safe filtering
    std::mutex& get_assets_mutex() { return assets_mutex_; }

private:
    // Background thread function
    void process_events();

    // Process a batch of events
    void process_event_batch(const std::vector<FileEvent>& batch);

    // Event processing methods
    void process_created_events(const std::vector<FileEvent>& events);
    void process_modified_events(const std::vector<FileEvent>& events);
    void process_deleted_events(const std::vector<FileEvent>& events);
    void process_renamed_events(const std::vector<FileEvent>& events);

    // Asset manipulation methods (thread-safe)
    void add_asset(const FileInfo& asset);
    void update_asset(const FileInfo& asset);
    void remove_asset(const std::string& path);
    void rename_asset(const std::string& old_path, const std::string& new_path);

    // Find asset index by path (assumes assets mutex is locked)
    int find_asset_index(const std::string& path);

    // References to global state
    AssetDatabase& database_;
    std::vector<FileInfo>& assets_;
    std::atomic<bool>& search_update_needed_;

    // Processing thread and synchronization
    std::thread processing_thread_;
    mutable std::mutex queue_mutex_;
    mutable std::mutex assets_mutex_;
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

    // Asset indexer for file processing
    AssetIndexer indexer_;
};