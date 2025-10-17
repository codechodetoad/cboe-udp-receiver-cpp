#pragma once

#include "packet_types.h"
#include <string>
#include <functional>
#include <atomic>

/**
 * Callback function type for packet handling
 * Parameters: packet_id, port, buffer, length, source_ip
 */
using PacketCallback = std::function<void(int, int, const char*, int, const std::string&)>;

/**
 * Handles multicast socket creation and packet reception
 */
class NetworkHandler {
public:
    /**
     * Constructor - creates and configures multicast sockets
     */
    NetworkHandler();
    
    /**
     * Destructor - cleans up sockets
     */
    ~NetworkHandler();
    
    /**
     * Start packet capture loop
     * @param callback Function to call for each received packet
     */
    void start_capture(PacketCallback callback);
    
    /**
     * Stop packet capture (can be called from signal handler)
     */
    void stop_capture();
    
    /**
     * Check if currently capturing
     */
    bool is_capturing() const { return capturing_; }

private:
    int sock1_;
    int sock2_;
    std::atomic<bool> capturing_;
    
    /**
     * Create and configure a multicast socket for the given port
     */
    int create_multicast_socket(uint16_t port);
};