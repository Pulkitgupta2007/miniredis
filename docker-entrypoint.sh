#!/bin/bash
set -e

REDIS_PORT="${REDIS_PORT:-6379}"
REDIS_HOST="${REDIS_HOST:-127.0.0.1}"
PORT="${PORT:-4000}"

echo "========================================="
echo "   MiniRedis Production Cloud Container  "
echo "========================================="
echo "[1/2] Starting MiniRedis C++ engine on ${REDIS_HOST}:${REDIS_PORT}..."
/usr/local/bin/miniredis-server "${REDIS_PORT}" "${REDIS_HOST}" &
REDIS_PID=$!

cleanup() {
    echo "Terminating MiniRedis container processes..."
    kill -TERM "$REDIS_PID" 2>/dev/null || true
    kill -TERM "$BRIDGE_PID" 2>/dev/null || true
    wait "$REDIS_PID" 2>/dev/null || true
    wait "$BRIDGE_PID" 2>/dev/null || true
    exit 0
}

trap cleanup SIGTERM SIGINT

# Small pause to ensure socket is bound
sleep 0.2

echo "[2/2] Starting Bridge & Dashboard on port ${PORT}..."
node /app/bridge/src/server.js &
BRIDGE_PID=$!

echo "MiniRedis is live and accepting traffic on port ${PORT}!"

# Wait for either process to terminate
wait -n "$REDIS_PID" "$BRIDGE_PID"
EXIT_CODE=$?
cleanup
exit $EXIT_CODE
