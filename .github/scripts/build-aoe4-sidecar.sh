#!/usr/bin/env bash
set -euo pipefail
[[ $(uname -s) == Darwin ]] || { echo "Build the native helper on macOS with Xcode and CMake >=3.27" >&2; exit 1; }
repo_root=$(cd "$(dirname "$0")/../.." && pwd)
work=${1:?usage: build-aoe4-sidecar.sh /path/to/new/work-directory}
[[ ! -e "$work" ]] || { echo "Work directory must not exist" >&2; exit 1; }
helper_repo=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["aoe4Sidecar"]["repo"])' "$repo_root/inputs.json")
helper_commit=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["aoe4Sidecar"]["commit"])' "$repo_root/inputs.json")
git clone --no-checkout "$helper_repo" "$work/source"
git -C "$work/source" checkout --detach "$helper_commit"
[[ "$(git -C "$work/source" rev-parse HEAD)" == "$helper_commit" ]]
cmake -S "$work/source" -B "$work/build" -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_BUILD_TYPE=Release
cmake --build "$work/build" --target x87sidecar --parallel
helper="$work/build/bin/x87sidecar"
[[ "$(lipo -archs "$helper")" == arm64 ]]
codesign --force --sign - "$helper"
codesign --verify --strict "$helper"
shasum -a 256 "$helper"
echo "Helper: $work/build/bin/x87sidecar; run --probe on the target Apple Silicon Mac"
