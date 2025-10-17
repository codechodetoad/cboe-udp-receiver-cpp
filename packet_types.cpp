#include "packet_types.h"
#include <endian.h>
#include <arpa/inet.h>
#include <string>

// CBOE Message Type mapping
static const MessageTypeInfo CBOE_MESSAGE_TYPES[] = {
    {0x97, "UNIT_CLEAR", "Unit Clear", 2},
    {0x3B, "TRADING_STATUS", "Trading Status", 8},
    {0x37, "ADD_ORDER", "Add Order", 34},
    {0x38, "ORDER_EXECUTED", "Order Executed", 30},
    {0x58, "ORDER_EXECUTED_AT_PRICE", "Order Executed at Price", 38},
    {0x39, "REDUCE_SIZE", "Reduce Size", 18},
    {0x3A, "MODIFY_ORDER", "Modify Order", 34},
    {0x3C, "DELETE_ORDER", "Delete Order", 18},
    {0x3D, "TRADE", "Trade", 42},
    {0x3E, "TRADE_BREAK", "Trade Break", 18},
    {0xE3, "CALCULATED_VALUE", "Calculated Value", 26},
    {0x2D, "END_OF_SESSION", "End of Session", 2},
    {0x59, "AUCTION_UPDATE", "Auction Update", 30},
    {0x5A, "AUCTION_SUMMARY", "Auction Summary", 30},
    {0x01, "LOGIN", "Login", 44},
    {0x02, "LOGIN_RESPONSE", "Login Response", 3},
    {0x03, "GAP_REQUEST", "Gap Request", 20},
    {0x04, "GAP_RESPONSE", "Gap Response", 20},
    {0x80, "SPIN_IMAGE_AVAILABLE", "Spin Image Available", 20},
    {0x81, "SPIN_REQUEST", "Spin Request", 20},
    {0x82, "SPIN_RESPONSE", "Spin Response", 20},
    {0x83, "SPIN_FINISHED", "Spin Finished", 20}
};

static constexpr int NUM_MESSAGE_TYPES = sizeof(CBOE_MESSAGE_TYPES) / sizeof(MessageTypeInfo);

/**
 * Lookup message type information by type ID
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
 * Classify packet type based on sequence and count
 */
PacketType classify_packet_type(uint32_t seq, uint8_t count, int len) {
    if (seq == 0) {
        if (count == 0 && len <= 20) {
            return PacketType::HEARTBEAT;
        } else if (count == 0) {
            return PacketType::ADMIN;
        } else {
            return PacketType::UNSEQUENCED;
        }
    }
    return PacketType::DATA;
}

/**
 * Convert string IP to binary format for compact storage
 */
uint32_t ip_to_binary(const std::string& ip_str) {
    struct in_addr addr;
    if (inet_aton(ip_str.c_str(), &addr) == 0) {
        return 0;
    }
    return addr.s_addr;
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