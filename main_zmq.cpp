#include "zmq_network_handler.h"
#include "packet_processor.h"
#include "packet_types.h"
#include <iostream>
#include <csignal>
#include <memory>
#include <thread>
#include <chrono>

// Global instances for signal handling
std::unique_ptr<ZmqNetworkHandler> g_zmq_handler;
std::unique_ptr<PacketProcessor> g_packet_processor;

void signal_handler(int signal) {
    std::cout << "\nReceived signal " << signal << ", initiating graceful shutdown..." << std::endl;
    
    if (g_zmq_handler) {
        g_zmq_handler->stop_capture();
    }
    
    if (g_packet_processor) {
        std::cout << "Flushing remaining log data..." << std::endl;
        g_packet_processor->flush_logs();
        g_packet_processor->print_performance_report();
    }
    
    std::cout << "Shutdown complete." << std::endl;
    exit(0);
}

void print_zmq_startup_info() {
    std::cout << "========================================" << std::endl;
    std::cout << "CBOE PITCH ZeroMQ Binary Logger" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Transport: ZeroMQ Publisher-Subscriber" << std::endl;
    std::cout << "Endpoints: ipc:///tmp/cboe_port1.ipc, ipc:///tmp/cboe_port2.ipc" << std::endl;
    std::cout << "Target rate: 100,000 packets/second" << std::endl;
    std::cout << "Binary record size: " << sizeof(BinaryLogRecord) << " bytes + payload" << std::endl;
    std::cout << std::endl;
    
    std::cout << "Performance Configuration:" << std::endl;
    std::cout << "  Log file size: " << (Config::LOG_FILE_SIZE / (1024*1024)) << "MB per file" << std::endl;
    std::cout << "  Log file count: " << Config::LOG_FILE_COUNT << " files" << std::endl;
    std::cout << "  ZMQ High Water Mark: 1M messages" << std::endl;
    std::cout << "  Receive timeout: 100ms" << std::endl;
    std::cout << std::endl;
    
    std::cout << "Waiting for ZMQ publisher (CBOE pcap replayer)..." << std::endl;
    std::cout << "Press Ctrl+C to stop capture and view final statistics" << std::endl;
    std::cout << "========================================" << std::endl;
}

int main(int argc, char* argv[]) {
    try {
        print_zmq_startup_info();
        
        // Install signal handlers
        signal(SIGINT, signal_handler);
        signal(SIGTERM, signal_handler);
        
        // Create components
        g_packet_processor = std::make_unique<PacketProcessor>();
        g_zmq_handler = std::make_unique<ZmqNetworkHandler>();
        
        std::cout << "Initialization complete. Starting ZMQ packet capture..." << std::endl;
        
        // Define packet processing callback
        auto packet_callback = [](int packet_id, int port, const char* buffer, int len, const std::string& src_ip) {
            g_packet_processor->process_packet(packet_id, port, buffer, len, src_ip);
        };
        
        // Start ZMQ capture
        g_zmq_handler->start_capture(packet_callback);
        
        // Keep main thread alive
        while (true) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        
    } catch (const std::exception& e) {
        std::cerr << "FATAL ERROR: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}