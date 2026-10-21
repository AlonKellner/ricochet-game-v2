#!/usr/bin/env bash
# The gates, in order, with one timed summary (docs/WORKFLOW.md):
#   LEVEL=commit (make gate): the core build, the oracle, ctest (RelWithDebInfo,
#     build-core; the native sweeps at one player in 4), the extension build,
#     the level dumps current, the sweep manifests current, the Python host
#     on the C API (tests/host_py), the Godot host's suite, the goldens unchanged.
#     The result cache (tools/gate_cache.py): a ctest entry, the Python host or
#     the Godot suite whose inputs are the same as when it last passed is not
#     run again (GATE_CACHE=0: run everything; the Godot suite's inputs are the
#     host's, see godot_gate); the asynchronous and C API
#     session replays and the Python host's play every 4th session (the
#     synchronous native replay plays them all).
#   LEVEL=merge (make gates): the same with every sweep player and no cache,
#     then ctest under ASan+UBSan (build-asan).
#   SKIP_GODOT=1: every engine-free gate (no extension build, no Godot suite):
#     what a port to another engine runs (docs/PORTING.md).
# Every gate runs even after a failure; the exit status is nonzero if any failed.
# ctest, the Python host and the Godot extension + suite run side by side (their
# logs are separate; each gate's seconds overlap, the total is the wall time).
# Logs: $LOGDIR (printed). JOBS bounds the compiles (default: every core).
#
#   LEVEL=commit ./gates.sh     # or: make gate
#   LEVEL=merge ./gates.sh      # or: make gates
#   SKIP_GODOT=1 make gate
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

# a gate in the background (gate_join collects it, in the summary's order)
declare -a BG_NAMES BG_PIDS BG_T0
gate_bg() {
	local name=$1
	shift
	echo "== $name (in the background)"
	("$@" > "$LOGDIR/$name.log" 2>&1; rc=$?; date +%s > "$LOGDIR/$name.end"; exit $rc) &
	BG_NAMES+=("$name")
	BG_PIDS+=($!)
	BG_T0+=($(date +%s))
}
gate_join() {
	local i
	for i in "${!BG_PIDS[@]}"; do
		if wait "${BG_PIDS[$i]}"; then
			RESULTS+=("ok")
		else
			RESULTS+=("FAIL")
			status=1
			tail -30 "$LOGDIR/${BG_NAMES[$i]}.log"
		fi
		NAMES+=("${BG_NAMES[$i]}")
		SECS+=($(( $(cat "$LOGDIR/${BG_NAMES[$i]}.end" 2>/dev/null || date +%s) - BG_T0[i] )))
	done
	BG_NAMES=() BG_PIDS=() BG_T0=()
}

# the result cache: at the commit level only (tools/gate_cache.py)
cached() {
	if [ "$LEVEL" = commit ]; then
		python3 -I tools/gate_cache.py "$@"
	else
		GATE_CACHE=0 python3 -I tools/gate_cache.py "$@"
	fi
}

# the committed sweep manifests are what data/sweeps.json expands to (Stage 116: rc_tool)
manifests_current() {
	local d
	d=$(mktemp -d "${TMPDIR:-/tmp}/ricochet_manifests.XXXXXX")
	make -s manifests MANIFEST_DIR="$d" || return 1
	if ! diff -r bench/sweeps "$d"; then
		echo "!! bench/sweeps is stale: make manifests"
		return 1
	fi
	rm -rf "$d"
}

# the committed level dumps are what the level data builds (Stage 115: rc_tool)
dumps_current() {
	local d
	d=$(mktemp -d "${TMPDIR:-/tmp}/ricochet_dumps.XXXXXX")
	make -s dumps DUMPS_DIR="$d" > /dev/null || return 1
	if ! diff -r bench/scenes "$d"; then
		echo "!! bench/scenes is stale: make dumps"
		return 1
	fi
	rm -rf "$d"
}

goldens_unchanged() {
	local d
	d=$(git diff --stat bench/goldens bench/sessions bench/scenes)
	[ -z "$d" ] || { echo "$d"; return 1; }
}

# the Godot extension, then the Godot host's suite. The commit level keys the
# suite on the host (its project, its adapter, rc_api.h, the data, Godot), not
# on the game inside the extension: a game-only change is checked by the C++
# and Python hosts on the C API at a commit, by all four hosts at a merge.
godot_gate() {
	make native || return 1
	local godot_bin
	godot_bin=$(command -v "${GODOT_BIN:-godot}" || true)
	cached step gut --inputs engines/godot native/hosts/godot native/capi/include run_tests_parallel.sh data bench/sessions \
		${godot_bin:+"file:$godot_bin"} -- env SKIP_ORACLE=1 SKIP_BUILD=1 ./run_tests_parallel.sh
}

wall0=$(date +%s)
gate core make core
gate oracle make -s oracle-test
gate dumps dumps_current
gate manifests manifests_current
# side by side: ctest, the Python host, the Godot extension and suite
if [ "$LEVEL" = commit ]; then
	# the native sweeps at one player in 4; the asynchronous and C API session replays every 4th session
	gate_bg ctest env RC_SWEEPS_STRIDE=4 RC_SESSIONS_SAMPLE=4 python3 -I tools/gate_cache.py ctest native/build-core
else
	gate_bg ctest make ctest
fi
gate_bg pyhost cached step pyhost --inputs tests/host_py data bench/sessions 'file:native/build-core/libricochet_capi.*' -- \
	env RC_PYHOST_SESSION_STRIDE=$([ "$LEVEL" = commit ] && echo 4 || echo 1) make -s pyhost
if [ -z "${SKIP_GODOT:-}" ]; then
	gate_bg godot godot_gate
fi
gate_join
if [ "$LEVEL" = merge ]; then
	gate asan make native-asan
fi
gate goldens goldens_unchanged

echo
printf "%-10s %6s  %s\n" gate secs result
for i in "${!NAMES[@]}"; do
	printf "%-10s %6d  %s\n" "${NAMES[$i]}" "${SECS[$i]}" "${RESULTS[$i]}"
done
grep -h "Passing (\|gate_cache:" "$LOGDIR"/{ctest,pyhost,godot}.log 2>/dev/null
printf "%-10s %6d  %s   (wall; LEVEL=%s%s, logs: %s)\n" total "$(( $(date +%s) - wall0 ))" "$([ $status = 0 ] && echo ok || echo FAIL)" "$LEVEL" "${SKIP_GODOT:+, SKIP_GODOT}" "$LOGDIR"
exit $status
