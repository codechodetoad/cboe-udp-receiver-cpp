#include "zmq_network_handler.h"
#include "packet_types.h"
#include <iostream>
#include <chrono>
#include <cstring>

ZmqNetworkHandler::ZmqNetworkHandler() : running_(false), context_(nullptr), subscriber_(nullptr), subscriber2_(nullptr) {
}

ZmqNetworkHandler::~ZmqNetworkHandler() {
    stop_capture();
}

void ZmqNetworkHandler::capture_loop() {
    try {
        // Create ZMQ context
        context_ = zmq_ctx_new();
        if (!context_) {
            std::cerr << "Failed to create ZMQ context" << std::endl;
            return;
        }
        
        // Create separate PULL sockets for each port
        subscriber_ = zmq_socket(context_, ZMQ_PULL);
        subscriber2_ = zmq_socket(context_, ZMQ_PULL);
        if (!subscriber_ || !subscriber2_) {
            std::cerr << "Failed to create ZMQ PULL sockets" << std::endl;
            return;
        }
        
        // Optimize both sockets for 1M pps
        int hwm = 10000000;
        zmq_setsockopt(subscriber_, ZMQ_RCVHWM, &hwm, sizeof(hwm));
        zmq_setsockopt(subscriber2_, ZMQ_RCVHWM, &hwm, sizeof(hwm));
        
        int timeout = 0;
        zmq_setsockopt(subscriber_, ZMQ_RCVTIMEO, &timeout, sizeof(timeout));
        zmq_setsockopt(subscriber2_, ZMQ_RCVTIMEO, &timeout, sizeof(timeout));
        
        // Connect each socket to its own endpoint
        zmq_connect(subscriber_, "ipc:///tmp/cboe_port1.ipc");
        zmq_connect(subscriber2_, "ipc:///tmp/cboe_port2.ipc");
        
        std::cout << "ZMQ PULL sockets connected to separate endpoints" << std::endl;
        std::cout << "High water mark: " << hwm << " messages" << std::endl;
        
        char buffer[2048];
        int packet_id = 0;
        
        while (running_) {
            // Poll both sockets
            int size1 = zmq_recv(subscriber_, buffer, sizeof(buffer), ZMQ_DONTWAIT);
            if (size1 > 0 && callback_) {
                callback_(packet_id++, Config::PORT1, buffer, size1, "zmq_push");
            }
            
            int size2 = zmq_recv(subscriber2_, buffer, sizeof(buffer), ZMQ_DONTWAIT);
            if (size2 > 0 && callback_) {
                callback_(packet_id++, Config::PORT2, buffer, size2, "zmq_push");
            }
            
            // Check for errors
            if (size1 == -1 && zmq_errno() != EAGAIN) {
                std::cerr << "ZMQ port1 error: " << zmq_strerror(zmq_errno()) << std::endl;
                break;
            }
            if (size2 == -1 && zmq_errno() != EAGAIN) {
                std::cerr << "ZMQ port2 error: " << zmq_strerror(zmq_errno()) << std::endl;
                break;
            }
        }
        
    } catch (const std::exception& e) {
        std::cerr << "ZMQ Error: " << e.what() << std::endl;
    }
}

void ZmqNetworkHandler::start_capture(PacketCallback callback) {
    callback_ = callback;
    running_ = true;
    
    std::cout << "Starting ZMQ high-performance packet capture..." << std::endl;
    std::cout << "Target rate: 100k packets/second" << std::endl;
    
    // Start capture in separate thread
    capture_thread_ = std::thread(&ZmqNetworkHandler::capture_loop, this);
}

void ZmqNetworkHandler::stop_capture() {
    running_ = false;
    
    if (capture_thread_.joinable()) {
        capture_thread_.join();
    }
    
    if (subscriber_) {
        zmq_close(subscriber_);
        subscriber_ = nullptr;
    }
    
    if (subscriber2_) {
        zmq_close(subscriber2_);
        subscriber2_ = nullptr;
    }
    
    if (context_) {
        zmq_ctx_destroy(context_);
        context_ = nullptr;
    }
    
    std::cout << "ZMQ capture stopped" << std::endl;
}