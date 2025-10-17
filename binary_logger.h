#pragma once

#include "packet_types.h"
#include <spdlog/spdlog.h>
#include <memory>
#include <string>

/**
 * High-performance binary logger using spdlog async infrastructure
 * Optimized for logging millions of packets with minimal latency
 */
class BinaryLogger {
public:
    /**
     * Constructor - initializes spdlog with high-performance settings
     */
    BinaryLogger();
    
    /**
     * Destructor - ensures proper cleanup and final flush
     */
    ~BinaryLogger();
    
    /**
     * Log a packet in binary format
     * @param packet_id Unique packet identifier
     * @param port Source port
     * @param buffer Raw packet data
     * @param len Packet length
     * @param sequence CBOE sequence number
     * @param count Message count
     * @param unit Unit identifier
     * @param packet_type Type of packet
     * @param order_status Sequence order status
     * @param src_ip Source IP address (binary format)
     */
    void log_packet(uint32_t packet_id, uint16_t port, const char* buffer, uint16_t len,
                   uint32_t sequence, uint8_t count, uint8_t unit, 
                   PacketType packet_type, OrderStatus order_status,
                   uint32_t src_ip);
    
    /**
     * Force flush of pending log data
     */
    void flush();
    
    /**
     * Log informational message to console
     */
    void log_info(const std::string& message);
    
    /**
     * Log warning message to console
     */
    void log_warning(const std::string& message);
    
    /**
     * Log error message to console
     */
    void log_error(const std::string& message);

private:
    std::shared_ptr<spdlog::logger> binary_logger_;
    std::shared_ptr<spdlog::logger> console_logger_;
    
    /**
     * Initialize the logging system with optimized settings
     */
    void init_logging();
};