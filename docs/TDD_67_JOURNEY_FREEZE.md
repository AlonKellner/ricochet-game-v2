# TDD 67: The journey preview froze the game (Stage 163)

Your report (2026-10-25): "in the same scene where I plan the grapple, move the cursor around, and eventually the
game freezes up and is not responsive anymore."

## What happened

Stage 159 previews a grapple's journey: at every cursor move the preview runs the journey's motion
(`Session::journey_move`) tick by tick from the player to where it ends. A tick moves the traveller up to 15 px
along the path: a whole segment, or part of one. After a partial move the loop subtracted the distance moved,
`left -= len * (left / len)`, from the tick's remaining distance. That is zero in exact arithmetic, but rounding
can leave a tiny positive remainder, and the loop went on with it. Each further pass shrank it, down to the
smallest subnormal double, `2^-1074`. There `left / len` underflows to 0: the traveller does not move, `left`
does not shrink, and the loop never ends. The preview runs on the game's thread, so the game froze.

At grapple_gap with the grapple planned the cursor at (1393.7, 222.1) hits it (found by sweeping the cursor; a
sample of the hung process showed `Session::arrow_states -> journey_move -> kinematics::sweep_fraction`). The
live journey ran the same loop before Stage 159, once per shot; the preview made it run at every cursor
position, so the rare case was found by moving the cursor around.

## The fix

| Phase | Topic | Status |
|---|---|---|
| 163.1 | A partial move ends the tick (its distance is used up) -- no subtraction, no residue; a whole segment subtracts its length and goes on | Done |
| 163.2 | The path marks' loop bounded (a step far off the drawn bounds draws none); tests; `make gate` | Done |
| 163.3 | The user's interactive sign-off | Open |

The live journey uses the same function, so it is fixed too. Every session record replays unchanged: the live
journeys end where they did. A sweep of about 220,000 cursor positions over the scene (36,000 of them previewing a
journey) runs in two seconds with no hang.

## Tests

`rcg_tests` "a journey's motion ends its tick at a partial move ...": the cursor that hung -- the preview returns, and
the live journey ends where the preview said.
