#pragma once

#include "packet_types.h"
#include <map>
#include <utility>

/**
 * Manages sequence tracking for CBOE packet ordering
 */
class SequenceManager {
public:
    /**
     * Determine order status for a packet
     * @param seq Sequence number
     * @param count Message count in packet
     * @param port Port number
     * @param unit Unit identifier
     * @return OrderStatus enum value
     */
    OrderStatus determine_order_status(uint32_t seq, uint8_t count, int port, uint8_t unit);
    
    /**
     * Get statistics for a specific port/unit combination
     */
    const SequenceTracker* get_tracker(int port, uint8_t unit) const;
    
    /**
     * Clear all tracking data
     */
    void clear();
    
    /**
     * Get total number of tracked units
     */
    size_t get_tracker_count() const { return trackers_.size(); }

private:
    std::map<std::pair<int, int>, SequenceTracker> trackers_;
};