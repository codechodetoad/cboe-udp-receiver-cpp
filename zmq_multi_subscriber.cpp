#include <zmq.h>
#include <iostream>
#include <chrono>
#include <thread>
#include <vector>
#include <atomic>
#include <csignal>
#include <unordered_set>
#include <cstring>

constexpr int NUM_THREADS = 4;

std::atomic<bool> running(true);

// Per-thread statistics
struct ThreadStats {
    std::atomic<uint64_t> packets_received{0};
    std::atomic<uint64_t> receive_errors{0};
    std::atomic<uint64_t> duplicate_packets{0};
    std::atomic<uint64_t> out_of_order_packets{0};
    std::atomic<uint64_t> missing_packets{0};
    int thread_id;
};

ThreadStats thread_stats[NUM_THREADS];

void signal_handler(int signal) {
    std::cout << "\nReceived signal " << signal << ", stopping all subscribers..." << std::endl;
    running = false;
}

void subscriber_thread(int thread_id) {
    void* context = zmq_ctx_new();
    void* sub1 = zmq_socket(context, ZMQ_SUB);
    void* sub2 = zmq_socket(context, ZMQ_SUB);
    
    // High water mark for 1M pps
    int hwm = 1000000;
    zmq_setsockopt(sub1, ZMQ_RCVHWM, &hwm, sizeof(hwm));
    zmq_setsockopt(sub2, ZMQ_RCVHWM, &hwm, sizeof(hwm));
    
    int timeout = 10; // 10ms timeout
    zmq_setsockopt(sub1, ZMQ_RCVTIMEO, &timeout, sizeof(timeout));
    zmq_setsockopt(sub2, ZMQ_RCVTIMEO, &timeout, sizeof(timeout));
    
    // Connect to thread-specific endpoints
    std::string endpoint1 = "ipc:///tmp/cboe_port1_t" + std::to_string(thread_id) + ".ipc";
    std::string endpoint2 = "ipc:///tmp/cboe_port2_t" + std::to_string(thread_id) + ".ipc";
    
    zmq_connect(sub1, endpoint1.c_str());
    zmq_connect(sub2, endpoint2.c_str());
    
    // Subscribe to all messages
    zmq_setsockopt(sub1, ZMQ_SUBSCRIBE, "", 0);
    zmq_setsockopt(sub2, ZMQ_SUBSCRIBE, "", 0);
    
    char buffer[2048];
    std::unordered_set<uint32_t> seen_sequences;
    uint32_t last_sequence = 0;
    uint32_t expected_sequence = thread_id * 10000000; // Match publisher start
    
    while (running) {
        // Try to receive from both sockets
        int size1 = zmq_recv(sub1, buffer, sizeof(buffer), ZMQ_DONTWAIT);
        int size2 = zmq_recv(sub2, buffer, sizeof(buffer), ZMQ_DONTWAIT);
        
        // Process received packets
        for (int size : {size1, size2}) {
            if (size > 0) {
                thread_stats[thread_id].packets_received++;
                
                // Extract sequence number from packet
                if (size >= 8) {
                    uint32_t sequence;
                    memcpy(&sequence, buffer + 4, sizeof(sequence));
                    
                    // Check for duplicates
                    if (seen_sequences.find(sequence) != seen_sequences.end()) {
                        thread_stats[thread_id].duplicate_packets++;
                    } else {
                        seen_sequences.insert(sequence);
                    }
                    
                    // Check for out-of-order
                    if (sequence < last_sequence) {
                        thread_stats[thread_id].out_of_order_packets++;
                    }
                    
                    // Calculate missing packets
                    if (sequence > expected_sequence) {
                        thread_stats[thread_id].missing_packets += (sequence - expected_sequence);
                        expected_sequence = sequence + 1;
                    } else if (sequence == expected_sequence) {
                        expected_sequence++;
                    }
                    
                    last_sequence = sequence;
                }
            } else if (size == -1) {
                int err = zmq_errno();
                if (err != EAGAIN && err != ETIMEDOUT) {
                    thread_stats[thread_id].receive_errors++;
                }
            }
        }
        
        // Small sleep if no data received
        if (size1 <= 0 && size2 <= 0) {
            std::this_thread::sleep_for(std::chrono::microseconds(1));
        }
    }
    
    zmq_close(sub1);
    zmq_close(sub2);
    zmq_ctx_destroy(context);
}

