CXX = g++
CXXFLAGS = -std=c++17 -O3 -Wall -Wextra -pthread -DSPDLOG_COMPILED_LIB
LDFLAGS = -pthread -lspdlog -lfmt

# Source files
LOGGER_SOURCES = main.cpp packet_types.cpp sequence_tracker.cpp network_handler.cpp binary_logger.cpp packet_processor.cpp
READER_SRC = binary_log_reader.cpp
TEST_SOURCES = test_components.cpp packet_types.cpp sequence_tracker.cpp binary_logger.cpp packet_processor.cpp

# Header files (for dependency tracking)
HEADERS = packet_types.h sequence_tracker.h network_handler.h binary_logger.h packet_processor.h

# Object files
LOGGER_OBJECTS = $(LOGGER_SOURCES:.cpp=.o)
TEST_OBJECTS = $(TEST_SOURCES:.cpp=.o)

# Executables
LOGGER_BIN = packet_logger
ZMQ_LOGGER_BIN = packet_logger_zmq
ZMQ_BRIDGE_BIN = zmq_bridge
ZMQ_PUB_TEST = zmq_publisher_test
ZMQ_SUB_TEST = zmq_subscriber_test
ZMQ_MULTI_PUB = zmq_multi_publisher
ZMQ_MULTI_SUB = zmq_multi_subscriber
READER_BIN = log_reader
TEST_BIN = test_components

.PHONY: all clean install test size-check compare-sizes

all: $(LOGGER_BIN) $(READER_BIN) $(ZMQ_LOGGER_BIN) $(ZMQ_BRIDGE_BIN) $(ZMQ_PUB_TEST) $(ZMQ_SUB_TEST) $(ZMQ_MULTI_PUB) $(ZMQ_MULTI_SUB)

# Main packet logger
$(LOGGER_BIN): $(LOGGER_OBJECTS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

# Log reader utility
$(READER_BIN): $(READER_SRC)
	$(CXX) $(CXXFLAGS) -o $@ $< $(LDFLAGS)

# ZMQ packet logger (high performance)
$(ZMQ_LOGGER_BIN): main_zmq.cpp zmq_network_handler.o packet_types.o packet_processor.o binary_logger.o
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS) -lzmq

# ZMQ bridge (UDP multicast to ZMQ)
$(ZMQ_BRIDGE_BIN): zmq_bridge.cpp packet_types.o
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS) -lzmq

# ZMQ test publisher
$(ZMQ_PUB_TEST): zmq_publisher_test.cpp
	$(CXX) $(CXXFLAGS) -o $@ $< $(LDFLAGS) -lzmq

# ZMQ test subscriber
$(ZMQ_SUB_TEST): zmq_subscriber_test.cpp
	$(CXX) $(CXXFLAGS) -o $@ $< $(LDFLAGS) -lzmq

# Multi-threaded ZMQ publisher
$(ZMQ_MULTI_PUB): zmq_multi_publisher.cpp
	$(CXX) $(CXXFLAGS) -o $@ $< $(LDFLAGS) -lzmq

# Multi-threaded ZMQ subscriber
$(ZMQ_MULTI_SUB): zmq_multi_subscriber.cpp
	$(CXX) $(CXXFLAGS) -o $@ $< $(LDFLAGS) -lzmq

