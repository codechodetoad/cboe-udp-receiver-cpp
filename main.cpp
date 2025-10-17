#include "network_handler.h"
#include "packet_processor.h"
#include "packet_types.h"
#include <iostream>
#include <csignal>
#include <memory>
#include <iomanip>

// Global instances for signal handling
std::unique_ptr<NetworkHandler> g_network_handler;
std::unique_ptr<PacketProcessor> g_packet_processor;

/**
 * Signal handler for graceful shutdown
 * Note: Signal handler only stops capture; cleanup happens in main()
 */
void signal_handler(int signal) {
    std::cout << "\nReceived signal " << signal << ", initiating graceful shutdown..." << std::endl;

    if (g_network_handler) {
        g_network_handler->stop_capture();
    }
}

/**
 * Print startup banner and configuration
 */
void print_startup_info() {
    std::cout << "========================================" << std::endl;
    std::cout << "ULTRA HIGH-VOLUME CBOE PITCH Binary Logger" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Target capacity: 14+ million packets" << std::endl;
    std::cout << "Multicast group: " << Config::MULTICAST_IP << std::endl;
    std::cout << "Monitoring ports: " << Config::PORT1 << ", " << Config::PORT2 << std::endl;
    std::cout << "Binary record size: " << sizeof(BinaryLogRecord) << " bytes + payload" << std::endl;
    std::cout << std::endl;
    
    std::cout << "Performance Configuration:" << std::endl;
    std::cout << "  Log file size: " << (Config::LOG_FILE_SIZE / (1024*1024)) << "MB per file" << std::endl;
    std::cout << "  Log file count: " << Config::LOG_FILE_COUNT << " files" << std::endl;
    std::cout << "  Total log capacity: " << ((Config::LOG_FILE_SIZE / (1024*1024)) * Config::LOG_FILE_COUNT / 1024) << "GB" << std::endl;
    std::cout << "  Async queue size: " << (Config::ASYNC_QUEUE_SIZE / 1024) << "K entries" << std::endl;
    std::cout << "  Background threads: " << Config::ASYNC_THREADS << std::endl;
    std::cout << "  Socket buffer: 64MB per socket" << std::endl;
    std::cout << "  Heartbeat filtering: " << (Config::SKIP_HEARTBEATS ? "ENABLED" : "DISABLED") << std::endl;
    std::cout << std::endl;
    
    std::cout << "Performance Reporting:" << std::endl;
    std::cout << "  Statistics interval: Every " << (Config::STATS_INTERVAL / 1000) << "K packets" << std::endl;
    std::cout << "  Flush interval: Every " << (Config::FLUSH_INTERVAL / 1000000) << "M packets" << std::endl;
    std::cout << std::endl;
    
    std::cout << "Press Ctrl+C to stop capture and view final statistics" << std::endl;
    std::cout << "========================================" << std::endl;
}

/**
 * Main application entry point
 */
int main(int argc, char* argv[]) {
    try {
        // Print startup information
        print_startup_info();
        
        // Install signal handlers for graceful shutdown
        signal(SIGINT, signal_handler);
        signal(SIGTERM, signal_handler);
        
        // Create main components
        g_packet_processor = std::make_unique<PacketProcessor>();
        g_network_handler = std::make_unique<NetworkHandler>();
        
        std::cout << "Initialization complete. Starting packet capture..." << std::endl;
        std::cout << "Waiting for packets..." << std::endl;
        
        // Define packet processing callback
        auto packet_callback = [](int packet_id, int port, const char* buffer, int len, const std::string& src_ip) {
            g_packet_processor->process_packet(packet_id, port, buffer, len, src_ip);
        };
        
        // Start the main capture loop (blocks until stop_capture() is called)
        g_network_handler->start_capture(packet_callback);

        // Capture stopped (either by signal or error), perform cleanup
        std::cout << "\nPacket capture stopped. Performing cleanup..." << std::endl;

        if (g_packet_processor) {
            std::cout << "Flushing remaining log data..." << std::endl;
            g_packet_processor->flush_logs();

            std::cout << "\nFinal performance report:" << std::endl;
            g_packet_processor->print_performance_report();
        }

        // Explicitly reset unique_ptrs to ensure proper cleanup order
        g_network_handler.reset();
        g_packet_processor.reset();

        std::cout << "\nShutdown complete." << std::endl;

    } catch (const std::exception& e) {
        std::cerr << "FATAL ERROR: " << e.what() << std::endl;

        // Attempt to flush any pending data
        if (g_packet_processor) {
            try {
                g_packet_processor->flush_logs();
            } catch (...) {
                std::cerr << "Failed to flush logs during error cleanup" << std::endl;
            }
        }

        return 1;
    }

    return 0;
}