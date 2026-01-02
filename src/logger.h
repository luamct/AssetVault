#pragma once

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <mutex>
#include <system_error>
#include <vector>

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/cfg/env.h>

// Log level enum for easy configuration
enum class LogLevel {
    Trace = 0,
    Debug = 1,
    Info = 2,
    Warning = 3,
    Error = 4,
    Critical = 5,
    Off = 6
};

// Logger utility class
class Logger {
public:
    static std::shared_ptr<spdlog::logger>& instance() {
        static std::shared_ptr<spdlog::logger> logger_instance;
        return logger_instance;
    }

    static std::filesystem::path get_log_file_path() {
#ifdef __APPLE__
        if (const char* home = std::getenv("HOME")) {
            if (home[0] != '\0') {
                return std::filesystem::path(home) / "Library" / "Logs" / "AssetVault" / "asset_inventory.log";
            }
        }
#elif _WIN32
        if (const char* localappdata = std::getenv("LOCALAPPDATA")) {
            if (localappdata[0] != '\0') {
                return std::filesystem::path(localappdata) / "AssetVault" / "logs" / "asset_inventory.log";
            }
        }
#else
        if (const char* home = std::getenv("HOME")) {
            if (home[0] != '\0') {
                return std::filesystem::path(home) / ".local" / "state" / "asset_vault" / "logs" / "asset_inventory.log";
            }
        }
#endif

        return std::filesystem::path("logs") / "asset_inventory.log";
    }

    static void initialize(LogLevel level = LogLevel::Info) {
        // Guard against initialize being called from test + app threads in integration tests.
        static std::mutex init_mutex;
        std::lock_guard<std::mutex> lock(init_mutex);

        auto& existing = instance();
        if (existing) {
            existing->set_level(static_cast<spdlog::level::level_enum>(level));
            spdlog::set_default_logger(existing);
            spdlog::flush_every(std::chrono::seconds(1));
            spdlog::cfg::load_env_levels();
            return;
        }

        // Load log level from SPDLOG_LEVEL environment variable if set
        spdlog::cfg::load_env_levels();
        
        // Create console sink with colors
        auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        console_sink->set_level(static_cast<spdlog::level::level_enum>(level));
        
        // Create file sink (avoid relative paths; Finder-launched apps typically have cwd=/)
        std::vector<spdlog::sink_ptr> sinks{console_sink};
        const std::filesystem::path log_file_path = get_log_file_path();
        std::error_code ec;
        std::filesystem::create_directories(log_file_path.parent_path(), ec);
        try {
            auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(log_file_path.string(), true);
            file_sink->set_level(spdlog::level::trace); // Log everything to file
            sinks.push_back(file_sink);
        } catch (const spdlog::spdlog_ex& ex) {
            // Keep the app running even if file logging can't be enabled.
            // (stdout/stderr may be absent when launched from Finder.)
            fprintf(stderr, "Failed to open log file '%s': %s\n",
                log_file_path.string().c_str(), ex.what());
        }

        // Create logger with sinks
        auto logger = std::make_shared<spdlog::logger>("asset_inventory", sinks.begin(), sinks.end());
        
        // Set pattern and level
        logger->set_pattern("[%H:%M:%S] [%^%l%$] %v");
        logger->set_level(static_cast<spdlog::level::level_enum>(level));
        
        // Register as default logger and keep it alive for the process lifetime.
        existing = logger;
        spdlog::set_default_logger(existing);
        spdlog::flush_every(std::chrono::seconds(1));
        
        // Load environment levels again to override the programmatic level if SPDLOG_LEVEL is set
        spdlog::cfg::load_env_levels();
    }
    
    static void set_level(LogLevel level) {
        spdlog::set_level(static_cast<spdlog::level::level_enum>(level));
    }
};

// Static initializer to load environment variables on first use
struct SpdlogEnvLoader {
    SpdlogEnvLoader() {
        // This loads SPDLOG_LEVEL environment variable automatically
        // Format: SPDLOG_LEVEL=debug or SPDLOG_LEVEL=logger_name=debug,other_logger=info
        spdlog::cfg::load_env_levels();
    }
};

// This will run once when logger.h is first included anywhere
static SpdlogEnvLoader spdlog_env_loader;

// Convenient macros
#define LOG_TRACE(...)    spdlog::trace(__VA_ARGS__)
#define LOG_DEBUG(...)    spdlog::debug(__VA_ARGS__)
#define LOG_INFO(...)     spdlog::info(__VA_ARGS__)
#define LOG_WARN(...)     spdlog::warn(__VA_ARGS__)
#define LOG_ERROR(...)    spdlog::error(__VA_ARGS__)
#define LOG_CRITICAL(...) spdlog::critical(__VA_ARGS__)
