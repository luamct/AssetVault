#pragma once

#include <atomic>

// Main application entry point
// Runs in headless mode if TESTING environment variable is set
// shutdown_requested: Optional atomic flag for graceful shutdown (used by tests)
// Returns 0 on success, non-zero on error
int run(std::atomic<bool>* shutdown_requested = nullptr);