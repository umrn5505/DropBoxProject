#!/bin/bash
# Test: Worker-to-Client Result Delivery Verification
# This script verifies that the result of a worker thread is correctly delivered to the right client session.

set -e

echo "=== Worker-to-Client Result Delivery Test ==="
echo

# Create a test file for upload
TESTFILE="result_delivery_test.txt"
echo "This is a test for worker-to-client result delivery." > $TESTFILE

# Start two clients: one uploads, one lists, both should get correct responses
(
    echo "LOGIN jimmy 111"
    sleep 1
    echo "UPLOAD $TESTFILE"
    sleep 2
    echo "QUIT"
) | ../test_client > upload_output.txt &
PID1=$!

(
    sleep 0.5
    echo "LOGIN jimmy 111"
    sleep 1
    echo "LIST"
    sleep 2
    echo "QUIT"
) | ../test_client > list_output.txt &
PID2=$!

wait $PID1
wait $PID2

echo "--- Upload Client Output ---"
cat upload_output.txt

echo "--- List Client Output ---"
cat list_output.txt

echo
if grep -q "SUCCESS: Operation completed successfully" upload_output.txt && grep -q "$TESTFILE" list_output.txt; then
    echo "[PASS] Worker-to-client result delivery is working correctly."
else
    echo "[FAIL] Worker-to-client result delivery failed."
    exit 1
fi
