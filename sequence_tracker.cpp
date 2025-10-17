#include "sequence_tracker.h"
#include <algorithm>
#include <limits>

OrderStatus SequenceManager::determine_order_status(uint32_t seq, uint8_t count, int port, uint8_t unit) {
    if (seq == 0) {
        return OrderStatus::UNSEQUENCED;
    }

    auto key = std::make_pair(port, static_cast<int>(unit));
    auto& tracker = trackers_[key];

    uint32_t message_count = (count > 0) ? static_cast<uint32_t>(count) : 1;

    // Check for potential overflow: if adding message_count would overflow
    if (seq > std::numeric_limits<uint32_t>::max() - message_count + 1) {
        // Near overflow condition - treat as potential wrap-around
        // In production, CBOE sequences typically reset before overflow
        message_count = 1;  // Safe fallback
    }

    // First packet for this unit
    if (tracker.last_confirmed_seq == 0 && tracker.highest_seen_seq == 0) {
        tracker.last_confirmed_seq = seq + message_count - 1;
        tracker.highest_seen_seq = seq + message_count - 1;
        return OrderStatus::SEQUENCED_FIRST;
    }
    
    uint32_t expected = tracker.last_confirmed_seq + 1;
    
    // Exactly what we expected
    if (seq == expected) {
        tracker.last_confirmed_seq = seq + message_count - 1;
        
        // Check if we can now confirm any pending sequences
        while (true) {
            auto next_expected = tracker.last_confirmed_seq + 1;
            auto it = tracker.pending_sequences.find(next_expected);
            if (it != tracker.pending_sequences.end()) {
                uint32_t confirmed_end = next_expected;
                while (tracker.pending_sequences.find(confirmed_end + 1) != tracker.pending_sequences.end()) {
                    confirmed_end++;
                }
                
                tracker.last_confirmed_seq = confirmed_end;
                for (uint32_t s = next_expected; s <= confirmed_end; s++) {
                    tracker.pending_sequences.erase(s);
                }
            } else {
                break;
            }
        }
        return OrderStatus::SEQUENCED_IN_ORDER;
    }
    // Earlier than expected - definitely out of order (late arrival)
    else if (seq < expected) {
        return (seq <= tracker.last_confirmed_seq) ? 
               OrderStatus::SEQUENCED_DUPLICATE : 
               OrderStatus::SEQUENCED_OUT_OF_ORDER_LATE;
    }
    // Later than expected - early arrival
    else {
        for (uint32_t i = 0; i < message_count; i++) {
            tracker.pending_sequences[seq + i] = true;
        }
        tracker.highest_seen_seq = std::max(tracker.highest_seen_seq, seq + message_count - 1);
        return OrderStatus::SEQUENCED_OUT_OF_ORDER_EARLY;
    }
}

const SequenceTracker* SequenceManager::get_tracker(int port, uint8_t unit) const {
    auto key = std::make_pair(port, static_cast<int>(unit));
    auto it = trackers_.find(key);
    return (it != trackers_.end()) ? &it->second : nullptr;
}

void SequenceManager::clear() {
    trackers_.clear();
}