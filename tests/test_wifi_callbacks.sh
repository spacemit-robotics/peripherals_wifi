#!/usr/bin/env bash
# Copyright (C) 2026 SpacemiT (Hangzhou) Technology Co. Ltd.
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
module_root="$(cd "$script_dir/.." && pwd)"
artifact_dir="${SROBOTIS_TEST_ARTIFACT_DIR:-${SROBOTIS_OUTPUT_ROOT:-$PWD/output}/test-artifacts/components/peripherals/wifi/${SROBOTIS_TEST_NAME:-wifi-message-callbacks}}"
mkdir -p "$artifact_dir/logs"
{
    cmake -S "$module_root" -B "$artifact_dir/build" -DBUILD_TESTS=ON
    cmake --build "$artifact_dir/build" --target test_wifi_demo -j2
    python3 "$script_dir/test_callbacks.py" "$artifact_dir/build/libwifi.so" -v
    python3 "$script_dir/test_demo_callbacks.py" "$artifact_dir/build/test_wifi_demo" -v
} 2>&1 | tee "$artifact_dir/logs/wifi_callbacks.log"
