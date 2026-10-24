#!/bin/sh
# make profile: the region build of one level under macOS `sample` (every
# plannable side at PLAYERS players, repeated until SECONDS have passed), then
# tools/profile_incl.py's self and inclusive tables. Single-threaded by default
# (THREADS=1): a per-cast profile; THREADS=8 profiles the schedule.
#   make profile LEVEL=data/levels/test_levels/crossings.json [THREADS=1] [SECONDS=8] [PLAYERS=6] [ONLY=plan] [PATS=a,b]
set -e
TOOL=${TOOL:-native/build-core/rc_tool}
LEVEL=${LEVEL:-data/levels/test_levels/crossings.json}
THREADS=${THREADS:-1}
SECONDS_=${SECONDS_:-8}
PLAYERS=${PLAYERS:-6}
OUT=${OUT:-build/perf}
mkdir -p "$OUT"
name=$(basename "$LEVEL" .json)-t$THREADS
if ! command -v sample >/dev/null 2>&1; then
	echo "!! make profile needs macOS 'sample' (Xcode command line tools)"; exit 2
fi
"$TOOL" regions "$LEVEL" --threads "$THREADS" --players "$PLAYERS" --reps 1000 ${ONLY:+--only "$ONLY"} > "$OUT/profile-$name.log" 2>&1 &
pid=$!
sleep 1
sample "$pid" "$SECONDS_" 1 -mayDie -file "$OUT/profile-$name.txt" > /dev/null 2>&1 || true
kill "$pid" 2>/dev/null || true
wait "$pid" 2>/dev/null || true
echo "profile: $OUT/profile-$name.txt ($LEVEL, $THREADS thread(s), $SECONDS_ s)"
python3 -I tools/profile_incl.py "$OUT/profile-$name.txt" ${PATS:+--pats "$PATS"}