void stats_thread() {
    auto last_time = std::chrono::steady_clock::now();
    uint64_t last_counts[NUM_THREADS] = {0};
    
    while (running) {
        std::this_thread::sleep_for(std::chrono::seconds(2));
        
        auto now = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_time).count();
        
        if (duration >= 2000) {
            uint64_t total_received = 0;
            uint64_t total_errors = 0;
            uint64_t total_duplicates = 0;
            uint64_t total_ooo = 0;
            uint64_t total_missing = 0;
            
            std::cout << "\n=== Per-Thread Receive Statistics ===" << std::endl;
            
            for (int i = 0; i < NUM_THREADS; i++) {
                uint64_t current_count = thread_stats[i].packets_received.load();
                uint64_t packets_this_period = current_count - last_counts[i];
                double rate = (double)packets_this_period * 1000.0 / duration;
                
                uint64_t errors = thread_stats[i].receive_errors.load();
                uint64_t duplicates = thread_stats[i].duplicate_packets.load();
                uint64_t ooo = thread_stats[i].out_of_order_packets.load();
                uint64_t missing = thread_stats[i].missing_packets.load();
                
                std::cout << "Thread " << i << ": " 
                         << (int)rate << " pps | "
                         << "Total: " << current_count << " | "
                         << "Missing: " << missing << " | "
                         << "Dups: " << duplicates << " | "
                         << "OOO: " << ooo << " | "
                         << "Errors: " << errors << std::endl;
                
                total_received += current_count;
                total_errors += errors;
                total_duplicates += duplicates;
                total_ooo += ooo;
                total_missing += missing;
                last_counts[i] = current_count;
            }
            
            std::cout << "TOTAL: " << total_received << " received | "
                     << total_missing << " missing | "
                     << total_duplicates << " duplicates | "
                     << total_ooo << " out-of-order | "
                     << total_errors << " errors" << std::endl;
            
            double loss_rate = (double)total_missing / (total_received + total_missing) * 100.0;
            std::cout << "Loss rate: " << loss_rate << "%" << std::endl;
            std::cout << "========================================" << std::endl;
            
            last_time = now;
        }
    }
}

int main() {
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    std::cout << "Multi-threaded ZMQ Subscriber for 1M pps" << std::endl;
    std::cout << "Threads: " << NUM_THREADS << std::endl;
    std::cout << "Monitoring packet loss and throughput per thread..." << std::endl;
    
    // Initialize thread stats
    for (int i = 0; i < NUM_THREADS; i++) {
        thread_stats[i].thread_id = i;
    }
    
    // Start subscriber threads
    std::vector<std::thread> subscribers;
    for (int i = 0; i < NUM_THREADS; i++) {
        subscribers.emplace_back(subscriber_thread, i);
    }
    
    // Start stats thread
    std::thread stats(stats_thread);
    
    // Wait for all threads
    for (auto& t : subscribers) {
        t.join();
    }
    stats.join();
    
    // Final statistics
    uint64_t total_received = 0;
    uint64_t total_errors = 0;
    uint64_t total_duplicates = 0;
    uint64_t total_ooo = 0;
    uint64_t total_missing = 0;
    
    std::cout << "\n=== Final Statistics ===" << std::endl;
    for (int i = 0; i < NUM_THREADS; i++) {
        uint64_t received = thread_stats[i].packets_received.load();
        uint64_t errors = thread_stats[i].receive_errors.load();
        uint64_t duplicates = thread_stats[i].duplicate_packets.load();
        uint64_t ooo = thread_stats[i].out_of_order_packets.load();
        uint64_t missing = thread_stats[i].missing_packets.load();
        
        std::cout << "Thread " << i << ": " << received << " received, " 
                 << missing << " missing, " << duplicates << " duplicates, "
                 << ooo << " out-of-order, " << errors << " errors" << std::endl;
        
        total_received += received;
        total_errors += errors;
        total_duplicates += duplicates;
        total_ooo += ooo;
        total_missing += missing;
    }
    
    std::cout << "TOTAL: " << total_received << " received, " 
             << total_missing << " missing, " << total_duplicates << " duplicates, "
             << total_ooo << " out-of-order, " << total_errors << " errors" << std::endl;
    
    double loss_rate = (double)total_missing / (total_received + total_missing) * 100.0;
    std::cout << "Overall loss rate: " << loss_rate << "%" << std::endl;
    
    return 0;
}