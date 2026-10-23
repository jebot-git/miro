#!/usr/bin/env bash
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
build=${MIRO_BUILD_DIR:-"$root/build"}
cargo_cache=${CARGO_HOME:-"$HOME/.cargo"}
args=(-DCMAKE_BUILD_TYPE=Release -DFLTK_DIR="${FLTK_DIR:-/opt/fltk-vr/toolkit/share/fltk}" -DMIRO_CARGO_HOME="$cargo_cache")
if [[ -n ${CARGO_TARGET_DIR:-} ]]; then args+=(-DMIRO_CARGO_TARGET="$CARGO_TARGET_DIR"); fi
if [[ -n ${BINDGEN_EXTRA_CLANG_ARGS:-} ]]; then args+=(-DMIRO_BINDGEN_ARGS="$BINDGEN_EXTRA_CLANG_ARGS"); fi
cmake -S "$root" -B "$build" "${args[@]}"
cmake --build "$build" --parallel "${MIRO_BUILD_JOBS:-2}"
