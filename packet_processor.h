#pragma once

#include "packet_types.h"
#include "sequence_tracker.h"
#include "binary_logger.h"
#include <memory>
#include <string>
#include <chrono>

/**
 * Main packet processing engine
 * Handles packet classification, sequence tracking, and logging
 */
class PacketProcessor {
public:
    /**
     * Constructor
     */
    PacketProcessor();
    
    /**
     * Destructor
     */
    ~PacketProcessor() = default;
    
    /**
     * Process a received packet
     * @param packet_id Unique packet identifier
     * @param port Source port
     * @param buffer Raw packet data
     * @param len Packet length
     * @param src_ip Source IP address string
     */
    void process_packet(int packet_id, int port, const char* buffer, int len, const std::string& src_ip);
    
    /**
     * Get performance statistics
     */
    struct Statistics {
        uint64_t total_packets = 0;
        uint64_t heartbeats_skipped = 0;
        uint64_t data_packets = 0;
        uint64_t admin_packets = 0;
        uint64_t unsequenced_packets = 0;
        uint64_t out_of_order_packets = 0;
        uint64_t duplicate_packets = 0;
        std::chrono::high_resolution_clock::time_point start_time;
        
        double get_packets_per_second() const;
        double get_elapsed_seconds() const;
    };
    
    const Statistics& get_statistics() const { return stats_; }
    
    /**
     * Print performance report
     */
    void print_performance_report() const;
    
    /**
     * Force flush of logger
     */
    void flush_logs();

private:
    std::unique_ptr<BinaryLogger> logger_;
    std::unique_ptr<SequenceManager> sequence_manager_;
    Statistics stats_;
    
    /**
     * Validate packet structure
     */
    bool validate_packet(const char* buffer, int len) const;
    
    /**
     * Check if we should report statistics
     */
    bool should_report_statistics() const;
};