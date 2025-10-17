# CBOE UDP Receiver - High-Performance Packet Logger

A high-performance C++ application for capturing, processing, and logging CBOE (Chicago Board Options Exchange) PITCH protocol UDP multicast packets. Designed to handle ultra-high-volume market data streams with minimal latency.

## Features

- **Ultra-High Throughput**: Designed to handle 14+ million packets with 50K+ packets/second
- **Binary Logging**: Compact binary format for efficient storage and fast I/O
- **Asynchronous I/O**: Multi-threaded async logging using spdlog infrastructure
- **Sequence Tracking**: Full CBOE PITCH sequence validation and out-of-order detection
- **Packet Classification**: Automatic classification of heartbeats, admin, data, and unsequenced packets
- **ZeroMQ Support**: Optional ZMQ-based architecture for distributed processing
- **Performance Monitoring**: Real-time statistics and performance reporting
- **Memory Safety**: Modern C++17 with RAII and smart pointers

## Architecture Overview

The application consists of several modular components:

- **NetworkHandler**: UDP multicast socket management and packet capture
- **PacketProcessor**: CBOE PITCH packet parsing and classification
- **BinaryLogger**: High-performance async binary logging
- **SequenceManager**: Packet sequence tracking and gap detection
- **ZMQ Bridge** (optional): UDP-to-ZeroMQ bridge for distributed systems

For a detailed architecture overview, see [PROJECT_OVERVIEW.md](PROJECT_OVERVIEW.md).

## Prerequisites

### Required Dependencies

```bash
# Ubuntu/Debian
sudo apt-get update
sudo apt-get install -y \
    build-essential \
    g++ \
    make \
    libspdlog-dev \
    libfmt-dev

# Optional: ZeroMQ support
sudo apt-get install -y libzmq3-dev

# Optional: Development tools
sudo apt-get install -y \
    clang-format \
    cppcheck \
    valgrind
```

### System Requirements

- **CPU**: Multi-core processor (4+ cores recommended for high volume)
- **RAM**: 4GB minimum, 8GB+ recommended
- **Network**: Support for multicast UDP
- **OS**: Linux (tested on Ubuntu 20.04+, WSL2)
- **Compiler**: GCC 7+ or Clang 6+ with C++17 support

## Building

### Quick Start

```bash
# Clone the repository
git clone <repository-url>
cd cboe-udp-receiver-cpp

# Check dependencies
make deps

# Build all components
make all

# Build specific target
make packet_logger      # Main UDP logger
make log_reader         # Binary log reader
```

### Build Targets

| Target | Description |
|--------|-------------|
| `all` | Build all components (default) |
| `packet_logger` | Main UDP multicast packet logger |
| `packet_logger_zmq` | ZMQ-enabled packet logger |
| `log_reader` | Binary log file reader/analyzer |
| `zmq_bridge` | UDP to ZMQ bridge |
| `clean` | Remove all build artifacts |
| `debug` | Build with debug symbols |
| `profile` | Build with profiling enabled |

### Development Builds

```bash
# Debug build (with symbols, no optimization)
make debug

# Profile build (for performance analysis)
make profile

# Check dependencies
make deps
```

## Usage

### Running the Packet Logger

```bash
# Basic usage
./packet_logger

# Run with performance monitoring
make run-monitored

# Monitor in another terminal
top -p $(pgrep packet_logger)
```

### Configuration

Configuration is set in [packet_types.h](packet_types.h):

```cpp
namespace Config {
    constexpr const char* MULTICAST_IP = "233.218.133.80";
    constexpr uint16_t PORT1 = 30501;
    constexpr uint16_t PORT2 = 30502;
    constexpr bool SKIP_HEARTBEATS = true;
    constexpr size_t LOG_FILE_SIZE = 500 * 1024 * 1024;  // 500MB
    constexpr int LOG_FILE_COUNT = 50;                    // 50 files
    constexpr size_t ASYNC_QUEUE_SIZE = 1024 * 1024;     // 1M entries
    constexpr int ASYNC_THREADS = 4;                      // 4 threads
}
```

### Reading Binary Logs

```bash
# Display help
./log_reader --help

# Read and analyze binary logs
./log_reader packets_binary.log.0

# Check log file sizes
make size-check
```

## Performance

### Optimizations

- **64MB socket buffers** per port to prevent packet drops
- **Asynchronous logging** with 1M entry queue and 4 background threads
- **Zero-copy operations** where possible
- **Heartbeat filtering** to reduce unnecessary I/O
- **Binary format** reduces storage by ~70% vs text logs
- **Optimized poll timeout** (100ms) for CPU efficiency

### Benchmarks

Typical performance on modern hardware:
- **Throughput**: 50,000+ packets/second sustained
- **Latency**: Sub-millisecond packet processing
- **Storage**: ~30 bytes/packet (header) + payload
- **Capacity**: 25GB total (50 x 500MB rotating files)

