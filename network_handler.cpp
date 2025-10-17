#include "network_handler.h"
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <poll.h>
#include <cstring>
#include <stdexcept>
#include <iostream>
#include <cerrno>

NetworkHandler::NetworkHandler() : sock1_(-1), sock2_(-1), capturing_(false) {
    sock1_ = create_multicast_socket(Config::PORT1);
    sock2_ = create_multicast_socket(Config::PORT2);
}

NetworkHandler::~NetworkHandler() {
    stop_capture();
    if (sock1_ >= 0) close(sock1_);
    if (sock2_ >= 0) close(sock2_);
}

int NetworkHandler::create_multicast_socket(uint16_t port) {
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        throw std::runtime_error("Failed to create socket: " + std::string(strerror(errno)));
    }

    int reuse = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
        std::cerr << "Warning: Failed to set SO_REUSEADDR: " << strerror(errno) << std::endl;
    }

    // CRITICAL: Increase socket buffer for 14M packets
    int rcvbuf = 64 * 1024 * 1024;  // 64MB socket buffer
    if (setsockopt(sock, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf)) < 0) {
        std::cerr << "Warning: Failed to set SO_RCVBUF: " << strerror(errno) << std::endl;
    }

    int pktinfo = 1;
    if (setsockopt(sock, IPPROTO_IP, IP_PKTINFO, &pktinfo, sizeof(pktinfo)) < 0) {
        std::cerr << "Warning: Failed to enable IP_PKTINFO: " << strerror(errno) << std::endl;
    }

    sockaddr_in local_addr{};
    local_addr.sin_family = AF_INET;
    local_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    local_addr.sin_port = htons(port);

    if (bind(sock, reinterpret_cast<struct sockaddr*>(&local_addr), sizeof(local_addr)) < 0) {
        close(sock);
        throw std::runtime_error("Failed to bind to port " + std::to_string(port) + ": " + strerror(errno));
    }

    ip_mreq mreq{};
    mreq.imr_multiaddr.s_addr = inet_addr(Config::MULTICAST_IP);
    mreq.imr_interface.s_addr = htonl(INADDR_ANY);
    if (setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) < 0) {
        close(sock);
        throw std::runtime_error("Failed to join multicast group: " + std::string(strerror(errno)));
    }

    return sock;
}

void NetworkHandler::start_capture(PacketCallback callback) {
    capturing_ = true;

    struct pollfd fds[2];
    fds[0].fd = sock1_;
    fds[0].events = POLLIN;
    fds[1].fd = sock2_;
    fds[1].events = POLLIN;

    int packet_id = 0;
    char buffer[Config::MAX_BUF];
    char control_buffer[1024];

    while (capturing_) {
        // Use 100ms timeout for better CPU efficiency while maintaining responsiveness
        int ready = poll(fds, 2, 100);

        if (ready < 0) {
            // Handle poll errors
            if (errno == EINTR) {
                // Interrupted by signal, continue
                continue;
            }
            std::cerr << "Poll error: " << strerror(errno) << std::endl;
            break;
        }

        if (ready == 0) {
            // Timeout, no data available
            continue;
        }

        for (int i = 0; i < 2; ++i) {
            if (fds[i].revents & POLLIN) {
                sockaddr_in sender_addr{};
                socklen_t addr_len = sizeof(sender_addr);

                struct msghdr msg{};
                struct iovec iov{};

                iov.iov_base = buffer;
                iov.iov_len = Config::MAX_BUF;

                msg.msg_name = &sender_addr;
                msg.msg_namelen = addr_len;
                msg.msg_iov = &iov;
                msg.msg_iovlen = 1;
                msg.msg_control = control_buffer;
                msg.msg_controllen = sizeof(control_buffer);

                ssize_t len = recvmsg(fds[i].fd, &msg, 0);

                if (len > 0) {
                    packet_id++;
                    int port = (fds[i].fd == sock1_) ? Config::PORT1 : Config::PORT2;
                    std::string src_ip = inet_ntoa(sender_addr.sin_addr);

                    // Call the provided callback function
                    callback(packet_id, port, buffer, static_cast<int>(len), src_ip);
                } else if (len < 0) {
                    // Handle receive errors
                    if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
                        // Non-fatal errors, continue
                        continue;
                    }
                    std::cerr << "recvmsg error on port "
                              << ((fds[i].fd == sock1_) ? Config::PORT1 : Config::PORT2)
                              << ": " << strerror(errno) << std::endl;
                }
            }

            // Check for error events
            if (fds[i].revents & (POLLERR | POLLHUP | POLLNVAL)) {
                std::cerr << "Socket error on port "
                          << ((fds[i].fd == sock1_) ? Config::PORT1 : Config::PORT2)
                          << std::endl;
                capturing_ = false;
                break;
            }
        }
    }
}

void NetworkHandler::stop_capture() {
    capturing_ = false;
}