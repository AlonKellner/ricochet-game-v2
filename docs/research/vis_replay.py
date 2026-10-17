#!/usr/bin/env python3
"""Replay the visibility sweep's failing combos natively: one debug run per
(scene, plan, player), printing its structure-check and pin counts.

Usage: python3 docs/research/vis_replay.py <repo> [violations.json]
(run_tests_parallel.sh merges the shards' violations into violations.json; the
players are replayed at their exact float32 values, as the sweep saw them).
Needs native/build/rc_tests ("visibility debug" with RC_VIS_PIN)."""
import json, os, re, subprocess, sys

repo = sys.argv[1]
src = sys.argv[2] if len(sys.argv) > 2 else os.path.join(repo, 'violations.json')
data = json.load(open(src))
combos = {}
for v in data:
    if not v['violation'].startswith('VIS'):
        continue
    m = re.match(r'res://scenes/(playground|test_levels)/(\w+)\.tscn', v['scene'])
    scene = ('playground__' if m.group(1) == 'playground' else 'test_levels__') + m.group(2)
    plan = ','.join('%d%s' % (int(e['surface_id']), 'L' if int(e['side']) == 0 else 'R') for e in v['plan'])
    player = '%r,%r' % tuple(v['player_pos'])
    combos.setdefault((scene, plan, player), 0)
    combos[(scene, plan, player)] += 1
bad = 0
for (scene, plan, player), n in sorted(combos.items(), key=lambda kv: -kv[1]):
    env = dict(os.environ, RC_VIS_PIN='20', RC_VIS_SCENE=scene, RC_VIS_PLAN=plan, RC_VIS_PLAYER=player)
    out = subprocess.run([os.path.join(repo, 'native/build/rc_tests'), '-tc=visibility debug', '--no-skip'],
                         env=env, capture_output=True, text=True).stdout
    checks = re.search(r'checks: (\d+) violations', out)
    pin = re.search(r'pin: \d+ cursors, (\d+) mismatches', out)
    c = int(checks.group(1)) if checks else -1
    p = int(pin.group(1)) if pin else -1
    if c != 0 or p != 0:
        bad += 1
    print('%4d sweep | checks %3d pin %3d | %s [%s] %s' % (n, c, p, scene, plan, player), flush=True)
print('combos %d, still failing natively %d' % (len(combos), bad))
