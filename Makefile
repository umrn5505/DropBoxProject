# Makefile for DropBox Server

# Compiler and flags
CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -pthread -g
LDFLAGS = -pthread -lssl -lcrypto

# Target executable
TARGET = dropbox_server

# Source files
SOURCES = main.c queue_operations.c authentication.c thread_pool.c file_operations.c file_storage.c utilities.c

# Object files (derived from source files)
OBJECTS = $(SOURCES:.c=.o)

# Header files
HEADERS = dropbox_server.h

# Add test client source (standalone)
TEST_CLIENT_SRC = test_client.c

# Default target
all: $(TARGET)

# Link object files to create executable
$(TARGET): $(OBJECTS)
	$(CC) $(OBJECTS) -o $(TARGET) $(LDFLAGS)
	@echo "Build successful! Run with: ./$(TARGET)"

# Compile source files to object files
%.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) -c $< -o $@

# Optional test client build (only if you want the client built on this platform)
.PHONY: test_client
test_client:
	@echo "Note: test_client may be platform-specific. Building if possible..."
	$(CC) $(CFLAGS) $(TEST_CLIENT_SRC) -o test_client || true

# Build concurrency test
tests/concurrency_test: tests/concurrency_test.c
	$(CC) $(CFLAGS) tests/concurrency_test.c -o tests/concurrency_test

# Build full integration test
tests/full_integration_test: tests/full_integration_test.c
	$(CC) $(CFLAGS) tests/full_integration_test.c -o tests/full_integration_test

# Clean build artifacts
clean:
	rm -f $(OBJECTS) $(TARGET) test_client .test_client_stamp tests/concurrency_test tests/full_integration_test
	@echo "Clean completed"

# Clean and rebuild
rebuild: clean all

# Run the server
run: $(TARGET)
	./$(TARGET)

# Debug build with additional flags
debug: CFLAGS += -DDEBUG -fsanitize=thread -fsanitize=address
debug: LDFLAGS += -fsanitize=thread -fsanitize=address
debug: $(TARGET)

# Memory leak check (requires valgrind)
valgrind: $(TARGET)
	valgrind --leak-check=full --track-origins=yes ./$(TARGET)

# Thread sanitizer build
tsan: CFLAGS += -fsanitize=thread -g
tsan: LDFLAGS += -fsanitize=thread
tsan: $(TARGET)

# Install dependencies (on Ubuntu/Debian)
install-deps:
	sudo apt-get update
	sudo apt-get install build-essential valgrind

# Show help
help:
	@echo "Available targets:"
	@echo "  all       - Build the server (default)"
	@echo "  clean     - Remove build artifacts"
	@echo "  rebuild   - Clean and rebuild"
	@echo "  run       - Build and run the server"
	@echo "  debug     - Build with debug flags and sanitizers"
	@echo "  valgrind  - Run with memory leak detection"
	@echo "  tsan      - Build with thread sanitizer"
	@echo "  help      - Show this help message"

# Phony targets
.PHONY: all clean rebuild run debug valgrind tsan install-deps help run-concurrency valgrind-test tsan-test run-full-integration valgrind-full tsan-full
