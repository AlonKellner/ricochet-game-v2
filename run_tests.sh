#!/usr/bin/env bash
# Build the native extension, then run the whole GUT suite in one process.
# Extra arguments are passed to gut_cmdln.gd. GODOT_BIN overrides the binary.
set -euo pipefail

GODOT="${GODOT_BIN:-godot}"
cd "$(dirname "$0")"

# fail fast: without native/bin/ the tests/native/ files cannot pass
GODOT_BIN="$GODOT" make native
make -s oracle-test

"$GODOT" --headless -s addons/gut/gut_cmdln.gd "$@"
