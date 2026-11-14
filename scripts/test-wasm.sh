#!/usr/bin/env bash
set -euo pipefail

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
project_root=$(cd -- "$script_dir/.." && pwd)
player_dir="$project_root/build/wasm-release/upstream/ballanced/Source/Player/src"

for artifact in Player.js Player.wasm Player.data; do
    if [[ ! -f "$player_dir/$artifact" ]]; then
        printf 'Missing build artifact: %s\n' "$player_dir/$artifact" >&2
        exit 1
    fi
done

source "$project_root/.tools/emsdk/emsdk_env.sh" >/dev/null
cd "$player_dir"

set +e
output=$(SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy timeout --signal=TERM "${TEST_TIMEOUT:-15s}" node -r "$project_root/scripts/node-browser-shim.cjs" "$player_dir/Player.js" --verbose --root-path=/ --rasterizer null 2>&1)
status=$?
set -e
printf '%s\n' "$output"

if [[ $status -ne 0 && $status -ne 124 ]]; then
    exit "$status"
fi
if [[ "$output" != *"Game is playing."* ]]; then
    printf 'The game did not reach the playing state.\n' >&2
    exit 1
fi
if [[ "$output" != *"BML+ WASM module initialized: version 0.3.14"* ]]; then
    printf 'The BML+ WASM bootstrap did not initialize.\n' >&2
    exit 1
fi
if [[ "$output" != *"BML+ WASM static runtime initialized: 13 official mods"* ]]; then
    printf 'The BML+ WASM static mod runtime did not initialize.\n' >&2
    exit 1
fi
for registration in BallSticky:0.3.14 BMLModuls:0.3.14 CameraUtilities:0.3.14 \
                    DebugUtilities:0.3.2 DeformedWB:0.3.14 DualBallControl:0.3.14 \
                    DynamicFov:0.3.14 IVP:0.1.0 MapScripts:0.3.14 NewSpawn:0.0.2 \
                    Segment:2.1.6 SpiritTrail:0.3.14 TravelMode:0.3.14; do
    mod=${registration%%:*}
    version=${registration#*:}
    if [[ "$output" != *"BML+ WASM mod loaded: $mod $version"* ]]; then
        printf 'The official BML+ mod did not load: %s %s\n' "$mod" "$version" >&2
        exit 1
    fi
done
