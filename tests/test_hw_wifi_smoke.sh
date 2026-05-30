#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
module_root="$(cd "$script_dir/.." && pwd)"
artifact_dir="${SROBOTIS_TEST_ARTIFACT_DIR:-${SROBOTIS_OUTPUT_ROOT:-$PWD/output}/test-artifacts/components/peripherals/wifi/${SROBOTIS_TEST_NAME:-wifi-hardware-smoke}}"
log_dir="$artifact_dir/logs"
build_dir="$artifact_dir/build"
log_file="$log_dir/wifi_hardware_smoke.log"

timeout_s="${WIFI_HW_SMOKE_TIMEOUT_S:-45}"

mkdir -p "$log_dir" "$build_dir"

{
    echo "[info] module_root=$module_root"
    echo "[info] build_dir=$build_dir"
    echo "[info] timeout_s=$timeout_s"

    cmake -S "$module_root" -B "$build_dir" -DBUILD_TESTS=ON
    cmake --build "$build_dir" --target test_wifi_demo -j"$(nproc)"
    LD_LIBRARY_PATH="$build_dir:${LD_LIBRARY_PATH:-}" \
        timeout "$timeout_s" "$build_dir/test_wifi_demo" scan
} 2>&1 | tee "$log_file"

network_count="$(
    sed -n 's/^Found \([0-9][0-9]*\) networks.*/\1/p' "$log_file" | tail -n 1
)"
if [ -z "$network_count" ] || [ "$network_count" -le 0 ]; then
    echo "[error] expected at least one real WiFi scan result, got: ${network_count:-none}" | tee -a "$log_file"
    exit 1
fi

if ! grep -q "^  SSID=" "$log_file"; then
    echo "[error] scan reported $network_count network(s), but no SSID entries were printed" | tee -a "$log_file"
    exit 1
fi
