#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <iomanip>
#include <chrono>
#include <cstring>
#include <arpa/inet.h>
#include <sstream>
#include <map>
#include <algorithm>

// Must match the binary logger structures exactly
#pragma pack(push, 1)
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
    // Variable length payload follows
};

// CBOE Sequenced Unit Header structure
struct CboeSequencedUnitHeader {
    uint16_t hdr_length;    // Length of the packet
    uint8_t hdr_count;      // Number of messages in packet
    uint8_t hdr_unit;       // Unit identifier
    uint32_t hdr_sequence;  // Sequence number
};

// CBOE Message Header structure
struct CboeMessageHeader {
    uint8_t length;         // Length of this message
    uint8_t message_type;   // Message type identifier
};
#pragma pack(pop)

// Enumerations matching the logger
enum class PacketType : uint8_t {
    HEARTBEAT = 0,
    ADMIN = 1,
    UNSEQUENCED = 2,
    DATA = 3
};

enum class OrderStatus : uint8_t {
    UNSEQUENCED = 0,
    SEQUENCED_FIRST = 1,
    SEQUENCED_IN_ORDER = 2,
    SEQUENCED_OUT_OF_ORDER_LATE = 3,
    SEQUENCED_OUT_OF_ORDER_EARLY = 4,
    SEQUENCED_DUPLICATE = 5
};

// CBOE Message Type mapping
struct MessageTypeInfo {
    uint8_t type_id;
    const char* name;
    const char* description;
};

static const MessageTypeInfo CBOE_MESSAGE_TYPES[] = {
    {0x97, "UNIT_CLEAR", "Unit Clear"},
    {0x3B, "TRADING_STATUS", "Trading Status"},
    {0x37, "ADD_ORDER", "Add Order"},
    {0x38, "ORDER_EXECUTED", "Order Executed"},
    {0x58, "ORDER_EXECUTED_AT_PRICE", "Order Executed at Price"},
    {0x39, "REDUCE_SIZE", "Reduce Size"},
    {0x3A, "MODIFY_ORDER", "Modify Order"},
    {0x3C, "DELETE_ORDER", "Delete Order"},
    {0x3D, "TRADE", "Trade"},
    {0x3E, "TRADE_BREAK", "Trade Break"},
    {0xE3, "CALCULATED_VALUE", "Calculated Value"},
    {0x2D, "END_OF_SESSION", "End of Session"},
    {0x59, "AUCTION_UPDATE", "Auction Update"},
    {0x5A, "AUCTION_SUMMARY", "Auction Summary"},
    {0x01, "LOGIN", "Login"},
    {0x02, "LOGIN_RESPONSE", "Login Response"},
    {0x03, "GAP_REQUEST", "Gap Request"},
    {0x04, "GAP_RESPONSE", "Gap Response"},
    {0x80, "SPIN_IMAGE_AVAILABLE", "Spin Image Available"},
    {0x81, "SPIN_REQUEST", "Spin Request"},
    {0x82, "SPIN_RESPONSE", "Spin Response"},
    {0x83, "SPIN_FINISHED", "Spin Finished"}
};

static const int NUM_MESSAGE_TYPES = sizeof(CBOE_MESSAGE_TYPES) / sizeof(MessageTypeInfo);

class BinaryLogReader {
private:
    std::ifstream file;
    std::string filename;
    size_t file_size;
    size_t bytes_read;
    
public:
    BinaryLogReader(const std::string& fname) : filename(fname), bytes_read(0) {
        file.open(filename, std::ios::binary);
        if (!file.is_open()) {
            throw std::runtime_error("Cannot open file: " + filename);
        }
        
        // Get file size
        file.seekg(0, std::ios::end);
        file_size = file.tellg();
        file.seekg(0, std::ios::beg);
    }
    
    ~BinaryLogReader() {
        if (file.is_open()) {
            file.close();
        }
    }
    
    bool read_record(BinaryLogRecord& record, std::vector<char>& payload) {
        if (bytes_read >= file_size) {
            return false;
        }
        
        // Read the fixed part of the record
        file.read(reinterpret_cast<char*>(&record), sizeof(BinaryLogRecord));
        if (file.gcount() != sizeof(BinaryLogRecord)) {
            return false;
        }
        bytes_read += sizeof(BinaryLogRecord);
        
        // Read the variable payload
        payload.resize(record.payload_length);
        if (record.payload_length > 0) {
            file.read(payload.data(), record.payload_length);
            if (file.gcount() != record.payload_length) {
                return false;
            }
            bytes_read += record.payload_length;
        }
        
        return true;
    }
    
