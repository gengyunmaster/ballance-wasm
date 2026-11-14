#!/usr/bin/env bash
set -euo pipefail

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
project_root=$(cd -- "$script_dir/.." && pwd)

source "$project_root/.tools/emsdk/emsdk_env.sh" >/dev/null
export PATH="$project_root/.tools/cmake/bin:$project_root/.tools/ninja:$PATH"

"$project_root/scripts/apply-patches.sh"
cmake --preset wasm-release
cmake --build --preset wasm-release --parallel "${BUILD_PARALLELISM:-4}"
