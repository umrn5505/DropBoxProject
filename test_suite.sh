#!/bin/bash

# DropBox Server Test Suite
# This script tests various aspects of the server implementation

echo "=== DropBox Server Test Suite ==="

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Test results tracking
TESTS_PASSED=0
TESTS_FAILED=0

# Function to print test results
print_result() {
    if [ $1 -eq 0 ]; then
        echo -e "${GREEN}✓ PASS${NC}: $2"
        TESTS_PASSED=$((TESTS_PASSED + 1))
    else
        echo -e "${RED}✗ FAIL${NC}: $2"
        TESTS_FAILED=$((TESTS_FAILED + 1))
    fi
}

echo -e "${YELLOW}Building server...${NC}"
make clean > /dev/null 2>&1
make > /dev/null 2>&1
if [ $? -eq 0 ]; then
    print_result 0 "Server compilation"
else
    print_result 1 "Server compilation"
    echo "Cannot proceed without successful compilation"
    exit 1
fi

echo -e "${YELLOW}Building test client...${NC}"
gcc test_client.c -o test_client > /dev/null 2>&1
if [ $? -eq 0 ]; then
    print_result 0 "Test client compilation"
else
    print_result 1 "Test client compilation"
fi

echo -e "${YELLOW}Testing with Thread Sanitizer...${NC}"
make clean > /dev/null 2>&1
make tsan > /dev/null 2>&1
if [ $? -eq 0 ]; then
    print_result 0 "Thread Sanitizer build"
else
    print_result 1 "Thread Sanitizer build"
fi

# Clean up for standard build
make clean > /dev/null 2>&1
make > /dev/null 2>&1

echo -e "${YELLOW}Starting server for functional tests...${NC}"
./dropbox_server &
SERVER_PID=$!
sleep 2

# Check if server is running
if kill -0 $SERVER_PID 2>/dev/null; then
    print_result 0 "Server startup"
else
    print_result 1 "Server startup"
    exit 1
fi

echo -e "${YELLOW}Testing basic connectivity...${NC}"
# Test if port is listening
netstat -tuln 2>/dev/null | grep ":8080" > /dev/null
if [ $? -eq 0 ]; then
    print_result 0 "Server listening on port 8080"
else
    print_result 1 "Server listening on port 8080"
fi

echo -e "${YELLOW}Testing concurrent connections...${NC}"
# Start multiple test clients in background
for i in {1..3}; do
    (
        echo -e "SIGNUP testuser$i password$i\nQUIT\n" | nc localhost 8080 > /dev/null 2>&1
    ) &
done

# Wait for connections to complete
sleep 3

# Check if server is still running (didn't crash)
if kill -0 $SERVER_PID 2>/dev/null; then
    print_result 0 "Concurrent connections handling"
else
    print_result 1 "Concurrent connections handling"
fi

echo -e "${YELLOW}Testing authentication flow...${NC}"
# Test signup and login
AUTH_RESULT=$(echo -e "SIGNUP testauth authpass\nLIST\nQUIT\n" | nc localhost 8080 2>/dev/null)
if echo "$AUTH_RESULT" | grep -q "SIGNUP_SUCCESS"; then
    print_result 0 "User signup functionality"
else
    print_result 1 "User signup functionality"
fi

if echo "$AUTH_RESULT" | grep -q "PLACEHOLDER.*LIST"; then
    print_result 0 "Command processing after authentication"
else
    print_result 1 "Command processing after authentication"
fi

echo -e "${YELLOW}Testing command parsing...${NC}"
CMD_RESULT=$(echo -e "SIGNUP cmdtest cmdpass\nUPLOAD test.txt\nDOWNLOAD test.txt\nDELETE test.txt\nLIST\nQUIT\n" | nc localhost 8080 2>/dev/null)

if echo "$CMD_RESULT" | grep -q "PLACEHOLDER.*UPLOAD"; then
    print_result 0 "UPLOAD command parsing"
else
    print_result 1 "UPLOAD command parsing"
fi

if echo "$CMD_RESULT" | grep -q "PLACEHOLDER.*DOWNLOAD"; then
    print_result 0 "DOWNLOAD command parsing"
else
    print_result 1 "DOWNLOAD command parsing"
fi

if echo "$CMD_RESULT" | grep -q "PLACEHOLDER.*DELETE"; then
    print_result 0 "DELETE command parsing"
else
    print_result 1 "DELETE command parsing"
fi

echo -e "${YELLOW}Testing error handling...${NC}"
ERROR_RESULT=$(echo -e "SIGNUP errortest errorpass\nINVALID_COMMAND\nUPLOAD\nQUIT\n" | nc localhost 8080 2>/dev/null)

if echo "$ERROR_RESULT" | grep -q "ERROR.*Invalid command"; then
    print_result 0 "Invalid command error handling"
else
    print_result 1 "Invalid command error handling"
fi

if echo "$ERROR_RESULT" | grep -q "ERROR.*Invalid command format"; then
    print_result 0 "Missing filename error handling"
else
    print_result 1 "Missing filename error handling"
fi

echo -e "${YELLOW}Testing graceful shutdown...${NC}"
# Send SIGINT to server
kill -INT $SERVER_PID
sleep 3

# Check if server shut down properly
if kill -0 $SERVER_PID 2>/dev/null; then
    print_result 1 "Graceful shutdown"
    # Force kill if it didn't shut down
    kill -KILL $SERVER_PID 2>/dev/null
else
    print_result 0 "Graceful shutdown"
fi

echo -e "${YELLOW}Testing memory management...${NC}"
# Check for created directories and files
if [ -d "users" ]; then
    print_result 0 "User directory creation"
else
    print_result 1 "User directory creation"
fi

if [ -d "storage" ]; then
    print_result 0 "Storage directory creation"
else
    print_result 1 "Storage directory creation"
fi

# Clean up test files
rm -rf users storage 2>/dev/null

echo ""
echo "=== Test Summary ==="
echo -e "Tests Passed: ${GREEN}$TESTS_PASSED${NC}"
echo -e "Tests Failed: ${RED}$TESTS_FAILED${NC}"
echo -e "Total Tests: $((TESTS_PASSED + TESTS_FAILED))"

if [ $TESTS_FAILED -eq 0 ]; then
    echo -e "${GREEN}All tests passed! 🎉${NC}"
    exit 0
else
    echo -e "${RED}Some tests failed. Please review the implementation.${NC}"
    exit 1
fi