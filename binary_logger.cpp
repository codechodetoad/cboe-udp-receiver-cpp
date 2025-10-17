#include "binary_logger.h"
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/async.h>
#include <chrono>
#include <stdexcept>
#include <algorithm>

BinaryLogger::BinaryLogger() {
    init_logging();
}

BinaryLogger::~BinaryLogger() {
    if (binary_logger_) {
        binary_logger_->flush();
    }
    spdlog::shutdown();
}

void BinaryLogger::init_logging() {
    try {
        // Initialize async logging with MASSIVE queue for 14M packets
        spdlog::init_thread_pool(Config::ASYNC_QUEUE_SIZE, Config::ASYNC_THREADS);
        
        // Create rotating file sink optimized for binary data
        auto rotating_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
            "packets_binary.log", Config::LOG_FILE_SIZE, Config::LOG_FILE_COUNT);
        
        // Disable automatic flushing for maximum performance
        rotating_sink->set_level(spdlog::level::info);
        
        // Create console sink for status messages only
        auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        
        // Create async binary logger with maximum performance settings
        binary_logger_ = std::make_shared<spdlog::async_logger>(
            "binary_logger", 
            rotating_sink, 
            spdlog::thread_pool(),
            spdlog::async_overflow_policy::block);  // Block instead of dropping packets
        
        // Create console logger for status messages
        console_logger_ = std::make_shared<spdlog::logger>("console", console_sink);
        
        // Set MINIMAL pattern for binary logger (no timestamp, just raw data)
        binary_logger_->set_pattern("%v");  // Only the message content
        console_logger_->set_pattern("[%Y-%m-%d %H:%M:%S.%f] [%l] %v");
        
        // Set levels
        binary_logger_->set_level(spdlog::level::info);
        console_logger_->set_level(spdlog::level::info);
        
        // CRITICAL: Disable automatic flushing for 14M packets performance
        // We'll manually flush periodically
        binary_logger_->flush_on(spdlog::level::off);  
        
        // Register loggers
        spdlog::register_logger(binary_logger_);
        spdlog::register_logger(console_logger_);
        
        console_logger_->info("HIGH-VOLUME binary logging initialized: {}MB files, {} threads, {}K queue", 
                               Config::LOG_FILE_SIZE/(1024*1024), Config::ASYNC_THREADS, Config::ASYNC_QUEUE_SIZE/1024);
        
    } catch (const spdlog::spdlog_ex& ex) {
        throw std::runtime_error("spdlog initialization failed: " + std::string(ex.what()));
    }
}

void BinaryLogger::log_packet(uint32_t packet_id, uint16_t port, const char* buffer, uint16_t len,
                              uint32_t sequence, uint8_t count, uint8_t unit, 
                              PacketType packet_type, OrderStatus order_status,
                              uint32_t src_ip) {
    
    // Get high-precision timestamp
    auto now = std::chrono::high_resolution_clock::now();
    uint64_t timestamp_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        now.time_since_epoch()).count();
    
    // Limit payload size for binary logging (store first 256 bytes for analysis)
    uint16_t payload_length = std::min(static_cast<uint16_t>(len), static_cast<uint16_t>(256));
    
    // Create compact binary record
    BinaryLogRecord record = {
        .timestamp_ns = timestamp_ns,
        .packet_id = packet_id,
        .sequence = sequence,
        .src_ip = src_ip,
        .port = port,
        .length = len,
        .count = count,
        .unit = unit,
        .packet_type = static_cast<uint8_t>(packet_type),
        .order_status = static_cast<uint8_t>(order_status),
        .payload_length = payload_length
    };
    
    // CRITICAL: Log binary data using spdlog's string for zero-copy performance
    // This creates a single log entry with: [BinaryRecord][Limited Payload]
    std::string log_entry;
    log_entry.reserve(sizeof(BinaryLogRecord) + record.payload_length);
    
    // Append binary record
    log_entry.append(reinterpret_cast<const char*>(&record), sizeof(BinaryLogRecord));
    
    // Append limited payload (first 256 bytes for analysis)
    log_entry.append(buffer, record.payload_length);
    
    // Log to spdlog as raw binary data - EXTREMELY fast
    binary_logger_->info(log_entry);
}

void BinaryLogger::flush() {
    if (binary_logger_) {
        binary_logger_->flush();
    }
}

void BinaryLogger::log_info(const std::string& message) {
    if (console_logger_) {
        console_logger_->info(message);
    }
}

void BinaryLogger::log_warning(const std::string& message) {
    if (console_logger_) {
        console_logger_->warn(message);
    }
}

void BinaryLogger::log_error(const std::string& message) {
    if (console_logger_) {
        console_logger_->error(message);
    }
}