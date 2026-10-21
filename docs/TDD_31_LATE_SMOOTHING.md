# TDD 31: Late means slow to compute; the path preview's morph (Stage 126)

The seventh stage of the smooth-frames program. The user asked (2026-10-21): "I do see morphing when planning
the circle inversion in `crossings` and moving around, check it and see if there's anything to learn from that.
Also, proceed to the path preview morph at the same time".

## Phase table

| Phase | Topic | Status |
|---|---|---|
| 126.1 | A region is smoothed by its computation time, not its latency; C API 1.4 `late_ms` | Done |
| 126.2 | The path preview morphs in when its exact climb was slower than 30 Hz (a diff of steps by the surfaces they hit) | Done |
| 126.3 | Docs, `make gates`, sign-off | Done (user, 2026-10-21) |

## 126.1 What the crossings morph taught

**Reproduced.** The tool walks like the Godot host: 120 Hz in real time, asynchronous, a 4 ms budget, the
presenter. On crossings with the inversion circle (19) planned:

- regions computed in about 27 ms (p95 30, max 47);
- they arrived about 42 ms after their request (p95 50);
- the morph ran on 592 of 600 frames.

**The lesson.** Stage 124.6 timed a region from its request to its arrival. While the player walks, a request
waits behind the region already computing (a slow region is not cancelled, or the player would never see one),
so latency runs about 1.5× the computation. The user's rule was about the calculation ("make the morph active
only if the calculation is slower than 30 Hz"), and a 27 ms calculation is faster than that. The presenter now
smooths a region only when both hold:

- it missed its frame (it was seen pending);
- its computation (`Result::build_usec`) took `late_seconds` (1/30 s) or more.

With that rule, the morph ran on 52 frames of 600: the regions that really took longer than 33 ms. C API 1.4
adds `rc_game_config.late_ms` (0: 1000/30).

**A second lesson: the benchmark missed this case.** The stage tool planned the first plannable side. With the
inversion circle planned, a crossings region takes 27 ms, not 18. The plan that matters is the one players
make, so later measurements should cover each level's projectives and inversions.

## 126.2 The path preview's morph

**When.** The preview is computed in its frame, in float64. Only when the ladder must climb is the float64
path shown provisionally while the exact one is computed in the background (Stage 121). The path morphs in only
when both hold:

- it was shown provisionally (it missed its frame);
- its exact climb took `late_seconds` or more.

Any other path is the session's own. The same rule as the region's, by computation.

**How** (the user's design for the region, applied to the path; `rcg::path_morph`):

- **Aligned like a diff.** The old and new paths' steps are aligned by the surfaces they hit (the longest
  common subsequence of their hit surfaces), so points move along the same surfaces.
- **A paired step** interpolates its start, via and end: a segment, or an arc through three points. Every frame
  is straight lines and circular arcs.
- **A partnerless step** grows out of, or shrinks into, the point the other path had reached there, so the path
  stays joined at every moment.
- **Kind and colour** switch at the midpoint, with no blending. A step to or from infinity (a direction, not
  points) switches whole.
- **Timing.** The morph lasts 80 ms (`PresentConfig::path_seconds`). A path that arrives on time during a morph
  retargets from what is shown over the time left.

The session exposes `preview_steps`, `preview_seconds` (the shown exact preview's climb) and `paint_steps`. The
presenter paints its frames with the painter the session's own path uses, so draw lists and sessions are
unchanged.

**How often.** On inversion_grid, the one shipped level whose previews still climb (TDD_28), 7 of 60 cursor
positions climbed. At 4 of them the exact path differs from the provisional one (33 steps each), and those
morph when their climb is slow. A climb takes milliseconds there, so in play the morph is a fallback that
should rarely show.

## Results

| Case | Before | After |
|---|---|---|
| crossings, inversion planned, walking at 120 Hz | morphing 592 frames of 600 | 52 frames (regions over 33 ms) |
| a preview whose exact climb is slow | the exact path replaced the provisional one at once | morphed in, 80 ms |

## Findings

| # | Finding | Status |
|---|---|---|
| F8 (TDD_27) | The preview arrives later than its input and replaces what was shown at once | Resolved: the path's morph (126.2) |
| F18 | Latency is about 1.5× the computation while walking (a request waits behind the running region) | Recorded: the smoothing rule uses the computation (126.1); a faster region is the cure |
| F19 | crossings with its inversion circle planned: about 27 ms a region (the stage tool's default plan measured 18) | Open: the worst planned region on the shipped levels |

## Tests

| Test | What it checks |
|---|---|
| `present: a region faster than 30 Hz is drawn as it is, though it missed its frame` | a 1 ms region arriving 66 ms after its request (the presenter's clock) is neither followed nor morphed |
| `present: the path's diff starts and ends on the two paths, stays joined, and grows a new step out of its point` | the diff on synthetic paths |
| `present: a preview whose exact climb is slower than 30 Hz morphs in; a faster one is drawn as it arrives` | inversion_grid's provisional-then-exact path |
| `test_an_animated_game_smooths_only_a_late_region` (Python host) | C API 1.4 `late_ms` |
