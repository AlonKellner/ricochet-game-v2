#!/usr/bin/env bash
# Every gate before a commit, in order, with one timed summary:
#   the native build, the oracle tests, ctest (RelWithDebInfo), the GUT suite
#   (run_tests_parallel.sh), ctest under ASan+UBSan (alone: nothing else runs
#   meanwhile), and the goldens unchanged (git diff --stat bench/goldens).
# Every gate runs even after a failure; the exit status is nonzero if any failed.
# Logs: $LOGDIR (printed). JOBS bounds the compiles (default 4).
#
#   ./gates.sh          # or: make gates
set -uo pipefail
cd "$(dirname "$0")"
JOBS="${JOBS:-4}"
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

gate native make native JOBS="$JOBS"
gate oracle make -s oracle-test
gate ctest make ctest
gate gut env -u JOBS SKIP_ORACLE=1 ./run_tests_parallel.sh # every core (JOBS here bounds compiles)
gate asan make native-asan JOBS="$JOBS"
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
printf "%-10s %6d  %s   (logs: %s)\n" total "$total" "$([ $status = 0 ] && echo ok || echo FAIL)" "$LOGDIR"
exit $status
