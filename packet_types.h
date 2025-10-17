#pragma once

#include <cstdint>
#include <map>
#include <string>
// Configuration constants
namespace Config {
    constexpr const char* MULTICAST_IP = "233.218.133.80";
    constexpr uint16_t PORT1 = 30501;
    constexpr uint16_t PORT2 = 30502;
    constexpr int MAX_BUF = 2048;
    constexpr bool SKIP_HEARTBEATS = true;
    
    // Binary logging configuration - optimized for 14M packets
    constexpr size_t LOG_FILE_SIZE = 500 * 1024 * 1024;  // 500MB per file
    constexpr int LOG_FILE_COUNT = 50;                    // 50 files = 25GB total
    constexpr size_t ASYNC_QUEUE_SIZE = 1024 * 1024;     // 1M queue size
    constexpr int ASYNC_THREADS = 4;                     // 4 background threads
    constexpr int STATS_INTERVAL = 100000;              // Report every 100K packets
    constexpr int FLUSH_INTERVAL = 1000000;             // Force flush every 1M packets
}

// CBOE Sequenced Unit Header structure (packed to match network format)
#pragma pack(push, 1)
struct CboeSequencedUnitHeader {
    uint16_t hdr_length;    // Length of the packet
    uint8_t hdr_count;      // Number of messages in packet
    uint8_t hdr_unit;       // Unit identifier
    uint32_t hdr_sequence;  // Sequence number
};

// CBOE Message Header structure (each message within a packet)
struct CboeMessageHeader {
    uint8_t length;         // Length of this message
    uint8_t message_type;   // Message type identifier
};

// Compact binary record structure for spdlog
struct BinaryLogRecord {
    uint64_t timestamp_ns;      // 8 bytes - nanosecond timestamp
    uint32_t packet_id;         // 4 bytes - packet sequence
    uint32_t sequence;          // 4 bytes - CBOE sequence number
    uint32_t src_ip;            // 4 bytes - source IP (binary)
    uint16_t port;              // 2 bytes - port number
    uint16_t length;            // 2 bytes - packet length
    uint8_t count;              // 1 byte - message count
    uint8_t unit;               // 1 byte - unit identifier
    uint8_t packet_type;        // 1 byte - packet type enum
    uint8_t order_status;       // 1 byte - order status enum
    uint16_t payload_length;    // 2 bytes - actual payload length stored
    // Variable length payload follows in the same log entry
} __attribute__((packed));
#pragma pack(pop)

// Packet type enumeration for binary storage
enum class PacketType : uint8_t {
    HEARTBEAT = 0,
    ADMIN = 1,
    UNSEQUENCED = 2,
    DATA = 3
};

// Order status enumeration for binary storage
enum class OrderStatus : uint8_t {
    UNSEQUENCED = 0,
    SEQUENCED_FIRST = 1,
    SEQUENCED_IN_ORDER = 2,
    SEQUENCED_OUT_OF_ORDER_LATE = 3,
    SEQUENCED_OUT_OF_ORDER_EARLY = 4,
    SEQUENCED_DUPLICATE = 5
};

// Message type information structure
struct MessageTypeInfo {
    uint8_t type_id;
    const char* name;
    const char* description;
    uint8_t min_length;
};

// Sequence tracking for each unit
struct SequenceTracker {
    uint32_t last_confirmed_seq = 0;
    uint32_t highest_seen_seq = 0;
    std::map<uint32_t, bool> pending_sequences;
    
    SequenceTracker() = default;
};

// Function declarations
const MessageTypeInfo* lookup_message_type(uint8_t type_id);
PacketType classify_packet_type(uint32_t seq, uint8_t count, int len);
uint32_t ip_to_binary(const std::string& ip_str);

// Safe endian conversion functions
uint16_t le16toh_safe(uint16_t val);
uint32_t le32toh_safe(uint32_t val);
uint64_t le64toh_safe(uint64_t val);