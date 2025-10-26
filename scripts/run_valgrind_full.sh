#!/bin/bash
set -euo pipefail

REPO_DIR="$(cd "$(dirname "$0")/.." && pwd)"
VG_LOG="$REPO_DIR/valgrind_full_server.log"
VG_PID_FILE="$REPO_DIR/vg_pid"
TEST_BIN="$REPO_DIR/tests/full_integration_test"
SERVER_BIN="$REPO_DIR/dropbox_server"

get_listening_pids() {
  # Prefer lsof if available
  if command -v lsof >/dev/null 2>&1; then
    lsof -ti :8080 || true
    return
  fi
  # fallback: parse ss output for pid
  ss -ltnp 2>/dev/null | awk '{ if ($0 ~ ":8080") { if (match($0, /pid=[0-9]+/)) { pid=substr($0, RSTART+4, RLENGTH-4); print pid } } }' || true
}

# Kill any process listening on port 8080
PIDS=$(get_listening_pids | tr '\n' ' ' | sed 's/ $//')
if [ -n "$PIDS" ]; then
  echo "Found processes listening on :8080: $PIDS -- killing them"
  for p in $PIDS; do
    echo "Killing PID $p"
    kill -TERM $p || true
  done
  sleep 1
fi

# Double-check no listener
if get_listening_pids | grep -q '[0-9]'; then
  echo "Port 8080 still in use, aborting" 1>&2
  exit 1
fi

# Start server under valgrind
rm -f "$VG_LOG" "$VG_PID_FILE"
echo "Starting server under Valgrind..."
valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes --log-file="$VG_LOG" "$SERVER_BIN" &
VG_PID=$!
echo $VG_PID > "$VG_PID_FILE"

# Wait for server to be listening on port 8080
for i in {1..20}; do
  if get_listening_pids | grep -q '[0-9]'; then
    echo "Server listening on port 8080"
    break
  fi
  sleep 0.5
done

if ! get_listening_pids | grep -q '[0-9]'; then
  echo "Server didn't start listening on port 8080. Check $VG_LOG" 1>&2
  kill -INT $VG_PID || true
  wait $VG_PID || true
  exit 1
fi

# Run integration test
echo "Running integration test..."
"$TEST_BIN"
TEST_RES=$?

# Stop server
kill -INT $VG_PID || true
wait $VG_PID || true

echo "Valgrind log: $VG_LOG"
exit $TEST_RES
