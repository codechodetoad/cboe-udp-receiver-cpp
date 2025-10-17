#include <zmq.h>
#include <iostream>
#include <chrono>
#include <thread>
#include <random>
#include <atomic>
#include <csignal>
#include <cstring>

std::atomic<bool> running(true);
std::atomic<uint64_t> packets_sent(0);
std::atomic<uint64_t> send_errors(0);

void signal_handler(int signal) {
    std::cout << "\nReceived signal " << signal << ", stopping publisher..." << std::endl;
    running = false;
}

void stats_thread() {
    auto last_time = std::chrono::steady_clock::now();
    uint64_t last_count = 0;
    
    while (running) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        
        auto now = std::chrono::steady_clock::now();
        uint64_t current_count = packets_sent.load();
        
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_time).count();
        uint64_t packets_this_second = current_count - last_count;
        
        if (duration >= 1000) {
            double rate = (double)packets_this_second * 1000.0 / duration;
            std::cout << "Rate: " << (int)rate << " pps | Total: " << current_count 
                     << " | Errors: " << send_errors.load() << std::endl;
            
            last_time = now;
            last_count = current_count;
        }
    }
}

int main() {
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    void* context = zmq_ctx_new();
    void* pub1 = zmq_socket(context, ZMQ_PUB);
    void* pub2 = zmq_socket(context, ZMQ_PUB);
    
    int hwm = 100000;
    zmq_setsockopt(pub1, ZMQ_SNDHWM, &hwm, sizeof(hwm));
    zmq_setsockopt(pub2, ZMQ_SNDHWM, &hwm, sizeof(hwm));
    
    zmq_bind(pub1, "ipc:///tmp/cboe_port1.ipc");
    zmq_bind(pub2, "ipc:///tmp/cboe_port2.ipc");
    
    std::cout << "ZMQ Test Publisher started" << std::endl;
    std::cout << "Publishing to: ipc:///tmp/cboe_port1.ipc, ipc:///tmp/cboe_port2.ipc" << std::endl;
    std::cout << "Target rate: 100k packets/second" << std::endl;
    
    // Start stats thread
    std::thread stats(stats_thread);
    
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> size_dist(40, 200);
    std::uniform_int_distribution<> data_dist(0, 255);
    
    auto start_time = std::chrono::steady_clock::now();
    
    while (running) {
        // Generate packet
        int packet_size = size_dist(gen);
        char packet_data[256];
        
        // CBOE-like header
        uint16_t length = packet_size;
        uint8_t count = 1 + (packets_sent % 5);
        uint8_t unit = 1;
        uint32_t sequence = packets_sent.load();
        
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
            packets_sent++;
        } else {
            send_errors++;
        }
        
        // Rate limiting for 100k pps
        std::this_thread::sleep_for(std::chrono::nanoseconds(10000)); // 10μs = 100k pps
    }
    
    stats.join();
    
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(end_time - start_time).count();
    
    std::cout << "\nFinal Stats:" << std::endl;
    std::cout << "Total packets: " << packets_sent.load() << std::endl;
    std::cout << "Send errors: " << send_errors.load() << std::endl;
    std::cout << "Duration: " << duration << " seconds" << std::endl;
    std::cout << "Average rate: " << (packets_sent.load() / duration) << " pps" << std::endl;
    
    zmq_close(pub1);
    zmq_close(pub2);
    zmq_ctx_destroy(context);
    
    return 0;
}