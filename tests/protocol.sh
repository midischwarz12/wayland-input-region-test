#!/usr/bin/env bash
set -euo pipefail

client=${1:?pass the built client executable}
scratch=$(mktemp -d)
compositor=
cleanup() {
    status=$?
    if [[ -n "$compositor" ]]; then
        kill "$compositor" 2>/dev/null || true
        wait "$compositor" 2>/dev/null || true
    fi
    if (( status != 0 )); then
        cat "$scratch"/*.log >&2
    fi
    rm -rf -- "$scratch"
}
trap cleanup EXIT
export XDG_RUNTIME_DIR="$scratch/runtime"
mkdir -m 700 "$XDG_RUNTIME_DIR"
unset DISPLAY WAYLAND_DISPLAY

weston --backend=headless --renderer=pixman --shell=kiosk-shell.so \
    --no-config --idle-time=0 --width=800 --height=600 --fake-seat \
    --socket=input-region-test >"$scratch/weston.log" 2>&1 &
compositor=$!
for ((attempt = 0; attempt < 200; attempt++)); do
    [[ -S "$XDG_RUNTIME_DIR/input-region-test" ]] && break
    kill -0 "$compositor"
    sleep 0.05
done
test -S "$XDG_RUNTIME_DIR/input-region-test"
export WAYLAND_DISPLAY=input-region-test

for mode in rect empty full; do
    args=()
    [[ "$mode" == rect ]] || args+=("--$mode-input")
    status=0
    WAYLAND_DEBUG=client timeout 2 "$client" "${args[@]}" >"$scratch/$mode.log" 2>&1 || status=$?
    # A healthy client stays open until the test stops it.
    test "$status" -eq 124
    grep -q 'configured 800x600' "$scratch/$mode.log"
    grep -q 'ack_configure(' "$scratch/$mode.log"
    grep -q 'wl_buffer.*release()' "$scratch/$mode.log"
    case "$mode" in
        rect) grep -q 'add(24, 24, 200, 80)' "$scratch/$mode.log" ;;
        empty)
            ! grep -q 'wl_region.*\.add(' "$scratch/$mode.log"
            grep -q 'set_input_region(wl_region' "$scratch/$mode.log"
            ;;
        full) grep -q 'set_input_region(nil)' "$scratch/$mode.log" ;;
    esac
done
printf '%s\n' 'Wayland configure, buffer release, and all three input modes passed'
