#!/bin/bash
set -e

BUILD_DIR="build"
BUILD_TYPE="${1:-Debug}"

mkdir -p "${BUILD_DIR}"
cmake -S . -B "${BUILD_DIR}" -DCMAKE_BUILD_TYPE="${BUILD_TYPE}"
cmake --build "${BUILD_DIR}" -j"$(nproc)"
