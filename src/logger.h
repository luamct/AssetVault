#pragma once

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/basic_file_sink.h>

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
    static void initialize(LogLevel level = LogLevel::Info) {
        // Create console sink with colors
        auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        console_sink->set_level(static_cast<spdlog::level::level_enum>(level));
        
        // Create file sink
        auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>("logs/asset_inventory.log", true);
        file_sink->set_level(spdlog::level::trace); // Log everything to file
        
        // Create logger with both sinks
        std::vector<spdlog::sink_ptr> sinks{console_sink, file_sink};
        auto logger = std::make_shared<spdlog::logger>("asset_inventory", sinks.begin(), sinks.end());
        
        // Set pattern and level
        logger->set_pattern("[%H:%M:%S] [%^%l%$] %v");
        logger->set_level(static_cast<spdlog::level::level_enum>(level));
        
        // Register as default logger
        spdlog::set_default_logger(logger);
        spdlog::flush_every(std::chrono::seconds(1));
    }
    
    static void set_level(LogLevel level) {
        spdlog::set_level(static_cast<spdlog::level::level_enum>(level));
    }
};

// Convenient macros
#define LOG_TRACE(...)    spdlog::trace(__VA_ARGS__)
#define LOG_DEBUG(...)    spdlog::debug(__VA_ARGS__)
#define LOG_INFO(...)     spdlog::info(__VA_ARGS__)
#define LOG_WARN(...)     spdlog::warn(__VA_ARGS__)
#define LOG_ERROR(...)    spdlog::error(__VA_ARGS__)
#define LOG_CRITICAL(...) spdlog::critical(__VA_ARGS__)