    size_t get_file_size() const { return file_size; }
    size_t get_bytes_read() const { return bytes_read; }
    double get_progress() const { 
        return file_size > 0 ? static_cast<double>(bytes_read) / file_size * 100.0 : 0.0; 
    }
};

/**
 * Convert binary IP back to string
 */
std::string binary_to_ip(uint32_t binary_ip) {
    struct in_addr addr;
    addr.s_addr = binary_ip;
    return std::string(inet_ntoa(addr));
}

/**
 * Convert timestamp to human readable format
 */
std::string timestamp_to_string(uint64_t timestamp_ns) {
    auto time_point = std::chrono::time_point<std::chrono::system_clock>(
        std::chrono::nanoseconds(timestamp_ns));
    auto time_t = std::chrono::system_clock::to_time_t(time_point);
    auto ns_part = timestamp_ns % 1000000000;
    
    std::ostringstream oss;
    oss << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S");
    oss << "." << std::setfill('0') << std::setw(9) << ns_part;
    return oss.str();
}

/**
 * Convert packet type enum to string
 */
const char* packet_type_to_string(PacketType type) {
    switch (type) {
        case PacketType::HEARTBEAT: return "HEARTBEAT";
        case PacketType::ADMIN: return "ADMIN";
        case PacketType::UNSEQUENCED: return "UNSEQUENCED";
        case PacketType::DATA: return "DATA";
        default: return "UNKNOWN";
    }
}

/**
 * Convert order status enum to string
 */
const char* order_status_to_string(OrderStatus status) {
    switch (status) {
        case OrderStatus::UNSEQUENCED: return "UNSEQUENCED";
        case OrderStatus::SEQUENCED_FIRST: return "SEQUENCED-FIRST";
        case OrderStatus::SEQUENCED_IN_ORDER: return "SEQUENCED-IN-ORDER";
        case OrderStatus::SEQUENCED_OUT_OF_ORDER_LATE: return "SEQUENCED-OUT-OF-ORDER-LATE";
        case OrderStatus::SEQUENCED_OUT_OF_ORDER_EARLY: return "SEQUENCED-OUT-OF-ORDER-EARLY";
        case OrderStatus::SEQUENCED_DUPLICATE: return "SEQUENCED-DUPLICATE";
        default: return "UNKNOWN";
    }
}

/**
 * Lookup message type information
 */
const MessageTypeInfo* lookup_message_type(uint8_t type_id) {
    for (int i = 0; i < NUM_MESSAGE_TYPES; i++) {
        if (CBOE_MESSAGE_TYPES[i].type_id == type_id) {
            return &CBOE_MESSAGE_TYPES[i];
        }
    }
    return nullptr;
}

/**
 * Safe little-endian conversion functions
 */
uint16_t le16toh_safe(uint16_t val) {
    return le16toh(val);
}

uint32_t le32toh_safe(uint32_t val) {
    return le32toh(val);
}

uint64_t le64toh_safe(uint64_t val) {
    return le64toh(val);
}

/**
 * Parse messages within payload for detailed analysis
 */
std::vector<std::string> parse_payload_messages(const std::vector<char>& payload) {
    std::vector<std::string> messages;
    
    if (payload.size() < sizeof(CboeSequencedUnitHeader)) {
        return messages;
    }
    
    // Skip the unit header
    int offset = sizeof(CboeSequencedUnitHeader);
    int message_count = 0;
    
    while (offset < static_cast<int>(payload.size()) && message_count < 100) {
        if (offset + 2 > static_cast<int>(payload.size())) break;
        
        const CboeMessageHeader* msg_header = reinterpret_cast<const CboeMessageHeader*>(payload.data() + offset);
        uint8_t msg_length = msg_header->length;
        uint8_t msg_type = msg_header->message_type;
        
        if (msg_length == 0 || offset + msg_length > static_cast<int>(payload.size())) {
            break;
        }
        
        // Look up message type information
        const MessageTypeInfo* type_info = lookup_message_type(msg_type);
        std::string type_name = type_info ? type_info->name : "UNKNOWN";
        
        std::ostringstream msg_oss;
        msg_oss << "Type=0x" << std::hex << std::setfill('0') << std::setw(2) 
                << static_cast<int>(msg_type) << " (" << type_name << "), Len=" 
                << std::dec << static_cast<int>(msg_length);
        
        messages.push_back(msg_oss.str());
        
        offset += msg_length;
        message_count++;
    }
    
    return messages;
}

