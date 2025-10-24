#!/bin/bash
# Test: Per-file concurrency control (conflicting operations serialize)
# This script launches multiple clients attempting to upload the SAME file simultaneously.
# Expected: Only one upload proceeds at a time; others wait for the lock and then proceed in order.

set -e

TEST_FILE="concurrent_test_file.txt"
echo "This is a test file for concurrency lock test." > $TEST_FILE

# Clean up any previous test artifacts
rm -f upload_result_*.log


echo "Starting first client (PID will be shown)..."
(
    echo "LOGIN jimmy 111"
    sleep 1
    echo "UPLOAD $TEST_FILE"
    sleep 4
    echo "QUIT"
) | ../test_client > upload_result_1.log &
PID1=$!
echo "First client started with PID $PID1."

echo "Starting second client (PID will be shown)..."
(
    sleep 0.5
    echo "LOGIN jimmy 111"
    sleep 1
    echo "UPLOAD $TEST_FILE"
    sleep 4
    echo "QUIT"
) | ../test_client > upload_result_2.log &
PID2=$!
echo "Second client started with PID $PID2."

echo "Waiting for both clients to finish..."
wait $PID1
wait $PID2
echo "Both clients finished."

echo
echo "--- upload_result_1.log (last 10 lines) ---"
tail -n 10 upload_result_1.log
echo
echo "--- upload_result_2.log (last 10 lines) ---"
tail -n 10 upload_result_2.log
echo
echo "Check server output for lock acquisition/release messages."
echo "If working, uploads should not overlap and should serialize."
