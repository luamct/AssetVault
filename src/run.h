#pragma once

#include <atomic>

// Main application entry point
// Returns 0 on success, non-zero on error
int run();

// Global flag for requesting graceful shutdown (used by tests)
extern std::atomic<bool> g_shutdown_requested;