/**
 * Statistics tracking structure
 */
struct LogStatistics {
    uint64_t total_records = 0;
    std::map<PacketType, uint64_t> packet_type_counts;
    std::map<OrderStatus, uint64_t> order_status_counts;
    std::map<uint16_t, uint64_t> port_counts;
    std::map<uint8_t, uint64_t> unit_counts;
    std::map<uint8_t, uint64_t> message_type_counts;
    uint64_t min_timestamp = UINT64_MAX;
    uint64_t max_timestamp = 0;
    uint32_t min_sequence = UINT32_MAX;
    uint32_t max_sequence = 0;
    uint64_t out_of_order_count = 0;
    uint64_t duplicate_count = 0;
    
    void update(const BinaryLogRecord& record, const std::vector<char>& payload) {
        total_records++;
        packet_type_counts[static_cast<PacketType>(record.packet_type)]++;
        order_status_counts[static_cast<OrderStatus>(record.order_status)]++;
        port_counts[record.port]++;
        unit_counts[record.unit]++;
        
        min_timestamp = std::min(min_timestamp, record.timestamp_ns);
        max_timestamp = std::max(max_timestamp, record.timestamp_ns);
        
        if (record.sequence > 0) {
            min_sequence = std::min(min_sequence, record.sequence);
            max_sequence = std::max(max_sequence, record.sequence);
        }
        
        OrderStatus status = static_cast<OrderStatus>(record.order_status);
        if (status == OrderStatus::SEQUENCED_OUT_OF_ORDER_EARLY || 
            status == OrderStatus::SEQUENCED_OUT_OF_ORDER_LATE) {
            out_of_order_count++;
        }
        if (status == OrderStatus::SEQUENCED_DUPLICATE) {
            duplicate_count++;
        }
        
        // Count message types in payload
        if (payload.size() >= sizeof(CboeSequencedUnitHeader)) {
            int offset = sizeof(CboeSequencedUnitHeader);
            while (offset + 2 <= static_cast<int>(payload.size())) {
                const CboeMessageHeader* msg_header = reinterpret_cast<const CboeMessageHeader*>(payload.data() + offset);
                if (msg_header->length == 0 || offset + msg_header->length > static_cast<int>(payload.size())) {
                    break;
                }
                message_type_counts[msg_header->message_type]++;
                offset += msg_header->length;
                if (offset >= static_cast<int>(payload.size())) break;
            }
        }
    }
    
