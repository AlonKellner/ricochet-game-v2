#!/usr/bin/env bash
# The Godot suite (Stage 119: the Godot host alone -- tests/test_adapter_smoke.gd,
# tests/test_session_replay.gd; the game is tested natively: make check, make pyhost).
# Exit status nonzero if a test fails, a script does not load, or an object leaks.
#
#   ./run_tests_parallel.sh
#   SKIP_ORACLE=1 ./run_tests_parallel.sh   # the oracle runs elsewhere (gates.sh)
#   SKIP_BUILD=1 ./run_tests_parallel.sh    # the extension is built already (gates.sh)
set -uo pipefail

GODOT="${GODOT_BIN:-godot}"
cd "$(dirname "$0")"

if [ -z "${SKIP_BUILD:-}" ] && ! GODOT_BIN="$GODOT" make native; then
	echo "!! native build failed (make native); not running the suite"
	exit 1
fi
if [ -z "${SKIP_ORACLE:-}" ] && ! make -s oracle-test; then
	echo "!! oracle tests failed (make oracle-test)"
	exit 1
fi

start=$(date +%s)
LOG=$(mktemp "${TMPDIR:-/tmp}/ricochet_gut.XXXXXX")
"$GODOT" --headless -s addons/gut/gut_cmdln.gd -gconfig= -gdir=res://tests -gprefix=test_ -gsuffix=.gd -gexit > "$LOG" 2>&1
clean=$(sed 's/\x1b\[[0-9;]*m//g' "$LOG")
pass=$(grep -E "^Passing Tests" <<< "$clean" | awk '{print $3}')
fail=$(grep -E "^Failing Tests" <<< "$clean" | awk '{print $3}')
status=0
if [ -z "$pass" ]; then
	echo "!! the suite did not finish (see $LOG)"
	status=1
fi
if grep -qE "SCRIPT ERROR|Parse Error|Failed to load" <<< "$clean"; then
	echo "!! script errors (see $LOG):"
	grep -E "SCRIPT ERROR|Parse Error|Failed to load" <<< "$clean" | head -10 | sed 's/^/   /'
	status=1
fi
if grep -qE "ObjectDB instances leaked|resources still in use" <<< "$clean"; then
	echo "!! objects leaked at exit (see $LOG)"
	status=1
fi
[ "${fail:-0}" != 0 ] && status=1
echo "Passing (the Godot host's suite): ${pass:-0}   Failing: ${fail:-0}   Wall: $(( $(date +%s) - start ))s (log: $LOG)"
exit $status
