#!/bin/bash
set -euo pipefail
cd "$1"
platform=$2
frames=$3
if [[ "$frames" == detach ]]; then
    log="$PWD/Player.log"
    if [[ "$platform" == LinuxServer ]]; then startup="Opened startup scene:"
    else startup="Presented first frame:"
    fi
    # Retain this session until the Player exits, including any lazy reads.
    # A detached SSH session would otherwise keep the Exporter waiting.
    nohup bash "$PWD/RunGuest.sh" "$PWD" "$platform" detached > "$log" 2>&1 < /dev/null &
    player=$!
    for attempt in {1..300}; do
        if grep -q "^$startup" "$log"; then
            cat "$log"
            printf 'Linux Player running in the build VM. PID %s. Log: %s\n' "$player" "$log"
            exit 0
        fi
        if ! kill -0 "$player" 2>/dev/null; then
            cat "$log" >&2
            exit 1
        fi
        sleep 0.1
    done
    kill -TERM "$player" 2>/dev/null || true
    cat "$log" >&2
    printf 'Timed out waiting for the Linux Player startup.\n' >&2
    exit 1
fi
if [[ "$frames" == detached ]]; then
    # Run the child in its own process group so termination also reaches Xvfb.
    directory="$PWD"
    set -m
    bash "$directory/RunGuest.sh" "$directory" "$platform" 0 &
    child=$!
    trap 'kill -TERM -- "-$child" 2>/dev/null || true; wait "$child" 2>/dev/null || true; rm -rf "$directory"' EXIT
    trap 'exit 143' TERM
    wait "$child"
    exit
fi
arguments=()
if [[ "$frames" != 0 ]]; then arguments+=("--frames=$frames"); fi
if [[ "$platform" == LinuxServer ]]; then
    exec ./GameSproutPlayer --headless "${arguments[@]}"
fi
export GAMESPROUT_LINUX_PREVIEW=1
if [[ -n "${DISPLAY:-}${WAYLAND_DISPLAY:-}" ]]; then
    exec ./GameSproutPlayer "${arguments[@]}"
fi
command -v xvfb-run >/dev/null || {
    printf 'A graphical Linux session or xvfb-run is required to run the Linux Player in this VM.\n' >&2
    exit 1
}
exec xvfb-run -a ./GameSproutPlayer "${arguments[@]}"
