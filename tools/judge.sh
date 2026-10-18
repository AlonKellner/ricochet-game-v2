#!/usr/bin/env bash
# The goldens judge (Workflow W4): what the current tree changes against a
# reference build, dumped and classified, as a report for the user's approval.
#
#   make judge [REF=main] [RULES=stage106]   # or: tools/judge.sh
#   make rebless APPROVED=1                  # apply the judged re-bless lines
#
# 1. The reference: `git worktree add --detach` of REF under native/build-ref/
#    (a .gdignore keeps Godot out), rc_tests built there without the extension,
#    cached by commit.
# 2. The current rc_tests under RC_SWEEP_REBLESS=1 / RC_GOLDEN_REBLESS=1 lists
#    every changed sweep combo (REBLESS lines) and golden field (REBLESS-JSON
#    lines: fixtures, corpus, regions) -> $OUT/rebless.txt.
# 3. Both binaries dump the changed combos (RC_SWEEP_DUMP) -> old.txt / new.txt.
# 4. docs/research/judge.py classifies them -> $OUT/report.md (exit 1 while
#    anything is unclassified).
set -euo pipefail
cd "$(dirname "$0")/.."
REF="${REF:-main}"
RULES="${RULES:-}"
OUT="${OUT:-native/build-ref/judge}"
PY="${PY:-docs/research/.venv/bin/python}"
CMAKE="${CMAKE:-cmake}"
NEW="${NEW:-native/build-core/rc_tests}"

sha=$(git rev-parse --verify "$REF^{commit}")
short=$(git rev-parse --short "$sha")
mkdir -p native/build-ref "$OUT"
touch native/build-ref/.gdignore
src="native/build-ref/src-$short"
bld="native/build-ref/build-$short"
if [ ! -x "$bld/rc_tests" ]; then
	echo "== reference $REF ($short): worktree + rc_tests"
	[ -d "$src" ] || git worktree add --detach "$src" "$sha" > /dev/null
	gen=""
	command -v ninja > /dev/null && gen="-G Ninja"
	# the worktree has no submodule checkout: no extension, tests only
	$CMAKE -S "$src/native" -B "$bld" $gen -DCMAKE_BUILD_TYPE=RelWithDebInfo -DRC_BUILD_EXTENSION=OFF -DRC_BUILD_TESTS=ON > /dev/null
	$CMAKE --build "$bld" -j "$(sysctl -n hw.ncpu 2>/dev/null || nproc)" > /dev/null
fi
[ -x "$NEW" ] || { echo "!! $NEW missing: make core"; exit 1; }

echo "== the changed combos and fields (current tree)"
RC_SWEEP_REBLESS=1 RC_GOLDEN_REBLESS=1 "$NEW" \
	-tc="goldens: the sweep combos*,goldens: every stage fixture*,goldens: the corpus*,visibility: the regions' digest*" \
	> "$OUT/rebless.raw" 2>&1 || true
grep -E "^REBLESS(-JSON)?	" "$OUT/rebless.raw" > "$OUT/rebless.txt" || true
awk -F'\t' '$1 == "REBLESS" { print $2 "\t" $4 }' "$OUT/rebless.txt" | sort -u > "$OUT/combos.txt"
combos=$(wc -l < "$OUT/combos.txt" | tr -d ' ')
fields=$(grep -c "^REBLESS-JSON" "$OUT/rebless.txt" || true)
echo "   $combos sweep combos, $fields golden fields differ"

if [ "$combos" != 0 ]; then
	echo "== dumping them from both builds"
	RC_SWEEP_DUMP="$OUT/combos.txt" "$bld/rc_tests" -tc="goldens: the sweep combos*" 2>/dev/null | grep "^DUMP	" > "$OUT/old.txt" || true
	RC_SWEEP_DUMP="$OUT/combos.txt" "$NEW" -tc="goldens: the sweep combos*" 2>/dev/null | grep "^DUMP	" > "$OUT/new.txt" || true
else
	: > "$OUT/old.txt"
	: > "$OUT/new.txt"
fi

echo "== judging"
set +e
$PY docs/research/judge.py --old "$OUT/old.txt" --new "$OUT/new.txt" --rebless "$OUT/rebless.txt" --ref "$REF@$short" \
	${RULES:+--rules "$RULES"} --report "$OUT/report.md"
status=$?
set -e
echo "report: $OUT/report.md  (re-bless lines: $OUT/rebless.txt)"
exit $status
