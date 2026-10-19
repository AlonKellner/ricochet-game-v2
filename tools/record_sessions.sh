#!/usr/bin/env bash
# Records the session replay golden (docs/research/session_digest.md) into a
# directory, in JOBS parallel Godot processes (scripts/testing/record_sessions.gd
# --shard i/JOBS). Exit status nonzero if any shard failed (its log tail printed).
#
#   [RECORD_ARGS="--draw --full"] tools/record_sessions.sh <out dir> [jobs]
set -uo pipefail
cd "$(dirname "$0")/.."
OUT="$1"
JOBS="${2:-$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)}"
GODOT="${GODOT_BIN:-godot}"
mkdir -p "$OUT"
LOGS=$(mktemp -d "${TMPDIR:-/tmp}/ricochet_record.XXXXXX")
pids=()
for ((i = 0; i < JOBS; i++)); do
	"$GODOT" --headless --fixed-fps 60 --path . -s res://scripts/testing/record_sessions.gd \
		-- --out "$OUT" --shard "$i/$JOBS" ${RECORD_ARGS:-} > "$LOGS/shard$i.log" 2>&1 &
	pids+=($!)
done
status=0
for i in "${!pids[@]}"; do
	if ! wait "${pids[$i]}"; then
		status=1
		echo "!! shard $i/$JOBS failed:"
		tail -20 "$LOGS/shard$i.log"
	fi
done
[ $status = 0 ] && rm -rf "$LOGS"
exit $status
