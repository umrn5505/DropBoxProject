#!/bin/bash
# Comprehensive Test Suite for Phase 2 - Tasks 6-8
# This script runs all testing scenarios including concurrency, Valgrind, and ThreadSanitizer

set -euo pipefail

REPO_DIR="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO_DIR"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo "========================================"
echo "  PHASE 2 COMPREHENSIVE TEST SUITE"
echo "   Testing, Race/Leak Detection"
echo "========================================"
echo

# Create logs directory
mkdir -p logs

# Function to cleanup
cleanup() {
    echo "Cleaning up..."
    # Kill any server processes
    pkill -f dropbox_server || true
    # Kill processes on port 8080
    if command -v lsof >/dev/null 2>&1; then
        lsof -ti :8080 | xargs kill -9 2>/dev/null || true
    fi
    sleep 1
}

trap cleanup EXIT

#========================================
# TEST 1: Basic Concurrency Test
#========================================
echo -e "${YELLOW}[TEST 1] Running Basic Concurrency Test${NC}"
echo "Building concurrency test..."
make -C tests concurrency_test || {
    echo -e "${RED}Failed to build concurrency test${NC}"
    exit 1
}

echo "Starting server..."
./dropbox_server &
SERVER_PID=$!
sleep 2

echo "Running concurrency test with 20 clients, 40 operations each..."
./tests/concurrency_test 20 40 > logs/concurrency_basic.log 2>&1 && \
    echo -e "${GREEN}✓ Basic concurrency test PASSED${NC}" || \
    echo -e "${RED}✗ Basic concurrency test FAILED${NC}"

kill -INT $SERVER_PID 2>/dev/null || true
wait $SERVER_PID 2>/dev/null || true
sleep 2

#========================================
# TEST 2: Stress Test (Many Clients)
#========================================
echo
echo -e "${YELLOW}[TEST 2] Running Stress Test (50 clients)${NC}"
echo "Starting server..."
./dropbox_server &
SERVER_PID=$!
sleep 2

echo "Running stress test with 50 clients, 30 operations each..."
./tests/concurrency_test 50 30 > logs/concurrency_stress.log 2>&1 && \
    echo -e "${GREEN}✓ Stress test PASSED${NC}" || \
    echo -e "${RED}✗ Stress test FAILED${NC}"

kill -INT $SERVER_PID 2>/dev/null || true
wait $SERVER_PID 2>/dev/null || true
sleep 2

#========================================
# TEST 3: Multiple Sessions Per User
#========================================
echo
echo -e "${YELLOW}[TEST 3] Running Multiple Sessions Per User Test${NC}"
echo "Starting server..."
./dropbox_server &
SERVER_PID=$!
sleep 2

echo "Running test with same user connecting multiple times..."
./tests/full_integration_test > logs/multi_session.log 2>&1 && \
    echo -e "${GREEN}✓ Multiple sessions test PASSED${NC}" || \
    echo -e "${RED}✗ Multiple sessions test FAILED${NC}"

kill -INT $SERVER_PID 2>/dev/null || true
wait $SERVER_PID 2>/dev/null || true
sleep 2

#========================================
# TEST 4: Valgrind Memory Leak Detection
#========================================
echo
echo -e "${YELLOW}[TEST 4] Running Valgrind Memory Leak Detection${NC}"
echo "This may take several minutes..."

bash scripts/run_valgrind_full.sh > logs/valgrind_output.log 2>&1 && \
    echo -e "${GREEN}✓ Valgrind test PASSED${NC}" || \
    echo -e "${RED}✗ Valgrind test FAILED${NC}"

echo "Checking Valgrind results..."
if [ -f valgrind_full_server.log ]; then
    LEAK_COUNT=$(grep "definitely lost:" valgrind_full_server.log | awk '{print $4}' | sed 's/,//g')
    ERROR_COUNT=$(grep "ERROR SUMMARY:" valgrind_full_server.log | awk '{print $4}' | sed 's/,//g')
    
    echo "  Memory definitely lost: ${LEAK_COUNT:-0} bytes"
    echo "  Total errors: ${ERROR_COUNT:-0}"
    
    if [ "${LEAK_COUNT:-0}" -eq 0 ] && [ "${ERROR_COUNT:-0}" -eq 0 ]; then
        echo -e "${GREEN}✓ No memory leaks detected!${NC}"
    else
        echo -e "${YELLOW}⚠ Memory issues detected - check valgrind_full_server.log${NC}"
    fi
fi

sleep 2

#========================================
# TEST 5: ThreadSanitizer Race Detection
#========================================
echo
echo -e "${YELLOW}[TEST 5] Running ThreadSanitizer Race Detection${NC}"
echo "Rebuilding with ThreadSanitizer..."

bash scripts/run_tsan_full.sh > logs/tsan_output.log 2>&1 && \
    echo -e "${GREEN}✓ ThreadSanitizer test PASSED${NC}" || \
    echo -e "${RED}✗ ThreadSanitizer test FAILED${NC}"

echo "Checking ThreadSanitizer results..."
if [ -f tsan_full_server.log ]; then
    RACE_COUNT=$(grep -c "WARNING: ThreadSanitizer: data race" tsan_full_server.log || echo "0")
    
    echo "  Data races found: $RACE_COUNT"
    
    if [ "$RACE_COUNT" -eq 0 ]; then
        echo -e "${GREEN}✓ No data races detected!${NC}"
    else
        echo -e "${YELLOW}⚠ Data races detected - check tsan_full_server.log${NC}"
    fi
fi

# Rebuild without sanitizers for normal use
echo "Rebuilding without sanitizers..."
make clean > /dev/null 2>&1
make > /dev/null 2>&1

#========================================
# SUMMARY
#========================================
echo
echo "========================================"
echo "  TEST SUMMARY"
echo "========================================"
echo "Logs available in logs/ directory:"
echo "  - logs/concurrency_basic.log"
echo "  - logs/concurrency_stress.log"
echo "  - logs/multi_session.log"
echo "  - logs/valgrind_output.log"
echo "  - logs/tsan_output.log"
echo
echo "Detailed reports:"
echo "  - valgrind_full_server.log"
echo "  - tsan_full_server.log"
echo
echo -e "${GREEN}All tests completed!${NC}"
echo "========================================"
