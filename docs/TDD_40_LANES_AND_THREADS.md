# TDD 40: Lanes by passes, and thread scaling (Stage 135)

After Stage 134 the user asked for F22 (thread scaling, TDD_33), and at the same time for a fix to the preview's
side-by-side lanes.

## Phase table

| Phase | Topic | Status |
|---|---|---|
| 135.1 | The side-by-side lanes by passes (the user's F12 report) | Done (approved: 742 records' draw lists in 43 sessions) |
| 135.2 | F22: what does not scale | Done (found: the system allocator's contention) |
| 135.3 | Fewer allocations per cast | Done (bit-exact; 36% fewer) |
| 135.4 | The compute threads' block cache | Done (bit-exact) |
| 135.5 | A split region's timings, per-cast `getenv` | Done |

## 135.1 Lanes by passes

The user's F12 dump (open_room_playable): a shot runs back and forth on one line, five laps through infinity. The
lanes' offset changed after every non-significant crossing. It took one value up to the cursor, another after it, a
third after the next crossing, and leaned to one side.

The cause: lanes were assigned per step. Every step in a group of overlapping steps got its own lane, and a pass along
the line is many steps: it breaks at the cursor and at every crossing. In this shot, 43 steps on one line made 43
lanes. The ray's origin (the player, passed on every lap) breaks every lap there too. No step crossed that point, so
the line split into two groups with 5 and 6 lanes and different centring.

Now (`side_by_side`, session_draw.cpp; GAME_SPEC 4.3):

- **A pass** is a run of one walk's consecutive steps that go on from one another along one carrier, the same way
  round, through the cursor, the crossings that fire nothing and the origin. No step of a pass lies on another. A
  wrap, a turn onto another carrier, the way back or a second lap starts a new pass.
- **Groups** are passes drawn on one another, transitively.
- **Lanes:** in path order, each pass takes the lowest lane no overlapping pass holds, so a group has as many lanes
  as its passes run together. They are 3.5 px apart and centred on the path; the planned walk's passes take the left
  lanes, the physical walk's the right.

Test: `present: side by side by passes -- one lane per pass, packed, centred`, the user's shot. It has six passes, at
±1.75, ±5.25 and ±8.75 px, each constant along its whole length. `rcg::lane_offsets` exposes the offsets. Painting
costs the same as before (pre_inverted's cursor orbit 0.14-0.26 ms).

## 135.2 What does not scale (F22)

Measured over every single-entry plan of crossings: the region's casts, the CPU time summed over the threads
(a scratch probe on `Result::batch_sum_usec`).

| Threads | Wall | Casts' CPU |
|---|---|---|
| 1 | 520 ms | 380 ms |
| 2 | 320 ms | 440 ms |
| 6 | 150 ms | 530 ms |
| 8 | 145 ms | 660 ms |

The work does not grow, but each cast slows as threads are added. Ruled out:

- **The efficiency cores:** at 6 threads nearly every cast runs on the performance cores (`pthread_cpu_number_np`), and
  a higher QoS class changed nothing.
- **Splitting the tracers' caches:** one thread rotating its casts over 6 tracers costs nothing.
- **The chip** (clock, shared caches): six independent single-threaded processes slow by 6% each, not 30%.
- **Locks and atomics:** the memo's 24k hits and 67k lookups are negligible.

Found: the macOS system allocator. A cast made about 200 small allocations. A crude lock-free thread cache in a probe
brought 6 threads down to the one-thread cost plus the chip's 6% (wall 147 → 112 ms).

## 135.3 Fewer allocations

Counted by call site, with a scratch `operator new` keyed by return address:

- `build_simple`: the aim chain's frames are a trie in one array (each its parent and one surface), not four maps
  keyed by the stacks' spelling;
- the incidence memo: open addressing in one table, not a node per entry;
- `unreliable_index` builds no vector; `collect`, `estimate`, `plan_effects`, `build_chain_direct` and
  `significant_events` reserve their sizes.

The result: 4.03 → 2.60 million allocations on the set. Everything is bit-exact: goldens, region digests and session
records are unchanged.

## 135.4 The compute threads' block cache

`rc/thread_alloc.hpp`, `core/src/thread_alloc.cpp`: the core's global `operator new` / `delete`.

- On a compute thread (the pool's workers, and the caller within a batch) a freed block goes to that thread's free
  list for its size: 16-byte classes up to 1 KiB, then 2, 4 and 8 KiB, at most 64 KiB per class. Allocations come
  back from those lists.
- Elsewhere, and for larger blocks, it is `malloc` / `free` as before.
- Every block is a real `malloc` block, classed by `malloc_size` when freed, so a block allocated anywhere (libc++'s
  own code included) may be freed anywhere.
- Off under the sanitizers, which must see every free, and where the platform has no `malloc_size`.

At 6 threads the casts' CPU went 530 → 430 ms. The extension, the C API library, the Godot suite and ASan+UBSan all
pass.

## 135.5 Small things

- A split region's reported timings were its first half's alone; `join_halves` now sums both halves' phases and
  batches.
- `trace_cast` and the tie trace read `getenv` once, not per cast: `getenv` takes the environment's lock.

## The result

`rc_tool bench --all` against main (f63d5d2), the work counts identical:

- plan clicks 7-20% faster (crossings 7.7 → 6.5 ms, inversion_grid 5.8 → 4.8, two_sided 3.8 → 3.1, portals
  3.7 → 3.1);
- player orbits 5-20% faster.

At 6 threads a cast now costs about 12% more than on one thread, against about 36% before. F22 is closed: what
remains is the allocations the cast's data flow makes (about 130 per cast), and the chip.
