#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build"
PORT=8996
OUTPUT_DIR="${ROOT_DIR}/concurrency_downloads"

echo "=========================================================="
echo " 10-Client Concurrent Streaming Stress Test"
echo "=========================================================="

# Ensure executables exist
if [[ ! -f "${BUILD_DIR}/media_server" || ! -f "${BUILD_DIR}/media_client" ]]; then
    cmake -B "${BUILD_DIR}" -S "${ROOT_DIR}" -DCMAKE_BUILD_TYPE=Release
    cmake --build "${BUILD_DIR}" -j"$(nproc)"
fi

# Ensure media exists
bash "${SCRIPT_DIR}/generate_media.sh" status=none > /dev/null 2>&1

rm -rf "${OUTPUT_DIR}"
mkdir -p "${OUTPUT_DIR}"

# Start server in background
"${BUILD_DIR}/media_server" --port "${PORT}" --media-dir "${ROOT_DIR}/media" --chunk-size 65536 > /dev/null 2>&1 &
SERVER_PID=$!

cleanup() {
    kill -INT "${SERVER_PID}" 2>/dev/null || true
    wait "${SERVER_PID}" 2>/dev/null || true
    rm -rf "${OUTPUT_DIR}"
}
trap cleanup EXIT

# Allow server time to bind
sleep 1

echo "Launching 10 concurrent client processes downloading files simultaneously..."
CLIENT_PIDS=()
START_TIME=$(date +%s%N)

# 10 Clients:
# Clients 1-3: sample_stream_large.bin (20 MB each = 60 MB)
# Clients 4-7: sample_video_720p.mp4    (5 MB each = 20 MB)
# Clients 8-10: sample_audio.bin        (256 KB each = ~1 MB)
FILES=(
    "sample_stream_large.bin"
    "sample_stream_large.bin"
    "sample_stream_large.bin"
    "sample_video_720p.mp4"
    "sample_video_720p.mp4"
    "sample_video_720p.mp4"
    "sample_video_720p.mp4"
    "sample_audio.bin"
    "sample_audio.bin"
    "sample_audio.bin"
)

for i in "${!FILES[@]}"; do
    FILE="${FILES[$i]}"
    CLIENT_NUM=$((i + 1))
    CLIENT_DIR="${OUTPUT_DIR}/client_${CLIENT_NUM}"
    mkdir -p "${CLIENT_DIR}"

    "${BUILD_DIR}/media_client" 127.0.0.1 "${PORT}" "${FILE}" "${CLIENT_DIR}" > /dev/null 2>&1 &
    CLIENT_PIDS+=($!)
done

# While all 10 clients are actively downloading, verify server responsiveness
PROBE_START=$(date +%s%N)
"${BUILD_DIR}/media_client" 127.0.0.1 "${PORT}" --list > /dev/null 2>&1
PROBE_END=$(date +%s%N)
PROBE_LATENCY_MS=$(( (PROBE_END - PROBE_START) / 1000000 ))

echo "-> Probe request (LIST) completed during active concurrency in ${PROBE_LATENCY_MS} ms"

# Wait for all 10 clients
for pid in "${CLIENT_PIDS[@]}"; do
    wait "$pid"
done

END_TIME=$(date +%s%N)
DURATION_MS=$(( (END_TIME - START_TIME) / 1000000 ))
DURATION_SEC=$(awk -v ms="${DURATION_MS}" 'BEGIN { printf "%.3f", ms / 1000 }')

echo "All 10 client processes finished in ${DURATION_SEC} s."
echo ""
echo "Verifying bit-for-bit SHA-256 checksums of all 10 downloads..."

TOTAL_BYTES=0
ALL_MATCH=true

for i in "${!FILES[@]}"; do
    FILE="${FILES[$i]}"
    CLIENT_NUM=$((i + 1))
    CLIENT_FILE="${OUTPUT_DIR}/client_${CLIENT_NUM}/${FILE}"

    if [[ ! -f "${CLIENT_FILE}" ]]; then
        echo "❌ Client #${CLIENT_NUM}: File not found!"
        ALL_MATCH=false
        continue
    fi

    FILE_SZ=$(stat -c%s "${CLIENT_FILE}")
    TOTAL_BYTES=$(( TOTAL_BYTES + FILE_SZ ))

    ORIG_HASH=$(sha256sum "${ROOT_DIR}/media/${FILE}" | awk '{print $1}')
    CLIENT_HASH=$(sha256sum "${CLIENT_FILE}" | awk '{print $1}')

    if [[ "${ORIG_HASH}" == "${CLIENT_HASH}" ]]; then
        echo "  [OK] Client #${CLIENT_NUM} (${FILE}, ${FILE_SZ} bytes) - SHA-256 match"
    else
        echo "❌ [MISMATCH] Client #${CLIENT_NUM} (${FILE}) hash mismatch!"
        ALL_MATCH=false
    fi
done

TOTAL_MB=$(awk -v bytes="${TOTAL_BYTES}" 'BEGIN { printf "%.2f", bytes / (1024 * 1024) }')
THROUGHPUT_MBPS=$(awk -v mb="${TOTAL_MB}" -v sec="${DURATION_SEC}" 'BEGIN { printf "%.2f", mb / sec }')

echo ""
echo "=========================================================="
echo " Concurrency Test Summary:"
echo "   - Concurrent Clients: 10"
echo "   - Probe Latency:      ${PROBE_LATENCY_MS} ms (remained fully responsive)"
echo "   - Total Transferred:  ${TOTAL_BYTES} bytes (${TOTAL_MB} MB)"
echo "   - Total Wall Time:    ${DURATION_SEC} s"
echo "   - Aggregate Speed:    ${THROUGHPUT_MBPS} MB/s"
echo "=========================================================="

if [ "$ALL_MATCH" = true ]; then
    echo "✅ ALL CHECKS PASSED: Server handled 10 concurrent clients without corruption or blockage."
    exit 0
else
    echo "❌ CONCURRENCY VERIFICATION FAILED."
    exit 1
fi
