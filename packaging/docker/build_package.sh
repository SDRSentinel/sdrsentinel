#!/bin/bash
set -e

# Default to Ubuntu 24.04 (Noble) if no argument given
BASE_IMAGE="${1:-ubuntu:noble}"
TAG_NAME="sdrsentinel_build_${BASE_IMAGE//[:\/]/_}"

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

echo "==> Building Docker image using base: ${BASE_IMAGE}..."
docker build --build-arg BASE_IMAGE="${BASE_IMAGE}" -t "${TAG_NAME}" "${SCRIPT_DIR}"

echo "==> Running build in container..."
docker run --rm \
    -v "${REPO_ROOT}:/root/SDRSentinel" \
    --env BUILD_NO="${BUILD_NO:-}" \
    "${TAG_NAME}"

echo "==> Done! Debian package created in ${REPO_ROOT}."
