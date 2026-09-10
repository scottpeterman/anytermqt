#!/usr/bin/env bash
# scripts/clean.sh
#
# Removes the build directory outright. The case that needs it is a cached
# toolchain or Qt: CMake records what it detected, so installing a matching Qt
# changes nothing until the cache is gone, and the version error survives the
# fix that should have cured it.
#
# For the narrower case -- Python disagreeing with the native app -- use
# scripts/regen-bindings.sh and keep the rest of the build.
#
# Does not source env.sh: this has to work when the environment is what is
# wrong.
set -euo pipefail

repo_root=$(cd -- "$(dirname -- "$0")/.." && pwd)
build_dir=${BUILD_DIR:-$repo_root/build}

if [ ! -d "$build_dir" ]; then
    echo "[clean] Nothing at $build_dir"
    exit 0
fi

echo "[clean] Removing $build_dir"
rm -rf -- "$build_dir"
echo "[clean] OK -- scripts/build.sh to start over"
