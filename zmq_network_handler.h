#pragma once
#include <functional>
#include <string>
#include <cstdint>
#include <thread>
#include <atomic>
#include <zmq.h>

using PacketCallback = std::function<void(int packet_id, int port, const char* buffer, int len, const std::string& src_ip)>;

class ZmqNetworkHandler {
public:
    ZmqNetworkHandler();
    ~ZmqNetworkHandler();
    
    void start_capture(PacketCallback callback);
    void stop_capture();
    
private:
    std::atomic<bool> running_;
    PacketCallback callback_;
    void* context_;
    void* subscriber_;
    void* subscriber2_;
    std::thread capture_thread_;
    
    void capture_loop();
};