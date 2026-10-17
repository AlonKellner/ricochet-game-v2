#!/usr/bin/env bash
# Run the whole test suite in parallel Godot processes.
#
# The invariant sweeps (the slow tests) run as JOBS shards each: SWEEP_SHARD=i/n
# makes a sweep check only the player positions with index % n == i, so the
# shards together cover exactly the unsharded sweep (SweepRunner.shard).
# Every other test file runs once, in one process. Exit status is nonzero if
# any test fails or any process does not finish.
#
#   ./run_tests_parallel.sh            # JOBS = performance cores
#   JOBS=4 ./run_tests_parallel.sh
set -uo pipefail

GODOT="${GODOT_BIN:-godot}"
cd "$(dirname "$0")"

# fail fast: the tests/native/ files need native/bin/ and the import step
if ! GODOT_BIN="$GODOT" make native; then
	echo "!! native build failed (make native); not running the suite"
	exit 1
fi
# the oracle (the spec the fixtures come from) and the fixture schema
if ! make -s oracle-test; then
	echo "!! oracle tests failed (make oracle-test)"
	exit 1
fi

if [ -z "${JOBS:-}" ]; then
	JOBS=$(sysctl -n hw.perflevel0.physicalcpu 2>/dev/null || nproc 2>/dev/null || echo 4)
fi

SWEEPS=(test_invariant_sweep.gd test_invariant_combos.gd test_precision_invariant_sweep.gd test_stage107_level_sweep.gd test_stage108_level_sweep.gd test_visibility_sweep.gd)

LOGDIR=$(mktemp -d "${TMPDIR:-/tmp}/ricochet_tests.XXXXXX")
rm -f violations.*.shard*.json

is_sweep() {
	local f
	for f in "${SWEEPS[@]}"; do [ "$1" = "$f" ] && return 0; done
	return 1
}

# job list: "<label>|<shard>|<comma-separated res:// test files>"
# "others" is every non-sweep file, including the native extension tests
OTHERS=""
for f in tests/test_*.gd tests/native/test_*.gd; do
	b=$(basename "$f")
	is_sweep "$b" || OTHERS="${OTHERS:+$OTHERS,}res://$f"
done
{
	for f in "${SWEEPS[@]}"; do
		for ((i = 0; i < JOBS; i++)); do
			echo "${f%.gd}.shard$i|$i/$JOBS|res://tests/$f"
		done
	done
	echo "others||$OTHERS"
} > "$LOGDIR/jobs.txt"

# $1: the job's line number in jobs.txt (xargs -I limits the length of a
# substituted argument, so the job itself is read from the file)
run_job() {
	local label shard files
	IFS='|' read -r label shard files <<< "$(sed -n "${1}p" "$LOGDIR/jobs.txt")"
	SWEEP_SHARD="$shard" "$GODOT" --headless -s addons/gut/gut_cmdln.gd -gconfig= \
		-gtest="$files" -gexit > "$LOGDIR/$label.log" 2>&1
}
export -f run_job
export GODOT LOGDIR

start=$(date +%s)
echo "Running $(wc -l < "$LOGDIR/jobs.txt" | tr -d ' ') jobs on $JOBS processes (logs: $LOGDIR)"
seq 1 "$(wc -l < "$LOGDIR/jobs.txt" | tr -d ' ')" | xargs -P "$JOBS" -I{} bash -c 'run_job "$@"' _ {}

status=0
pass=0
fail=0
expected=$(wc -l < "$LOGDIR/jobs.txt" | tr -d ' ')
got=$(ls "$LOGDIR"/*.log 2>/dev/null | wc -l | tr -d ' ')
if [ "$got" != "$expected" ]; then
	echo "!! only $got of $expected jobs produced a log"
	status=1
fi
for log in "$LOGDIR"/*.log; do
	label=$(basename "$log" .log)
	clean=$(sed -E 's/\x1b\[[0-9;]*m//g' "$log")
	if ! grep -q "^Totals" <<< "$clean"; then
		echo "!! $label: did not finish (see $log)"
		status=1
		continue
	fi
	p=$(grep -E "^Passing Tests" <<< "$clean" | awk '{print $3}')
	f=$(grep -E "^Failing Tests" <<< "$clean" | awk '{print $3}')
	pass=$((pass + ${p:-0}))
	fail=$((fail + ${f:-0}))
	# a script that does not parse is skipped by GUT ("Ignoring script"):
	# its tests never run, so it fails the suite
	skipped=""
	for r in $(grep -oE "Ignoring script res://[^ ]+" <<< "$clean" | sed 's/Ignoring script res:\/\///' | sort -u); do
		grep -q "^extends GutTest" "$r" && skipped="${skipped:+$skipped
}res://$r"
	done
	if [ -n "$skipped" ]; then
		echo "!! $label: scripts not loaded (parse errors), their tests did not run:"
		echo "$skipped" | sed 's/^/   /'
		grep -E "Parse Error" <<< "$clean" | sort -u | head -10 | sed 's/^/   /'
		status=1
	fi
	# Stage 103: nothing may leak (side links hold no reference cycles)
	if grep -qE "instances were leaked at exit|resources still in use at exit" <<< "$clean"; then
		echo "!! $label: objects leaked at exit:"
		grep -E "leaked at exit|still in use at exit" <<< "$clean" | sed 's/^/   /'
		status=1
	fi
	if [ "${f:-0}" != "0" ] || grep -q "SCRIPT ERROR" <<< "$clean"; then
		echo "!! $label: ${f:-0} failing, script errors in the log"
		sed -n '/Run Summary/,$p' <<< "$clean" | grep -E "^res://|^- |\[Failed\]" | head -40
		status=1
	fi
done

# merge the shards' violation dumps (violation_viewer reads violations.json)
if ls violations.*.shard*.json > /dev/null 2>&1; then
	python3 - <<'EOF'
import glob, json, os
out = []
for p in sorted(glob.glob("violations.*.shard*.json")):
	try:
		out += json.load(open(p))
	except ValueError:
		pass
	os.remove(p)
json.dump(out, open("violations.json", "w"), indent=2)
EOF
fi

echo "Passing (sweep tests counted once per shard): $pass   Failing: $fail   Wall: $(( $(date +%s) - start ))s"
exit $status
