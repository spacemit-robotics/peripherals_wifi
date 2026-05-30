#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
module_root="$(cd "$script_dir/.." && pwd)"
artifact_dir="${SROBOTIS_TEST_ARTIFACT_DIR:-${SROBOTIS_OUTPUT_ROOT:-$PWD/output}/test-artifacts/components/peripherals/wifi/${SROBOTIS_TEST_NAME:-wifi-api-contract}}"
log_dir="$artifact_dir/logs"
build_dir="$artifact_dir/build"
fake_bin="$build_dir/fake-bin"
mode="${1:-all}"
case "$mode" in
    all|functional|error-paths) ;;
    *) echo "usage: $0 [all|functional|error-paths]" >&2; exit 2 ;;
esac

log_file="$log_dir/wifi_api_${mode//-/_}.log"
cc="${CC:-cc}"

mkdir -p "$log_dir" "$build_dir" "$fake_bin"

cat > "$fake_bin/nmcli" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
cmd="$*"

case "$cmd" in
    "-t -f RUNNING general")
        printf 'running\n'
        ;;
    "-t -f WIFI general")
        printf 'enabled\n'
        ;;
    "radio wifi on"|"radio wifi off")
        ;;
    "-t -f DEVICE,TYPE,STATE device status")
        printf 'wlan0:wifi:connected\neth0:ethernet:connected\n'
        ;;
    "-t -f NAME,TYPE,DEVICE connection show --active")
        printf 'HomeNet:wifi:wlan0\n'
        ;;
    "-t -f NAME,TYPE connection show")
        printf 'HomeNet:wifi\nOldNet:wifi\nWired:ethernet\n'
        ;;
    "-t -f BSSID,SSID,FREQ,SIGNAL,SECURITY device wifi list")
        printf 'aa\\:bb\\:cc\\:dd\\:ee\\:ff:HomeNet:2412:77:WPA2\n'
        printf '11\\:22\\:33\\:44\\:55\\:66:OtherNet:2462:45:WPA3\n'
        ;;
    "-t -f ACTIVE,BSSID,SSID,FREQ,SIGNAL,SECURITY device wifi list")
        printf '*:aa\\:bb\\:cc\\:dd\\:ee\\:ff:HomeNet:2412:77:WPA2\n'
        printf ':11\\:22\\:33\\:44\\:55\\:66:OtherNet:2462:45:WPA3\n'
        ;;
    "-t -f GENERAL.HWADDR device show wlan0")
        printf 'GENERAL.HWADDR:12\\:34\\:56\\:78\\:9a\\:bc\n'
        ;;
    "-t -f IP4.ADDRESS,IP4.GATEWAY device show wlan0")
        printf 'IP4.ADDRESS[1]:192.168.1.20/24\n'
        printf 'IP4.GATEWAY:192.168.1.1\n'
        ;;
    "-t -f 802-11-wireless.mode connection show HomeNet")
        printf '802-11-wireless.mode:infrastructure\n'
        ;;
    "-t -f 802-11-wireless.ssid,802-11-wireless.channel,802-11-wireless-security.key-mgmt,802-11-wireless-security.psk connection show HomeNet")
        printf '802-11-wireless.ssid:HomeNet\n'
        printf '802-11-wireless.channel:1\n'
        printf '802-11-wireless-security.key-mgmt:wpa-psk\n'
        printf '802-11-wireless-security.psk:secret123\n'
        ;;
    device\ wifi\ connect*|connection\ down*|-w\ 0\ connection\ down*|connection\ up*|connection\ delete*|connection\ modify*|device\ wifi\ hotspot*|device\ disconnect*)
        ;;
    *)
        printf 'unexpected nmcli command: %s\n' "$cmd" >&2
        exit 9
        ;;
esac
EOF
chmod +x "$fake_bin/nmcli"

{
    echo "[info] module_root=$module_root"
    echo "[info] build_dir=$build_dir"
    echo "[info] cc=$cc"
    echo "[info] mode=$mode"

    "$cc" -D_GNU_SOURCE -std=c99 -Wall -Wextra -Werror \
        -I"$module_root/include" \
        -I"$module_root/src" \
        "$module_root/src/wifi.c" \
        "$module_root/tests/test_wifi_api_contract.c" \
        -o "$build_dir/test_wifi_api_contract"

    PATH="$fake_bin:$PATH" "$build_dir/test_wifi_api_contract" "$mode"
} | tee "$log_file"

case "$mode" in
    all) grep -q "wifi api contract test PASSED" "$log_file" ;;
    functional) grep -q "wifi api functional test PASSED" "$log_file" ;;
    error-paths) grep -q "wifi api error paths test PASSED" "$log_file" ;;
esac