## Testing

```bash
# Run all tests
make test-all

# Component integration tests
make test-components

# Log reader test
make test

# Memory leak detection
make memcheck

# Static analysis
make analyze

# Code formatting
make format
```

## ZeroMQ Architecture

The project includes optional ZeroMQ support for distributed processing:

```bash
# Build ZMQ components
make zmq_bridge zmq_publisher_test zmq_subscriber_test

# Run UDP to ZMQ bridge
./zmq_bridge

# Test publisher
./zmq_publisher_test

# Test subscriber
./zmq_subscriber_test
```

## File Structure

```
cboe-udp-receiver-cpp/
├── main.cpp                    # Main application entry point
├── network_handler.{h,cpp}     # UDP multicast socket handling
├── packet_processor.{h,cpp}    # Packet parsing and classification
├── packet_types.{h,cpp}        # CBOE PITCH data structures
├── sequence_tracker.{h,cpp}    # Sequence validation
├── binary_logger.{h,cpp}       # Async binary logging
├── binary_log_reader.cpp       # Log file reader utility
├── zmq_network_handler.{h,cpp} # ZMQ network handling
├── zmq_bridge.cpp              # UDP to ZMQ bridge
├── Makefile                    # Build configuration
└── README.md                   # This file
```

## Troubleshooting

### Common Issues

**Problem**: `bind: Address already in use`
```bash
# Check if port is in use
sudo netstat -tulpn | grep 30501

# Kill existing process
sudo kill $(sudo lsof -t -i:30501)
```

**Problem**: `No multicast packets received`
```bash
# Check multicast route
ip route show

# Add multicast route if needed
sudo route add -net 224.0.0.0 netmask 240.0.0.0 dev eth0

# Verify multicast group membership
netstat -g
```

**Problem**: Performance below 50K packets/second
```bash
# Increase socket buffer sizes (system-wide)
sudo sysctl -w net.core.rmem_max=134217728
sudo sysctl -w net.core.rmem_default=134217728

# Check for packet drops
netstat -su | grep -i drop
```

**Problem**: Binary logs growing too large
```bash
# Reduce log file count or size in packet_types.h
constexpr size_t LOG_FILE_SIZE = 200 * 1024 * 1024;  // 200MB
constexpr int LOG_FILE_COUNT = 20;                    // 20 files

# Rebuild
make clean && make
```

## Network Configuration

### Multicast Setup

```bash
# Enable multicast routing (if needed)
sudo sysctl -w net.ipv4.conf.all.mc_forwarding=1

# Add multicast route
sudo route add -net 224.0.0.0 netmask 240.0.0.0 dev eth0

# Verify
ip maddr show
```

### Firewall Configuration

```bash
# Allow multicast traffic (iptables)
sudo iptables -A INPUT -p udp -d 233.218.133.80 --dport 30501:30502 -j ACCEPT

# Allow multicast traffic (ufw)
sudo ufw allow proto udp from any to 233.218.133.80 port 30501:30502
```

## Contributing

### Code Style

- C++17 standard
- Use `clang-format` for formatting: `make format`
- Follow RAII principles
- Prefer smart pointers over raw pointers
- Add comments for complex logic

### Pull Requests

1. Fork the repository
2. Create a feature branch
3. Make your changes
4. Run tests: `make test-all`
5. Run static analysis: `make analyze`
6. Submit a pull request

## Performance Tuning

### System Tuning

```bash
# Increase UDP buffer sizes
sudo sysctl -w net.core.rmem_max=134217728
sudo sysctl -w net.core.rmem_default=134217728
sudo sysctl -w net.core.netdev_max_backlog=5000

# Make permanent
echo "net.core.rmem_max=134217728" | sudo tee -a /etc/sysctl.conf
echo "net.core.rmem_default=134217728" | sudo tee -a /etc/sysctl.conf
```

### Application Tuning

Edit [packet_types.h](packet_types.h):

```cpp
// For higher throughput
constexpr size_t ASYNC_QUEUE_SIZE = 2048 * 1024;  // 2M queue
constexpr int ASYNC_THREADS = 8;                   // 8 threads

// For lower memory usage
constexpr size_t ASYNC_QUEUE_SIZE = 512 * 1024;   // 512K queue
constexpr int ASYNC_THREADS = 2;                   // 2 threads
```

## License

[Specify your license here]

## Acknowledgments

- CBOE PITCH Protocol Specification
- spdlog - Fast C++ logging library
- fmt - Modern formatting library
- ZeroMQ - High-performance async messaging

## Contact

[Your contact information]

## See Also

- [PROJECT_OVERVIEW.md](PROJECT_OVERVIEW.md) - Detailed technical overview
- [CBOE PITCH Specification](https://www.cboe.com/us/options/connectivity/) - Official protocol documentation
