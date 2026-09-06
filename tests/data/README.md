# Test Data Fixtures

This directory contains deterministic, non-copyrighted binary test fixtures designed specifically for verifying binary file streaming, socket framing, and byte-level transfer integrity.

## Files

- **`test_media_fixture.bin`**:
  - **Size**: 131,072 bytes (128 KB)
  - **Format**: Synthetic binary test fixture. Begins with a 32-byte ASCII header (`MEDIA_STREAM_TEST_FIXTURE_V1.0\n\0`) followed by deterministic pseudo-random byte sequences `(i * 109 + 89) % 256`.
  - **SHA-256**: `99e8e58e6a76b3af84f8eb7bceb75cfad1a745ef44550d0079bbca8269d87ad9`
  - **Purpose**: Exercises multi-chunk binary transfers (e.g. two 64 KB chunks or four 32 KB chunks) and verifies bit-for-bit transmission without requiring large media files or copyrighted content.
