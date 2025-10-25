#!/bin/bash
# Phase 2: Multi-Client Concurrent Access Test
# Tests multiple simultaneous clients, including multiple sessions per user

echo "=== Phase 2: Multi-Client Concurrent Access Test ==="
echo

# Create test directory if it doesn't exist
mkdir -p ../Testing
cd ../Testing || exit

# Create test files of various sizes
echo "Creating test files..."
echo "Test file for client 1" > test_file_1.txt
echo "Test file for client 2 - slightly larger content for testing" > test_file_2.txt
echo "Test file for client 3" > test_file_3.txt

# Create a larger file for testing concurrent uploads
dd if=/dev/zero of=concurrent_test.bin bs=1M count=5 2>/dev/null

echo "Test files created."
echo

# Test 1: Multiple different users concurrently
echo "=== Test 1: Multiple Different Users Concurrently ==="
echo "Starting 3 clients with different users..."
echo



# Always use LOGIN for existing users
(
    echo "LOGIN user1 pass1"
    sleep 1
    echo "UPLOAD test_file_1.txt"
    sleep 2
    echo "LIST"
    sleep 1
    echo "QUIT"
) | ../test_client &
PID1=$!

(
    sleep 0.5
    echo "LOGIN user2 pass2"
    sleep 1
    echo "UPLOAD test_file_2.txt"
    sleep 2
    echo "LIST"
    sleep 1
    echo "QUIT"
) | ../test_client &
PID2=$!

(
    sleep 1
    echo "LOGIN user3 pass3"
    sleep 1
    echo "UPLOAD test_file_3.txt"
    sleep 2
    echo "LIST"
    sleep 1
    echo "QUIT"
) | ../test_client &
PID3=$!

# Wait for all clients to complete
wait $PID1
wait $PID2
wait $PID3

echo
echo "Test 1 completed: Multiple different users"
echo
sleep 2

# Test 2: Same user, multiple simultaneous sessions
echo "=== Test 2: Same User, Multiple Simultaneous Sessions ==="
echo "Starting 3 clients with the SAME user (jimmy)..."
echo

(
    echo "LOGIN jimmy 111"
    sleep 1
    echo "UPLOAD test_file_1.txt"
    sleep 2
    echo "LIST"
    sleep 1
    echo "QUIT"
) | ../test_client &
SESSION1=$!

(
    sleep 0.5
    echo "LOGIN jimmy 111"
    sleep 1
    echo "UPLOAD test_file_2.txt"
    sleep 2
    echo "LIST"
    sleep 1
    echo "QUIT"
) | ../test_client &
SESSION2=$!

(
    sleep 1
    echo "LOGIN jimmy 111"
    sleep 1
    echo "UPLOAD test_file_3.txt"
    sleep 2
    echo "LIST"
    sleep 1
    echo "QUIT"
) | ../test_client &
SESSION3=$!

# Wait for all sessions to complete
wait $SESSION1
wait $SESSION2
wait $SESSION3

echo
echo "Test 2 completed: Same user, multiple sessions"
echo
sleep 2

# Test 3: Concurrent operations on DIFFERENT files (should NOT block)
echo "=== Test 3: Concurrent Operations on Different Files ==="
echo "Multiple clients uploading different files simultaneously..."
echo

(
    echo "LOGIN jimmy 111"
    sleep 1
    echo "UPLOAD test_file_1.txt"
    sleep 3
    echo "QUIT"
) | ../test_client &
DIFF1=$!

(
    sleep 0.5
    echo "LOGIN jimmy 111"
    sleep 1
    echo "UPLOAD test_file_2.txt"
    sleep 3
    echo "QUIT"
) | ../test_client &
DIFF2=$!

(
    sleep 1
    echo "LOGIN jimmy 111"
    sleep 1
    echo "UPLOAD test_file_3.txt"
    sleep 3
    echo "QUIT"
) | ../test_client &
DIFF3=$!

wait $DIFF1
wait $DIFF2
wait $DIFF3

echo
echo "Test 3 completed: Concurrent operations on different files"
echo
sleep 2

# Test 4: Concurrent operations on SAME file (should serialize with locks)
echo "=== Test 4: Concurrent Operations on SAME File (Lock Test) ==="
echo "Multiple clients trying to upload the same file..."
echo "Expected: Serialized access with visible lock messages"
echo

(
    echo "LOGIN jimmy 111"
    sleep 1
    echo "UPLOAD concurrent_test.bin"
    sleep 4
    echo "QUIT"
) | ../test_client &
SAME1=$!

(
    sleep 0.5
    echo "LOGIN jimmy 111"
    sleep 1
    echo "UPLOAD concurrent_test.bin"
    sleep 4
    echo "QUIT"
) | ../test_client &
SAME2=$!

wait $SAME1
wait $SAME2

echo
echo "Test 4 completed: Concurrent operations on same file"
echo
sleep 2

# Test 5: Mixed operations (upload, download, list, delete) concurrently
echo "=== Test 5: Mixed Concurrent Operations ==="
echo "Multiple clients performing different operations simultaneously..."
echo

(
    echo "LOGIN jimmy 111"
    sleep 1
    echo "LIST"
    sleep 2
    echo "QUIT"
) | ../test_client &
MIX1=$!

(
    sleep 0.5
    echo "LOGIN jimmy 111"
    sleep 1
    echo "DOWNLOAD test_file_1.txt"
    sleep 2
    echo "QUIT"
) | ../test_client &
MIX2=$!

(
    sleep 1
    echo "LOGIN jimmy 111"
    sleep 1
    echo "UPLOAD test_file_3.txt"
    sleep 2
    echo "QUIT"
) | ../test_client &
MIX3=$!

wait $MIX1
wait $MIX2
wait $MIX3

echo
echo "Test 5 completed: Mixed concurrent operations"
echo

echo "=== All Phase 2 Tests Completed ==="
echo
echo "Summary of tests:"
echo "✓ Test 1: Multiple different users - tested concurrent access by different users"
echo "✓ Test 2: Same user, multiple sessions - tested multiple simultaneous sessions for one user"
echo "✓ Test 3: Different files - tested that operations on different files don't block each other"
echo "✓ Test 4: Same file - tested that file locks properly serialize conflicting operations"
echo "✓ Test 5: Mixed operations - tested various operations running concurrently"
echo
echo "Check server output for:"
echo "  - RED lock acquisition/release messages"
echo "  - Proper serialization of same-file operations"
echo "  - Concurrent processing of different-file operations"
echo "  - No race conditions or crashes"
echo
