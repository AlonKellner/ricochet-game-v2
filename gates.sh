#!/usr/bin/env bash
# The gates, in order, with one timed summary (docs/WORKFLOW.md):
#   LEVEL=commit (make gate): the core build, the oracle, ctest (RelWithDebInfo,
#     build-core), the extension build, the GUT suite, the goldens unchanged.
#   LEVEL=merge (make gates): the same, then ctest under ASan+UBSan (build-asan).
# Every gate runs even after a failure; the exit status is nonzero if any failed.
# Logs: $LOGDIR (printed). JOBS bounds the compiles (default: every core).
#
#   LEVEL=commit ./gates.sh     # or: make gate
#   LEVEL=merge ./gates.sh      # or: make gates
set -uo pipefail
cd "$(dirname "$0")"
LEVEL="${LEVEL:-merge}"
LOGDIR=$(mktemp -d "${TMPDIR:-/tmp}/ricochet_gates.XXXXXX")
declare -a NAMES SECS RESULTS
status=0

gate() {
	local name=$1
	shift
	local t0=$(date +%s)
	echo "== $name"
	if "$@" > "$LOGDIR/$name.log" 2>&1; then
		RESULTS+=("ok")
	else
		RESULTS+=("FAIL")
		status=1
		tail -30 "$LOGDIR/$name.log"
	fi
	NAMES+=("$name")
	SECS+=($(( $(date +%s) - t0 )))
}

goldens_unchanged() {
	local d
	d=$(git diff --stat bench/goldens)
	[ -z "$d" ] || { echo "$d"; return 1; }
}

gate core make core
gate oracle make -s oracle-test
gate ctest make ctest
gate native make native
gate gut env SKIP_ORACLE=1 SKIP_BUILD=1 ./run_tests_parallel.sh
if [ "$LEVEL" = merge ]; then
	gate asan make native-asan
fi
gate goldens goldens_unchanged

echo
printf "%-10s %6s  %s\n" gate secs result
total=0
for i in "${!NAMES[@]}"; do
	printf "%-10s %6d  %s\n" "${NAMES[$i]}" "${SECS[$i]}" "${RESULTS[$i]}"
	total=$((total + SECS[i]))
done
grep -h "Passing (" "$LOGDIR/gut.log" 2>/dev/null
grep -h "known finding" "$(sed -n 's/.*(logs: \(.*\))/\1/p' "$LOGDIR/gut.log" | head -1)"/*.log 2>/dev/null | sort | uniq -c
printf "%-10s %6d  %s   (LEVEL=%s, logs: %s)\n" total "$total" "$([ $status = 0 ] && echo ok || echo FAIL)" "$LEVEL" "$LOGDIR"
exit $status
