#include "packet_types.h"
#include <iostream>
#include <csignal>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <zmq.h>

volatile bool running = true;

void signal_handler(int signal) {
    std::cout << "\nReceived signal " << signal << ", stopping bridge..." << std::endl;
    running = false;
}

int create_multicast_socket(uint16_t port) {
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) return -1;
    
    int reuse = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    
    int bufsize = 64 * 1024 * 1024;
    setsockopt(sock, SOL_SOCKET, SO_RCVBUF, &bufsize, sizeof(bufsize));
    
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);
    
    if (bind(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(sock);
        return -1;
    }
    
    struct ip_mreq mreq;
    mreq.imr_multiaddr.s_addr = inet_addr(Config::MULTICAST_IP);
    mreq.imr_interface.s_addr = INADDR_ANY;
    
    if (setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) < 0) {
        close(sock);
        return -1;
    }
    
    return sock;
}

int main() {
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    std::cout << "========================================" << std::endl;
    std::cout << "CBOE UDP to ZMQ Bridge" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "UDP Input: " << Config::MULTICAST_IP << ":" << Config::PORT1 << "," << Config::PORT2 << std::endl;
    std::cout << "ZMQ Output: ipc:///tmp/cboe_port1.ipc, ipc:///tmp/cboe_port2.ipc" << std::endl;
    std::cout << "Press Ctrl+C to stop" << std::endl;
    std::cout << "========================================" << std::endl;
    
    // Create UDP sockets
    int udp_sock1 = create_multicast_socket(Config::PORT1);
    int udp_sock2 = create_multicast_socket(Config::PORT2);
    
    if (udp_sock1 < 0 || udp_sock2 < 0) {
        std::cerr << "Failed to create UDP sockets" << std::endl;
        return 1;
    }
    
    // Create ZMQ context and PUSH sockets for reliable delivery
    void* context = zmq_ctx_new();
    void* push1 = zmq_socket(context, ZMQ_PUSH);
    void* push2 = zmq_socket(context, ZMQ_PUSH);
    
    int hwm = 1000000;
    zmq_setsockopt(push1, ZMQ_SNDHWM, &hwm, sizeof(hwm));
    zmq_setsockopt(push2, ZMQ_SNDHWM, &hwm, sizeof(hwm));
    
    zmq_bind(push1, "ipc:///tmp/cboe_port1.ipc");
    zmq_bind(push2, "ipc:///tmp/cboe_port2.ipc");
    
    std::cout << "PUSH/PULL Bridge started - forwarding UDP to ZMQ" << std::endl;
    
    char buffer[2048];
    struct sockaddr_in sender_addr;
    socklen_t sender_len = sizeof(sender_addr);
    fd_set read_fds;
    struct timeval timeout;
    uint64_t packets_forwarded = 0;
    
    while (running) {
        FD_ZERO(&read_fds);
        FD_SET(udp_sock1, &read_fds);
        FD_SET(udp_sock2, &read_fds);
        
        timeout.tv_sec = 0;
        timeout.tv_usec = 1000; // 1ms timeout
        
        int max_fd = std::max(udp_sock1, udp_sock2) + 1;
        int activity = select(max_fd, &read_fds, nullptr, nullptr, &timeout);
        
        if (activity < 0) break;
        if (activity == 0) continue;
        
        // Forward from port 30501
        if (FD_ISSET(udp_sock1, &read_fds)) {
            ssize_t len = recvfrom(udp_sock1, buffer, sizeof(buffer), MSG_DONTWAIT,
                                 (struct sockaddr*)&sender_addr, &sender_len);
            if (len > 0) {
                zmq_send(push1, buffer, len, ZMQ_DONTWAIT);
                packets_forwarded++;
            }
        }
        
        // Forward from port 30502
        if (FD_ISSET(udp_sock2, &read_fds)) {
            ssize_t len = recvfrom(udp_sock2, buffer, sizeof(buffer), MSG_DONTWAIT,
                                 (struct sockaddr*)&sender_addr, &sender_len);
            if (len > 0) {
                zmq_send(push2, buffer, len, ZMQ_DONTWAIT);
                packets_forwarded++;
            }
        }
        
        // Status every 100K packets
        if (packets_forwarded % 100000 == 0 && packets_forwarded > 0) {
            std::cout << "Forwarded " << packets_forwarded << " packets" << std::endl;
        }
    }
    
    std::cout << "Bridge stopped. Total packets forwarded: " << packets_forwarded << std::endl;
    
    // Cleanup
    close(udp_sock1);
    close(udp_sock2);
    zmq_close(push1);
    zmq_close(push2);
    zmq_ctx_destroy(context);
    
    return 0;
}