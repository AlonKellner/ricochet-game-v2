# TDD 101: The journey is part of the shot (Stage 223)

Your report (2026-11-13, an F12 dump in pulling_3): "The preview says the object will reach the player, but in reality,
the object is blocked by the door. I want the preview to show the actual reality. Also, during a pull or grapple, I
expect that pressing space would speed the animation up just like arrow shooting ... the world should freeze during
those moments, and the consequence should be fully predetermined. However, in the situation I showed the door should
still close before the object is pulled through, because the detector has no object at that stage ... The simulation
which shows the preview should be the exact animation that's played eventually. Making the shot animation play faster
should persist to playing the grapple/pull animation."

The level: a box on a detector (`while`) holds `open`: door_1 (between the box and the player) open, door_2 closed.
The pull takes the box off the detector.

## What happened

- **The preview ran the journey under the flags the shot lands with** and read no detector on the way: the box went
  through door_1 to the player.
- **The live journey read the detectors every frame**, at the box's drawn position between ticks, and when the flags
  changed ran the rest of the journey again under them: the box met door_1, closed. So the two differed, and where a
  change fell depended on the frame rate.
- **The speed-up** (`speed_up`, any key) acted only while the arrow flew; a journey played at a tick per physics step.

## The fix

| Phase | Topic | Status |
|---|---|---|
| 223.1 | The run (`run_journey`) reads the detectors at every tick with the traveller where it is (`detect_into`: the session's detectors on given flags and insides, a moved body); a change of the flags is a new era of colliders from the next tick; the run holds each tick's flags, insides and era, and where it ends, settled, with the detectors on it there | Done |
| 223.2 | The shot plays the run with its flags (no re-run); the preview shows the same run, the flags it leaves a consequence of the journey's hit; smoothing among each tick's colliders | Done |
| 223.3 | The speed-up through the journey: any key in it, and one pressed in flight goes on (`flight_multiplier_` reset when a shot starts) | Done |
| 223.4 | Tests; the session record re-recorded after your approval; `make gate` | Done |
| 223.5 | The user's interactive sign-off | Done ("Works perfectly, approved, proceed", 2026-11-13) |

## Decisions

- **Detectors only.** The world is frozen during a journey (no physics, no cycle, no openings' swing), so the only
  thing that changes is the traveller, and the only rules that read it are the detectors. Spreading (tags) still runs
  once at the journey's end, as before.
- **Ticks, not frames.** The detectors read the raw tick positions (the run's), not the smoothed or the drawn ones: the
  same in the preview and in the shot, at any frame rate. A tick's flags apply when the shot reaches that tick.
- **The preview's consequences.** The flags the journey leaves (lasting ones, as the hits' are) are drawn as a
  consequence at the journey's hit -- a door outlined red where it closes, green where it opens, the goal opening.
- **Evidence (your approval, 2026-11-13).** Two of 62 session records changed, only in the preview's drawing:
  objectives__pull_box__play (two records) and objectives__platforms__play (one) now outline the goal opening (12
  dashed arcs) -- the pulled box crosses the `plate` detector on its way, which opens the goal; the trail and the ring
  are drawn as before, and every flight, position, flag and win is unchanged.

## Tests

`rcg_tests` "pull: the user's pulling_3 ...": the dump's state (the box where the shot hit it, on the detector) at 60,
144 and 7 frames per second -- the preview's trail ends against door_1 (x 1283.92), its predicted flags without
`open`; the shot ends where the preview said, without `open`; at 60 frames per second every frame of the journey is a
point of the preview's trail. "pull and grapple: the shot's speed-up goes on through its journey ...": sped up from the
first frame, the whole shot takes under a fifth of the frames, the same end; sped up in the journey, the journey too.