# Component test program
$(TEST_BIN): $(TEST_OBJECTS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

# Object file compilation with header dependencies
%.o: %.cpp $(HEADERS)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

# ZMQ network handler object
zmq_network_handler.o: zmq_network_handler.cpp zmq_network_handler.h
	$(CXX) $(CXXFLAGS) -c -o $@ $<

# Clean build artifacts
clean:
	rm -f $(LOGGER_BIN) $(READER_BIN) $(TEST_BIN) $(ZMQ_LOGGER_BIN) $(ZMQ_BRIDGE_BIN) $(ZMQ_PUB_TEST) $(ZMQ_SUB_TEST) $(ZMQ_MULTI_PUB) $(ZMQ_MULTI_SUB)
	rm -f *.o
	rm -f packets_binary.log*
	rm -f *.bin
	rm -f core

# Install to system
install: all
	sudo cp $(LOGGER_BIN) /usr/local/bin/
	sudo cp $(READER_BIN) /usr/local/bin/

# Development targets
debug: CXXFLAGS += -g -DDEBUG -O0
debug: clean all

profile: CXXFLAGS += -pg
profile: clean all

# Dependency checking
deps:
	@echo "Checking for required dependencies..."
	@pkg-config --exists spdlog || (echo "ERROR: spdlog not found. Install with: sudo apt-get install libspdlog-dev" && exit 1)
	@pkg-config --exists fmt || (echo "ERROR: fmt not found. Install with: sudo apt-get install libfmt-dev" && exit 1)
	@echo "All dependencies found."

# Component testing
test-components: $(TEST_BIN)
	@echo "Running component integration tests..."
	./$(TEST_BIN)

# Quick test target
test: $(READER_BIN)
	@echo "Testing log reader with --help option:"
	./$(READER_BIN) --help

# Comprehensive test suite
test-all: test-components test
	@echo "All tests completed successfully!"

# Show binary log file sizes
size-check:
	@echo "Binary log files:"
	@ls -lh packets_binary.log* 2>/dev/null || echo "No binary log files found"
	@echo ""
	@echo "Total size of binary logs:"
	@du -sh packets_binary.log* 2>/dev/null || echo "No binary log files found"

# Compare file sizes (if original text logs exist)
compare-sizes:
	@echo "File size comparison:"
	@echo "Text logs:"
	@ls -lh packets.log* 2>/dev/null || echo "No text log files found"
	@echo "Binary logs:"
	@ls -lh packets_binary.log* 2>/dev/null || echo "No binary log files found"

# Simple run target
run: $(LOGGER_BIN)
	@echo "Starting packet logger..."
	./$(LOGGER_BIN)

# Run with performance monitoring
run-monitored: $(LOGGER_BIN)
	@echo "Starting packet logger with performance monitoring..."
	@echo "Use 'top -p \$$\$$' in another terminal to monitor performance"
	./$(LOGGER_BIN)

# Development helper - format code (requires clang-format)
format:
	@which clang-format >/dev/null 2>&1 || (echo "clang-format not found, skipping..." && exit 0)
	clang-format -i *.cpp *.h

# Static analysis (requires cppcheck)
analyze:
	@which cppcheck >/dev/null 2>&1 || (echo "cppcheck not found, skipping..." && exit 0)
	cppcheck --enable=all --std=c++17 *.cpp *.h

# Memory leak check (requires valgrind)
memcheck: $(TEST_BIN)
	@which valgrind >/dev/null 2>&1 || (echo "valgrind not found, skipping..." && exit 0)
	valgrind --leak-check=full --show-leak-kinds=all ./$(TEST_BIN)

# Performance profiling
perf-test: $(LOGGER_BIN)
	@echo "Building performance test version..."
	@$(MAKE) profile
	@echo "Run './packet_logger' and then 'gprof packet_logger gmon.out > profile.txt'"

# Help target
help:
	@echo "Available targets:"
	@echo "  all            - Build packet logger and log reader"
	@echo "  clean          - Remove build artifacts and log files"
	@echo "  debug          - Build with debug symbols and no optimization"
	@echo "  profile        - Build with profiling enabled"
	@echo "  deps           - Check for required dependencies"
	@echo "  install        - Install binaries to /usr/local/bin"
	@echo "  test           - Test the log reader utility"
	@echo "  test-components- Run component integration tests"
	@echo "  test-all       - Run all tests"
	@echo "  size-check     - Show sizes of generated log files"
	@echo "  compare-sizes  - Compare text vs binary log file sizes"
	@echo "  run-monitored  - Run with performance monitoring hints"
	@echo "  format         - Format code with clang-format"
	@echo "  analyze        - Run static analysis with cppcheck"
	@echo "  memcheck       - Run memory leak detection with valgrind"
	@echo "  perf-test      - Build for performance profiling"
	@echo "  help           - Show this help message"