    void print_summary() const {
        std::cout << "\n=== BINARY LOG ANALYSIS SUMMARY ===" << std::endl;
        std::cout << "Total Records: " << total_records << std::endl;
        
        if (min_timestamp != UINT64_MAX && max_timestamp > 0) {
            std::cout << "Time Range: " << timestamp_to_string(min_timestamp) 
                     << " to " << timestamp_to_string(max_timestamp) << std::endl;
            double duration_seconds = static_cast<double>(max_timestamp - min_timestamp) / 1e9;
            std::cout << "Duration: " << std::fixed << std::setprecision(3) 
                     << duration_seconds << " seconds" << std::endl;
            if (duration_seconds > 0) {
                std::cout << "Average Rate: " << std::fixed << std::setprecision(1)
                         << total_records / duration_seconds << " packets/second" << std::endl;
            }
        }
        
        if (min_sequence != UINT32_MAX && max_sequence > 0) {
            std::cout << "Sequence Range: " << min_sequence << " to " << max_sequence << std::endl;
        }
        
        std::cout << "\nPacket Type Distribution:" << std::endl;
        for (const auto& [type, count] : packet_type_counts) {
            double percentage = static_cast<double>(count) / total_records * 100.0;
            std::cout << "  " << packet_type_to_string(type) << ": " << count 
                     << " (" << std::fixed << std::setprecision(2) << percentage << "%)" << std::endl;
        }
        
        std::cout << "\nOrder Status Distribution:" << std::endl;
        for (const auto& [status, count] : order_status_counts) {
            double percentage = static_cast<double>(count) / total_records * 100.0;
            std::cout << "  " << order_status_to_string(status) << ": " << count 
                     << " (" << std::fixed << std::setprecision(2) << percentage << "%)" << std::endl;
        }
        
        std::cout << "\nPort Distribution:" << std::endl;
        for (const auto& [port, count] : port_counts) {
            double percentage = static_cast<double>(count) / total_records * 100.0;
            std::cout << "  Port " << port << ": " << count 
                     << " (" << std::fixed << std::setprecision(2) << percentage << "%)" << std::endl;
        }
        
        if (!message_type_counts.empty()) {
            std::cout << "\nTop Message Types:" << std::endl;
            std::vector<std::pair<uint64_t, uint8_t>> sorted_types;
            for (const auto& [type, count] : message_type_counts) {
                sorted_types.emplace_back(count, type);
            }
            std::sort(sorted_types.rbegin(), sorted_types.rend());
            
            int shown = 0;
            for (const auto& [count, type] : sorted_types) {
                if (shown++ >= 10) break; // Show top 10
                const MessageTypeInfo* info = lookup_message_type(type);
                std::string name = info ? info->name : "UNKNOWN";
                std::cout << "  0x" << std::hex << std::setfill('0') << std::setw(2) 
                         << static_cast<int>(type) << std::dec << " (" << name << "): " 
                         << count << std::endl;
            }
        }
        
        if (out_of_order_count > 0 || duplicate_count > 0) {
            std::cout << "\nSequencing Issues:" << std::endl;
            std::cout << "  Out-of-order packets: " << out_of_order_count << std::endl;
            std::cout << "  Duplicate packets: " << duplicate_count << std::endl;
        }
    }
};

/**
 * Command-line options
 */
struct Options {
    std::string filename;
    bool show_statistics = false;
    bool show_details = false;
    bool show_messages = false;
    uint64_t max_records = 0; // 0 = unlimited
    uint32_t filter_sequence_start = 0;
    uint32_t filter_sequence_end = 0;
    uint16_t filter_port = 0;
    PacketType filter_packet_type = static_cast<PacketType>(255); // Invalid = no filter
    bool help = false;
};

void print_usage(const char* program_name) {
    std::cout << "Usage: " << program_name << " [options] <binary_log_file>" << std::endl;
    std::cout << "\nOptions:" << std::endl;
    std::cout << "  -s, --stats          Show statistics summary" << std::endl;
    std::cout << "  -d, --details        Show detailed packet information" << std::endl;
    std::cout << "  -m, --messages       Show message details within packets" << std::endl;
    std::cout << "  -n, --max-records N  Limit output to N records" << std::endl;
    std::cout << "  --seq-start N        Filter sequences >= N" << std::endl;
    std::cout << "  --seq-end N          Filter sequences <= N" << std::endl;
    std::cout << "  --port N             Filter by port number" << std::endl;
    std::cout << "  --type TYPE          Filter by packet type (HEARTBEAT|ADMIN|UNSEQUENCED|DATA)" << std::endl;
    std::cout << "  -h, --help           Show this help message" << std::endl;
}

PacketType string_to_packet_type(const std::string& str) {
    if (str == "HEARTBEAT") return PacketType::HEARTBEAT;
    if (str == "ADMIN") return PacketType::ADMIN;
    if (str == "UNSEQUENCED") return PacketType::UNSEQUENCED;
    if (str == "DATA") return PacketType::DATA;
    return static_cast<PacketType>(255); // Invalid
}

Options parse_arguments(int argc, char* argv[]) {
    Options opts;
    
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        
        if (arg == "-h" || arg == "--help") {
            opts.help = true;
        } else if (arg == "-s" || arg == "--stats") {
            opts.show_statistics = true;
        } else if (arg == "-d" || arg == "--details") {
            opts.show_details = true;
        } else if (arg == "-m" || arg == "--messages") {
            opts.show_messages = true;
        } else if (arg == "-n" || arg == "--max-records") {
            if (i + 1 < argc) {
                opts.max_records = std::stoull(argv[++i]);
            }
        } else if (arg == "--seq-start") {
            if (i + 1 < argc) {
                opts.filter_sequence_start = std::stoul(argv[++i]);
            }
        } else if (arg == "--seq-end") {
            if (i + 1 < argc) {
                opts.filter_sequence_end = std::stoul(argv[++i]);
            }
        } else if (arg == "--port") {
            if (i + 1 < argc) {
                opts.filter_port = std::stoul(argv[++i]);
            }
        } else if (arg == "--type") {
            if (i + 1 < argc) {
                opts.filter_packet_type = string_to_packet_type(argv[++i]);
            }
        } else if (arg[0] != '-') {
            opts.filename = arg;
        }
    }
    
    return opts;
}

