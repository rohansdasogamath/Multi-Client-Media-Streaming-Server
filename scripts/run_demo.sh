#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build"
PORT=8899
DOWNLOAD_DIR="${ROOT_DIR}/demo_downloads"

echo "=========================================================="
echo " Multi-Client Media Streaming Server - End-to-End Demo"
echo "=========================================================="

# 1. Build if not already built
if [[ ! -f "${BUILD_DIR}/media_server" || ! -f "${BUILD_DIR}/media_client" ]]; then
    echo "[1/5] Building project with CMake..."
    cmake -B "${BUILD_DIR}" -S "${ROOT_DIR}" -DCMAKE_BUILD_TYPE=Release
    cmake --build "${BUILD_DIR}" -j"$(nproc)"
else
    echo "[1/5] Executables found in ${BUILD_DIR}."
fi

# 2. Generate media
echo "[2/5] Checking sample media..."
bash "${SCRIPT_DIR}/generate_media.sh"

# 3. Start server in background
echo "[3/5] Starting media_server on port ${PORT}..."
rm -rf "${DOWNLOAD_DIR}"
mkdir -p "${DOWNLOAD_DIR}"

"${BUILD_DIR}/media_server" --port "${PORT}" --media-dir "${ROOT_DIR}/media" --chunk-size 65536 &
SERVER_PID=$!

cleanup() {
    echo ""
    echo "Shutting down demo server (PID: ${SERVER_PID})..."
    kill -INT "${SERVER_PID}" 2>/dev/null || true
    wait "${SERVER_PID}" 2>/dev/null || true
    echo "Server stopped cleanly."
}
trap cleanup EXIT

# Allow server time to bind
sleep 1

# 4. Run client to list files
echo "[4/5] Client: Listing available media..."
"${BUILD_DIR}/media_client" --server 127.0.0.1 --port "${PORT}" --list

# 5. Run concurrent downloads
echo "[5/5] Launching 3 concurrent client downloads..."
"${BUILD_DIR}/media_client" --server 127.0.0.1 --port "${PORT}" --get sample_intro.txt --output-dir "${DOWNLOAD_DIR}" &
CLIENT1_PID=$!

"${BUILD_DIR}/media_client" --server 127.0.0.1 --port "${PORT}" --get sample_audio.bin --output-dir "${DOWNLOAD_DIR}" &
CLIENT2_PID=$!

"${BUILD_DIR}/media_client" --server 127.0.0.1 --port "${PORT}" --get sample_video_720p.mp4 --output-dir "${DOWNLOAD_DIR}" &
CLIENT3_PID=$!

wait "${CLIENT1_PID}"
wait "${CLIENT2_PID}"
wait "${CLIENT3_PID}"

echo ""
echo "Verifying SHA-256 integrity of all streamed files..."
ORIG_HASH=$(sha256sum "${ROOT_DIR}/media/sample_video_720p.mp4" | awk '{print $1}')
DOWN_HASH=$(sha256sum "${DOWNLOAD_DIR}/sample_video_720p.mp4" | awk '{print $1}')

echo "Original file hash:   ${ORIG_HASH}"
echo "Downloaded file hash: ${DOWN_HASH}"

if [[ "${ORIG_HASH}" == "${DOWN_HASH}" ]]; then
    echo "✅ SUCCESS: All downloaded files match source bit-for-bit!"
else
    echo "❌ ERROR: Checksum mismatch detected!"
    exit 1
fi

echo "=========================================================="
echo " Demo completed successfully!"
echo "=========================================================="
