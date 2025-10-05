#!/bin/bash
# Test script for enhanced DropBox server

echo "=== DropBox Server Enhanced Implementation Test Script ==="
echo

# Create test files
echo "Creating test files..."
echo "This is a high priority test file" > high_priority.txt
echo "This is a medium priority test file with more content to test larger uploads" > medium_priority.txt
echo "Low priority file" > low_priority.txt

# Test data for stress testing
dd if=/dev/zero of=large_file.bin bs=1024 count=1024 2>/dev/null  # 1MB file

echo "Test files created:"
ls -la *.txt *.bin

echo
echo "=== Manual Test Instructions ==="
echo "1. Start the server: ./dropbox_server"
echo "2. In another terminal, run: ./enhanced_test_client"
echo "3. Test the following scenarios:"
echo

echo "--- Authentication Test ---"
echo "SIGNUP testuser password123"
echo

echo "--- Priority Upload Test ---"
echo "UPLOAD high_priority.txt --high"
echo "UPLOAD medium_priority.txt --medium" 
echo "UPLOAD low_priority.txt --low"
echo

echo "--- File Listing Test ---"
echo "LIST"
echo

echo "--- Priority Download Test ---"
echo "DOWNLOAD high_priority.txt --high"
echo "DOWNLOAD medium_priority.txt"
echo

echo "--- Quota Test ---"
echo "UPLOAD large_file.bin  # Should show quota usage"
echo

echo "--- Concurrent Test ---"
echo "# Open multiple clients and test concurrent operations"
echo

echo "--- Cleanup Test ---"
echo "DELETE high_priority.txt"
echo "DELETE medium_priority.txt --low"
echo "LIST  # Should show reduced quota usage"
echo

echo "QUIT"
echo

echo "=== Expected Behaviors ==="
echo "✓ Priority tasks should be processed before low priority"
echo "✓ File uploads should update quota usage"
echo "✓ File downloads should work correctly"
echo "✓ Concurrent operations on same file should be handled safely"
echo "✓ File listing should show metadata and quota information"
echo "✓ No memory leaks or race conditions"
echo "✓ Efficient thread synchronization"
echo

echo "=== Stress Test Commands ==="
echo "# Test with multiple concurrent clients:"
echo "for i in {1..5}; do ./enhanced_test_client < test_commands.txt & done"
echo

echo "=== Compilation Commands ==="
echo "# Standard build:"
echo "make"
echo
echo "# Debug build with sanitizers:"
echo "make debug"
echo
echo "# Thread safety test:"
echo "make tsan"
echo
echo "# Memory leak test:"
echo "make valgrind"
echo

echo "Test setup complete!"