int main(int argc, char* argv[]) {
    Options opts = parse_arguments(argc, argv);
    
    if (opts.help || opts.filename.empty()) {
        print_usage(argv[0]);
        return opts.help ? 0 : 1;
    }
    
    try {
        BinaryLogReader reader(opts.filename);
        LogStatistics stats;
        
        std::cout << "Reading binary log file: " << opts.filename << std::endl;
        std::cout << "File size: " << reader.get_file_size() << " bytes" << std::endl;
        
        BinaryLogRecord record;
        std::vector<char> payload;
        uint64_t records_processed = 0;
        uint64_t records_shown = 0;
        
        while (reader.read_record(record, payload)) {
            records_processed++;
            
            // Apply filters
            bool pass_filter = true;
            
            if (opts.filter_port != 0 && record.port != opts.filter_port) {
                pass_filter = false;
            }
            
            if (opts.filter_packet_type != static_cast<PacketType>(255) && 
                static_cast<PacketType>(record.packet_type) != opts.filter_packet_type) {
                pass_filter = false;
            }
            
            if (opts.filter_sequence_start > 0 && record.sequence < opts.filter_sequence_start) {
                pass_filter = false;
            }
            
            if (opts.filter_sequence_end > 0 && record.sequence > opts.filter_sequence_end) {
                pass_filter = false;
            }
            
            if (pass_filter) {
                stats.update(record, payload);
                
                if (opts.show_details && (opts.max_records == 0 || records_shown < opts.max_records)) {
                    std::cout << "\n--- Record " << records_shown + 1 << " ---" << std::endl;
                    std::cout << "Timestamp: " << timestamp_to_string(record.timestamp_ns) << std::endl;
                    std::cout << "Packet ID: " << record.packet_id << std::endl;
                    std::cout << "Sequence: " << record.sequence << std::endl;
                    std::cout << "Source IP: " << binary_to_ip(record.src_ip) << std::endl;
                    std::cout << "Port: " << record.port << std::endl;
                    std::cout << "Length: " << record.length << std::endl;
                    std::cout << "Count: " << static_cast<int>(record.count) << std::endl;
                    std::cout << "Unit: " << static_cast<int>(record.unit) << std::endl;
                    std::cout << "Packet Type: " << packet_type_to_string(static_cast<PacketType>(record.packet_type)) << std::endl;
                    std::cout << "Order Status: " << order_status_to_string(static_cast<OrderStatus>(record.order_status)) << std::endl;
                    std::cout << "Payload Length: " << record.payload_length << std::endl;
                    
                    if (opts.show_messages && !payload.empty()) {
                        std::vector<std::string> messages = parse_payload_messages(payload);
                        if (!messages.empty()) {
                            std::cout << "Messages:" << std::endl;
                            for (size_t i = 0; i < messages.size(); i++) {
                                std::cout << "  " << i + 1 << ": " << messages[i] << std::endl;
                            }
                        }
                    }
                    
                    records_shown++;
                }
            }
            
            // Progress indicator for large files
            if (records_processed % 10000 == 0) {
                std::cout << "\rProgress: " << std::fixed << std::setprecision(1) 
                         << reader.get_progress() << "% (" << records_processed 
                         << " records processed)" << std::flush;
            }
        }
        
        std::cout << "\rCompleted: 100.0% (" << records_processed << " records processed)" << std::endl;
        
        if (opts.show_statistics) {
            stats.print_summary();
        }
        
        if (!opts.show_details && !opts.show_statistics) {
            std::cout << "\nQuick Summary:" << std::endl;
            std::cout << "Total records processed: " << records_processed << std::endl;
            std::cout << "Use -s for statistics, -d for details, -m for message parsing" << std::endl;
        }
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}