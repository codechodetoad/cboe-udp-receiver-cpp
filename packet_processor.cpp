#include "packet_processor.h"
#include <iomanip>
#include <sstream>

PacketProcessor::PacketProcessor() 
    : logger_(std::make_unique<BinaryLogger>()),
      sequence_manager_(std::make_unique<SequenceManager>()) {
    
    stats_.start_time = std::chrono::high_resolution_clock::now();
    
    logger_->log_info("PacketProcessor initialized and ready for high-volume processing");
}

void PacketProcessor::process_packet(int packet_id, int port, const char* buffer, int len, const std::string& src_ip) {
    stats_.total_packets++;
    
    // Validate packet structure
    if (!validate_packet(buffer, len)) {
        logger_->log_warning("Invalid packet structure, packet_id: " + std::to_string(packet_id));
        return;
    }
    
    // Parse CBOE header
    const CboeSequencedUnitHeader* header = reinterpret_cast<const CboeSequencedUnitHeader*>(buffer);
    
    uint32_t sequence = le32toh_safe(header->hdr_sequence);
    uint16_t length = le16toh_safe(header->hdr_length);
    uint8_t count = header->hdr_count;
    uint8_t unit = header->hdr_unit;
    
    // Classify packet type
    PacketType packet_type = classify_packet_type(sequence, count, len);
    
    // Update statistics
    switch (packet_type) {
        case PacketType::HEARTBEAT:
            if (Config::SKIP_HEARTBEATS) {
                stats_.heartbeats_skipped++;
                return; // Skip logging heartbeats
            }
            break;
        case PacketType::DATA:
            stats_.data_packets++;
            break;
        case PacketType::ADMIN:
            stats_.admin_packets++;
            break;
        case PacketType::UNSEQUENCED:
            stats_.unsequenced_packets++;
            break;
    }
    
    // Determine sequence order status
    OrderStatus order_status = sequence_manager_->determine_order_status(sequence, count, port, unit);
    
    // Update order statistics
    switch (order_status) {
        case OrderStatus::SEQUENCED_OUT_OF_ORDER_EARLY:
        case OrderStatus::SEQUENCED_OUT_OF_ORDER_LATE:
            stats_.out_of_order_packets++;
            break;
        case OrderStatus::SEQUENCED_DUPLICATE:
            stats_.duplicate_packets++;
            break;
        default:
            break;
    }
    
    // Convert IP to binary format
    uint32_t src_ip_binary = ip_to_binary(src_ip);
    
    // Log the packet
    logger_->log_packet(
        static_cast<uint32_t>(packet_id),
        static_cast<uint16_t>(port),
        buffer,
        static_cast<uint16_t>(len),
        sequence,
        count,
        unit,
        packet_type,
        order_status,
        src_ip_binary
    );
    
    // Periodic performance reporting
    if (should_report_statistics()) {
        print_performance_report();
    }

    // Periodic flushing for data safety (silent flush for performance)
    if (stats_.total_packets % Config::FLUSH_INTERVAL == 0) {
        flush_logs();
        // Note: Flush info is included in performance report to avoid extra I/O
    }
}

bool PacketProcessor::validate_packet(const char* buffer, int len) const {
    if (len < static_cast<int>(sizeof(CboeSequencedUnitHeader))) {
        return false;
    }
    
    const CboeSequencedUnitHeader* header = reinterpret_cast<const CboeSequencedUnitHeader*>(buffer);
    uint16_t declared_length = le16toh_safe(header->hdr_length);
    
    // Basic sanity checks
    if (declared_length == 0 || declared_length > Config::MAX_BUF) {
        return false;
    }
    
    // Length should be reasonable compared to actual received length
    if (static_cast<int>(declared_length) > len + 100) { // Allow some tolerance
        return false;
    }
    
    return true;
}

bool PacketProcessor::should_report_statistics() const {
    return (stats_.total_packets % Config::STATS_INTERVAL == 0) && (stats_.total_packets > 0);
}

double PacketProcessor::Statistics::get_packets_per_second() const {
    double elapsed = get_elapsed_seconds();
    return (elapsed > 0) ? static_cast<double>(total_packets) / elapsed : 0.0;
}

double PacketProcessor::Statistics::get_elapsed_seconds() const {
    auto now = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time);
    return duration.count() / 1000.0;
}

void PacketProcessor::print_performance_report() const {
    double pps = stats_.get_packets_per_second();
    double elapsed = stats_.get_elapsed_seconds();
    
    std::ostringstream oss;
    oss << "PERFORMANCE: " << stats_.total_packets << " packets, "
        << std::fixed << std::setprecision(0) << pps << " pps, "
        << std::fixed << std::setprecision(1) << elapsed << "s elapsed";
    
    if (stats_.heartbeats_skipped > 0) {
        oss << ", " << stats_.heartbeats_skipped << " heartbeats skipped";
    }
    
    if (stats_.out_of_order_packets > 0 || stats_.duplicate_packets > 0) {
        oss << ", " << stats_.out_of_order_packets << " OOO, " 
            << stats_.duplicate_packets << " dups";
    }
    
    // Performance warning for high-volume scenarios
    if (pps < 50000 && stats_.total_packets > 100000) {
        oss << " [WARNING: Below 50K pps target]";
    }
    
    logger_->log_info(oss.str());
}

void PacketProcessor::flush_logs() {
    logger_->flush();
}