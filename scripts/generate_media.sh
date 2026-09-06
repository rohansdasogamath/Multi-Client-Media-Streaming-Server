#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
MEDIA_DIR="${ROOT_DIR}/media"

mkdir -p "${MEDIA_DIR}"

echo "Generating sample media files in: ${MEDIA_DIR}"

# 1. Text sample (small)
cat << 'EOF' > "${MEDIA_DIR}/sample_intro.txt"
======================================================
Multi-Client Media Streaming Server - Sample Content
======================================================
This is a sample text file served over the TCP streaming server.
The server streams content in configurable chunks (default 64 KB)
concurrently to multiple clients using standard POSIX sockets.
EOF

# 2. Audio sample simulation (256 KB)
echo "Generating sample_audio.bin (256 KB)..."
dd if=/dev/urandom of="${MEDIA_DIR}/sample_audio.bin" bs=1024 count=256 status=none

# 3. Video sample simulation (5 MB)
echo "Generating sample_video_720p.mp4 (5 MB)..."
dd if=/dev/urandom of="${MEDIA_DIR}/sample_video_720p.mp4" bs=1024 count=5120 status=none

# 4. Large media sample (20 MB)
echo "Generating sample_stream_large.bin (20 MB)..."
dd if=/dev/urandom of="${MEDIA_DIR}/sample_stream_large.bin" bs=1024 count=20480 status=none

echo "Generated files:"
ls -lh "${MEDIA_DIR}"

echo "Sample media generation completed successfully."
