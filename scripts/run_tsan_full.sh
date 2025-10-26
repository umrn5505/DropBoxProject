#!/bin/bash
set -euo pipefail

REPO_DIR="$(cd "$(dirname "$0")/.." && pwd)"
LOG="$REPO_DIR/tsan_full_server.log"
TEST_BIN="$REPO_DIR/tests/full_integration_test"
SERVER_BIN="$REPO_DIR/dropbox_server"

# Build server with tsan
echo "Building server with ThreadSanitizer..."
make clean
CFLAGS='-Wall -Wextra -std=c99 -pthread -g -fsanitize=thread' LDFLAGS='-pthread -lssl -lcrypto -fsanitize=thread' make -j1

# Kill any existing server on port 8080
PIDS=$(ss -ltnp 2>/dev/null | awk '/:8080/ { gsub(/.*pid=/,"",$0); gsub(/,.*$/,"",$0); print $NF }' | sort -u || true)
if [ -n "$PIDS" ]; then
  echo "Killing processes: $PIDS"
  for p in $PIDS; do kill -TERM $p || true; done
  sleep 1
fi

# Run server with TSAN output redirected
"$SERVER_BIN" 2> "$LOG" &
PID=$!
echo "Server PID $PID"

# Wait for listener
for i in {1..20}; do
  if ss -ltn | grep -q ":8080"; then break; fi; sleep 0.5
done

# Run integration test
"$TEST_BIN"

# Stop server
kill -INT $PID || true
wait $PID || true

echo "TSAN log: $LOG"
exit 0

