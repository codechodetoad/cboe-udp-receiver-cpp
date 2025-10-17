#include <zmq.h>
#include <iostream>
#include <chrono>
#include <thread>
#include <atomic>
#include <csignal>
#include <unordered_set>
#include <cstring>

std::atomic<bool> running(true);
std::atomic<uint64_t> packets_received(0);
std::atomic<uint64_t> receive_errors(0);
std::atomic<uint64_t> duplicate_packets(0);
std::atomic<uint64_t> out_of_order_packets(0);

std::unordered_set<uint32_t> seen_sequences;
uint32_t last_sequence = 0;
uint32_t expected_sequence = 0;

void signal_handler(int signal) {
    std::cout << "\nReceived signal " << signal << ", stopping subscriber..." << std::endl;
    running = false;
}

void stats_thread() {
    auto last_time = std::chrono::steady_clock::now();
    uint64_t last_count = 0;
    
    while (running) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        
        auto now = std::chrono::steady_clock::now();
        uint64_t current_count = packets_received.load();
        
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_time).count();
        uint64_t packets_this_second = current_count - last_count;
        
        if (duration >= 1000) {
            double rate = (double)packets_this_second * 1000.0 / duration;
            uint64_t missing = expected_sequence - current_count;
            
            std::cout << "Rate: " << (int)rate << " pps | Total: " << current_count 
                     << " | Missing: " << missing
                     << " | Dups: " << duplicate_packets.load()
                     << " | OOO: " << out_of_order_packets.load()
                     << " | Errors: " << receive_errors.load() << std::endl;
            
            last_time = now;
            last_count = current_count;
        }
    }
}

int main() {
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    void* context = zmq_ctx_new();
    void* sub1 = zmq_socket(context, ZMQ_SUB);
    void* sub2 = zmq_socket(context, ZMQ_SUB);
    
    int hwm = 1000000;
    zmq_setsockopt(sub1, ZMQ_RCVHWM, &hwm, sizeof(hwm));
    zmq_setsockopt(sub2, ZMQ_RCVHWM, &hwm, sizeof(hwm));
    
    int timeout = 100;
    zmq_setsockopt(sub1, ZMQ_RCVTIMEO, &timeout, sizeof(timeout));
    zmq_setsockopt(sub2, ZMQ_RCVTIMEO, &timeout, sizeof(timeout));
    
    zmq_connect(sub1, "ipc:///tmp/cboe_port1.ipc");
    zmq_connect(sub2, "ipc:///tmp/cboe_port2.ipc");
    
    // Subscribe to all messages
    zmq_setsockopt(sub1, ZMQ_SUBSCRIBE, "", 0);
    zmq_setsockopt(sub2, ZMQ_SUBSCRIBE, "", 0);
    
    std::cout << "ZMQ Test Subscriber started" << std::endl;
    std::cout << "Subscribing to: ipc:///tmp/cboe_port1.ipc, ipc:///tmp/cboe_port2.ipc" << std::endl;
    std::cout << "Monitoring packet loss and rates..." << std::endl;
    
    // Start stats thread
    std::thread stats(stats_thread);
    
    char buffer[2048];
    
    while (running) {
        // Try to receive from both sockets
        int size1 = zmq_recv(sub1, buffer, sizeof(buffer), ZMQ_DONTWAIT);
        int size2 = zmq_recv(sub2, buffer, sizeof(buffer), ZMQ_DONTWAIT);
        
        // Process received packets
        for (int size : {size1, size2}) {
            if (size > 0) {
                packets_received++;
                
                // Extract sequence number from packet
                if (size >= 8) {
                    uint32_t sequence;
                    memcpy(&sequence, buffer + 4, sizeof(sequence));
                    
                    // Check for duplicates
                    if (seen_sequences.find(sequence) != seen_sequences.end()) {
                        duplicate_packets++;
                    } else {
                        seen_sequences.insert(sequence);
                    }
                    
                    // Check for out-of-order
                    if (sequence < last_sequence) {
                        out_of_order_packets++;
                    }
                    
                    // Update expected sequence
                    if (sequence > expected_sequence) {
                        expected_sequence = sequence;
                    }
                    
                    last_sequence = sequence;
                }
            } else if (size == -1) {
                int err = zmq_errno();
                if (err != EAGAIN) {
                    receive_errors++;
                }
            }
        }
        
        // Small sleep to prevent busy waiting
        if (size1 <= 0 && size2 <= 0) {
            std::this_thread::sleep_for(std::chrono::microseconds(10));
        }
    }
    
    stats.join();
    
    uint64_t total_received = packets_received.load();
    uint64_t total_missing = expected_sequence - total_received;
    double loss_rate = (double)total_missing / expected_sequence * 100.0;
    
    std::cout << "\nFinal Stats:" << std::endl;
    std::cout << "Total received: " << total_received << std::endl;
    std::cout << "Expected packets: " << expected_sequence << std::endl;
    std::cout << "Missing packets: " << total_missing << std::endl;
    std::cout << "Loss rate: " << loss_rate << "%" << std::endl;
    std::cout << "Duplicate packets: " << duplicate_packets.load() << std::endl;
    std::cout << "Out-of-order packets: " << out_of_order_packets.load() << std::endl;
    std::cout << "Receive errors: " << receive_errors.load() << std::endl;
    
    zmq_close(sub1);
    zmq_close(sub2);
    zmq_ctx_destroy(context);
    
    return 0;
}