#!/usr/bin/env bash
set -euo pipefail

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
project_root=$(cd -- "$script_dir/.." && pwd)

shopt -s globstar nullglob
for patch in "$project_root"/patches/**/*.patch; do
    relative_path=${patch#"$project_root/patches/"}
    target="$project_root/upstream/ballanced/$(dirname "$relative_path")"
    if [[ "$relative_path" == Source/RenderEngine/000[134567]-*.patch && ! -d "$target/src/CKRasterizer/CKBgfxRasterizer" ]]; then
        # The retired rasterizer and these patches are restored during CMake configuration.
        continue
    fi
    if git -C "$target" apply --check "$patch" >/dev/null 2>&1; then
        git -C "$target" apply "$patch"
    elif git -C "$target" apply -R --check "$patch" >/dev/null 2>&1; then
        :
    else
        printf 'Unable to apply patch: %s\n' "$relative_path" >&2
        exit 1
    fi
done
