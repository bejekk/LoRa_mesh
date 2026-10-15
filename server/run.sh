#!/usr/bin/env bash
set -e

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$DIR"

if [ -d "venv" ]; then
    source venv/bin/activate
fi

cleanup() {
    echo -e "\n[Shutdown] Stopping services..."
    kill "$BRIDGE_PID" 2>/dev/null || true
    exit 0
}

trap cleanup SIGINT SIGTERM

python3 -u serial_bridge.py &
BRIDGE_PID=$!
echo "[Bridge] Background worker started (PID: $BRIDGE_PID)"

echo "[Server] Launching FastAPI service on http://0.0.0.0:8000"
exec uvicorn app:app --host 0.0.0.0 --port 8000
