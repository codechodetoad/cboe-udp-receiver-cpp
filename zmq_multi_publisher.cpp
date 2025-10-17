#include <zmq.h>
#include <iostream>
#include <chrono>
#include <thread>
#include <vector>
#include <random>
#include <atomic>
#include <csignal>
#include <cstring>

constexpr int NUM_THREADS = 4;
constexpr int TARGET_RATE_PER_THREAD = 250000; // 250k per thread = 1M total

std::atomic<bool> running(true);

// Per-thread statistics
struct ThreadStats {
    std::atomic<uint64_t> packets_sent{0};
    std::atomic<uint64_t> send_errors{0};
    std::atomic<uint64_t> dropped_packets{0};
    int thread_id;
};

ThreadStats thread_stats[NUM_THREADS];

void signal_handler(int signal) {
    std::cout << "\nReceived signal " << signal << ", stopping all publishers..." << std::endl;
    running = false;
}

void publisher_thread(int thread_id) {
    void* context = zmq_ctx_new();
    void* pub1 = zmq_socket(context, ZMQ_PUB);
    void* pub2 = zmq_socket(context, ZMQ_PUB);
    
    // High water mark for 1M pps
    int hwm = 1000000;
    zmq_setsockopt(pub1, ZMQ_SNDHWM, &hwm, sizeof(hwm));
    zmq_setsockopt(pub2, ZMQ_SNDHWM, &hwm, sizeof(hwm));
    
    // Each thread binds to different IPC endpoints
    std::string endpoint1 = "ipc:///tmp/cboe_port1_t" + std::to_string(thread_id) + ".ipc";
    std::string endpoint2 = "ipc:///tmp/cboe_port2_t" + std::to_string(thread_id) + ".ipc";
    
    zmq_bind(pub1, endpoint1.c_str());
    zmq_bind(pub2, endpoint2.c_str());
    
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> size_dist(40, 200);
    std::uniform_int_distribution<> data_dist(0, 255);
    
    uint32_t sequence = thread_id * 10000000; // Unique sequence per thread
    
    auto start_time = std::chrono::steady_clock::now();
    auto next_send_time = start_time;
    
    while (running) {
        // Generate packet
        int packet_size = size_dist(gen);
        char packet_data[256];
        
        // CBOE-like header with thread-specific sequence
        uint16_t length = packet_size;
        uint8_t count = 1 + (sequence % 5);
        uint8_t unit = thread_id + 1;
        
        memcpy(packet_data, &length, sizeof(length));
        memcpy(packet_data + 2, &count, sizeof(count));
        memcpy(packet_data + 3, &unit, sizeof(unit));
        memcpy(packet_data + 4, &sequence, sizeof(sequence));
        
        // Fill with random data
        for (int i = 8; i < packet_size; i++) {
            packet_data[i] = static_cast<char>(data_dist(gen));
        }
        
        // Send to both publishers
        int result1 = zmq_send(pub1, packet_data, packet_size, ZMQ_DONTWAIT);
        int result2 = zmq_send(pub2, packet_data, packet_size, ZMQ_DONTWAIT);
        
        if (result1 > 0 && result2 > 0) {
            thread_stats[thread_id].packets_sent++;
            sequence++;
        } else {
            if (result1 == -1 && zmq_errno() == EAGAIN) {
                thread_stats[thread_id].dropped_packets++;
            }
            if (result2 == -1 && zmq_errno() == EAGAIN) {
                thread_stats[thread_id].dropped_packets++;
            }
            thread_stats[thread_id].send_errors++;
        }
        
        // Rate limiting for 250k pps per thread
        next_send_time += std::chrono::nanoseconds(4000); // 4μs = 250k pps
        std::this_thread::sleep_until(next_send_time);
    }
    
    zmq_close(pub1);
    zmq_close(pub2);
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
            uint64_t total_sent = 0;
            uint64_t total_errors = 0;
            uint64_t total_dropped = 0;
            
            std::cout << "\n=== Per-Thread Statistics ===" << std::endl;
            
            for (int i = 0; i < NUM_THREADS; i++) {
                uint64_t current_count = thread_stats[i].packets_sent.load();
                uint64_t packets_this_period = current_count - last_counts[i];
                double rate = (double)packets_this_period * 1000.0 / duration;
                
                uint64_t errors = thread_stats[i].send_errors.load();
                uint64_t dropped = thread_stats[i].dropped_packets.load();
                
                std::cout << "Thread " << i << ": " 
                         << (int)rate << " pps | "
                         << "Total: " << current_count << " | "
                         << "Errors: " << errors << " | "
                         << "Dropped: " << dropped << std::endl;
                
                total_sent += current_count;
                total_errors += errors;
                total_dropped += dropped;
                last_counts[i] = current_count;
            }
            
            std::cout << "TOTAL: " << total_sent << " sent | "
                     << total_errors << " errors | "
                     << total_dropped << " dropped" << std::endl;
            std::cout << "========================================" << std::endl;
            
            last_time = now;
        }
    }
}

int main() {
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    std::cout << "Multi-threaded ZMQ Publisher for 1M pps" << std::endl;
    std::cout << "Threads: " << NUM_THREADS << std::endl;
    std::cout << "Target per thread: " << TARGET_RATE_PER_THREAD << " pps" << std::endl;
    std::cout << "Total target: " << (NUM_THREADS * TARGET_RATE_PER_THREAD) << " pps" << std::endl;
    
    // Initialize thread stats
    for (int i = 0; i < NUM_THREADS; i++) {
        thread_stats[i].thread_id = i;
    }
    
    // Start publisher threads
    std::vector<std::thread> publishers;
    for (int i = 0; i < NUM_THREADS; i++) {
        publishers.emplace_back(publisher_thread, i);
    }
    
    // Start stats thread
    std::thread stats(stats_thread);
    
    // Wait for all threads
    for (auto& t : publishers) {
        t.join();
    }
    stats.join();
    
    // Final statistics
    uint64_t total_sent = 0;
    uint64_t total_errors = 0;
    uint64_t total_dropped = 0;
    
    std::cout << "\n=== Final Statistics ===" << std::endl;
    for (int i = 0; i < NUM_THREADS; i++) {
        uint64_t sent = thread_stats[i].packets_sent.load();
        uint64_t errors = thread_stats[i].send_errors.load();
        uint64_t dropped = thread_stats[i].dropped_packets.load();
        
        std::cout << "Thread " << i << ": " << sent << " sent, " 
                 << errors << " errors, " << dropped << " dropped" << std::endl;
        
        total_sent += sent;
        total_errors += errors;
        total_dropped += dropped;
    }
    
    std::cout << "TOTAL: " << total_sent << " sent, " 
             << total_errors << " errors, " << total_dropped << " dropped" << std::endl;
    
    double loss_rate = (double)total_dropped / (total_sent + total_dropped) * 100.0;
    std::cout << "Loss rate: " << loss_rate << "%" << std::endl;
    
    return 0;
}