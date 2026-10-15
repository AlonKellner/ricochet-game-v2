# Stage 88b evidence: the `infinity` event in the committed corpora

Produced on 2026-10-15 from two scratch copies of the repository at `8d96433` (Stage 99):
the oracle with `EMIT_INFINITY = True` against the oracle as committed (`False`). Both copies
regenerated `bench/fixtures/*.json` (the `gen_fixtures_stage8*.py` generators),
`bench/scenes_corpus/expected` (`gen_scenes_corpus.py`), `bench/expected` and `bench/scenarios`
(`regen_bench_v2.py`) and `bench/stress`. The `False` copy reproduces every committed file
byte-identically, so every difference below is the infinity event and nothing else.

Every difference is one of three kinds; no other kind occurs:

- **pure insertion**: `infinity` events (surface_id -3, no point, `applied = false`) appear where a
  straight beam wraps; removing them gives the old event list exactly.
- **cut earlier**: the trace already reached MAX_HITS (33 events, the 33rd a `max`); the inserted
  `infinity` events count, so the old tail falls beyond MAX_HITS and the `max` event lands on the
  next old crossing (its point equals that crossing's point).
- **cut at infinity**: the MAX_HITS cut lands on an `infinity` event itself: the `max` event carries
  surface_id -3 and no point (the oracle's spec), or the old trace's last events were dropped.

| Corpus | files | changed | event lists | identical | pure insertion | cut earlier | cut at infinity | infinity events | old events now beyond MAX_HITS |
|---|---|---|---|---|---|---|---|---|---|
| fixtures | 10 | 6 | 749 | 723 | 19 | 7 | 0 | 57 | 38 |
| scenes_corpus | 348 | 211 | 1392 | 1136 | 218 | 34 | 4 | 334 | 92 |
| expected | 161 | 22 | 322 | 287 | 0 | 32 | 3 | 52 | 52 |
| stress | 40 | 0 | 80 | 80 | 0 | 0 | 0 | 0 | 0 |

No `applied = true` infinity event appears: the committed corpora hold no ∞ plan entry
(`stage88_infinity.json`, which does, is unchanged: its generator sets the switch itself).
The 40 stress scenarios (depth 16-25, all circle beams) are unchanged: circle beams never wrap.

## Every cut, by corpus

Format: file and event list, kind, number of old events now beyond MAX_HITS (the inserted
infinity events in parentheses are what pushed them out).

### fixtures (7)

- `stage81_rules.json.fixtures[departure_rehit_straight].expected`: cut earlier (max at next crossing); 16 old event(s) dropped
- `stage81_rules.json.fixtures[line_through_inversion_center].expected`: cut earlier (max at next crossing); 5 old event(s) dropped
- `stage82_portals.json.fixtures[portal_pop_departure_rehit].expected`: cut earlier (max at next crossing); 11 old event(s) dropped
- `stage83_projective.json.fixtures[player_at_inversion_center_planned].expected`: cut earlier (max at next crossing); 1 old event(s) dropped
- `stage83_projective.json.fixtures[cursor_at_inversion_center].expected`: cut earlier (max at next crossing); 1 old event(s) dropped
- `stage83_projective.json.fixtures[cursor_at_inversion_center_no_plan].expected`: cut earlier (max at next crossing); 3 old event(s) dropped
- `stage87_click_forks.json.fixtures[cn_back_inside_click_near_planned].expected`: cut earlier (max at next crossing); 1 old event(s) dropped

### scenes_corpus (38)

- `expected/main__02.json.planned`: cut earlier (max at next crossing); 1 old event(s) dropped
- `expected/playground__directional_arcs__11.json.planned`: cut earlier (max at next crossing); 3 old event(s) dropped
- `expected/playground__inversion_grid__00.json.planned`: cut earlier (max at next crossing); 2 old event(s) dropped
- `expected/playground__inversion_grid__00.json.physical`: cut earlier (max at next crossing); 1 old event(s) dropped
- `expected/playground__inversion_grid__03.json.physical`: cut earlier (max at next crossing); 1 old event(s) dropped
- `expected/playground__inversion_grid__06.json.planned`: cut earlier (max at next crossing); 1 old event(s) dropped
- `expected/playground__inversion_grid__08.json.planned`: cut earlier (max at next crossing); 1 old event(s) dropped
- `expected/playground__inversion_grid__09.json.planned`: cut earlier (max at next crossing); 1 old event(s) dropped
- `expected/playground__inversion_grid__09.json.physical`: cut earlier (max at next crossing); 1 old event(s) dropped
- `expected/playground__inversion_grid__11.json.planned`: cut earlier (max at next crossing); 2 old event(s) dropped
- `expected/playground__inversion_grid__11.json.physical`: cut earlier (max at next crossing); 2 old event(s) dropped
- `expected/playground__plannable_inversion__07.json.planned`: cut earlier (max at next crossing); 2 old event(s) dropped
- `expected/playground__projective__02.json.planned`: cut earlier (max at next crossing); 2 old event(s) dropped
- `expected/playground__projective__03.json.planned`: cut at infinity; 1 old event(s) dropped
- `expected/playground__projective__04.json.planned`: cut earlier (max at next crossing); 2 old event(s) dropped
- `expected/playground__projective__06.json.planned`: cut earlier (max at next crossing); 2 old event(s) dropped
- `expected/playground__projective_arcs__04.json.planned`: cut earlier (max at next crossing); 3 old event(s) dropped
- `expected/test_levels__circle_no_walls__00.json.planned`: cut earlier (max at next crossing); 2 old event(s) dropped
- `expected/test_levels__circle_no_walls__00.json.physical`: cut earlier (max at next crossing); 2 old event(s) dropped
- `expected/test_levels__circle_no_walls__01.json.planned`: cut earlier (max at next crossing); 4 old event(s) dropped
- `expected/test_levels__circle_no_walls__01.json.physical`: cut earlier (max at next crossing); 4 old event(s) dropped
- `expected/test_levels__circle_no_walls__03.json.planned`: cut at infinity (max at inf); 4 old event(s) dropped
- `expected/test_levels__circle_no_walls__04.json.planned`: cut earlier (max at next crossing); 4 old event(s) dropped
- `expected/test_levels__circle_no_walls__04.json.physical`: cut earlier (max at next crossing); 4 old event(s) dropped
- `expected/test_levels__circle_no_walls__05.json.planned`: cut earlier (max at next crossing); 3 old event(s) dropped
- `expected/test_levels__circle_no_walls__05.json.physical`: cut earlier (max at next crossing); 3 old event(s) dropped
- `expected/test_levels__circle_no_walls__06.json.physical`: cut at infinity (max at inf); 4 old event(s) dropped
- `expected/test_levels__circle_no_walls__07.json.physical`: cut earlier (max at next crossing); 3 old event(s) dropped
- `expected/test_levels__circle_no_walls__08.json.planned`: cut earlier (max at next crossing); 4 old event(s) dropped
- `expected/test_levels__circle_no_walls__09.json.planned`: cut earlier (max at next crossing); 3 old event(s) dropped
- `expected/test_levels__circle_no_walls__09.json.physical`: cut earlier (max at next crossing); 5 old event(s) dropped
- `expected/test_levels__circle_no_walls__11.json.planned`: cut earlier (max at next crossing); 4 old event(s) dropped
- `expected/test_levels__circle_no_walls__11.json.physical`: cut earlier (max at next crossing); 4 old event(s) dropped
- `expected/test_levels__full_circle_mirror__03.json.planned`: cut earlier (max at next crossing); 1 old event(s) dropped
- `expected/test_levels__full_circle_mirror__04.json.planned`: cut earlier (max at next crossing); 1 old event(s) dropped
- `expected/test_levels__full_circle_mirror__11.json.planned`: cut earlier (max at next crossing); 1 old event(s) dropped
- `expected/test_levels__three_mirrors__07.json.planned`: cut at infinity; 1 old event(s) dropped
- `expected/test_levels__two_sided_mirror__00.json.planned`: cut earlier (max at next crossing); 3 old event(s) dropped

### expected (34)

- `grid30_chain_d01_073.json.planned`: cut earlier (max at next crossing); 4 old event(s) dropped
- `grid30_chain_d01_073.json.physical`: cut earlier (max at next crossing); 4 old event(s) dropped
- `grid30_chain_d01_088.json.planned`: cut earlier (max at next crossing); 3 old event(s) dropped
- `grid30_chain_d01_088.json.physical`: cut earlier (max at next crossing); 3 old event(s) dropped
- `grid30_chain_d02_100.json.planned`: cut earlier (max at next crossing); 2 old event(s) dropped
- `grid30_chain_d02_100.json.physical`: cut earlier (max at next crossing); 2 old event(s) dropped
- `grid30_chain_d03_018.json.planned`: cut earlier (max at next crossing); 1 old event(s) dropped
- `grid30_chain_d03_018.json.physical`: cut earlier (max at next crossing); 1 old event(s) dropped
- `grid30_chain_d03_026.json.planned`: cut earlier (max at next crossing); 1 old event(s) dropped
- `grid30_chain_d03_026.json.physical`: cut earlier (max at next crossing); 1 old event(s) dropped
- `grid30_chain_d04_082.json.physical`: cut earlier (max at next crossing); 1 old event(s) dropped
- `grid30_chain_d06_061.json.physical`: cut earlier (max at next crossing); 1 old event(s) dropped
- `grid60_chain_d01_025.json.planned`: cut at infinity (max at inf); 1 old event(s) dropped
- `grid60_chain_d01_025.json.physical`: cut earlier (max at next crossing); 2 old event(s) dropped
- `grid60_chain_d01_063.json.planned`: cut earlier (max at next crossing); 1 old event(s) dropped
- `grid60_chain_d01_063.json.physical`: cut earlier (max at next crossing); 1 old event(s) dropped
- `grid60_chain_d02_020.json.planned`: cut earlier (max at next crossing); 1 old event(s) dropped
- `grid60_chain_d02_020.json.physical`: cut earlier (max at next crossing); 1 old event(s) dropped
- `grid60_chain_d02_058.json.planned`: cut at infinity (max at inf); 1 old event(s) dropped
- `grid60_chain_d02_058.json.physical`: cut earlier (max at next crossing); 2 old event(s) dropped
- `grid60_chain_d03_072.json.planned`: cut earlier (max at next crossing); 1 old event(s) dropped
- `grid60_chain_d03_101.json.planned`: cut earlier (max at next crossing); 1 old event(s) dropped
- `grid60_chain_d04_028.json.planned`: cut earlier (max at next crossing); 1 old event(s) dropped
- `grid60_chain_d04_139.json.planned`: cut earlier (max at next crossing); 1 old event(s) dropped
- `grid60_chain_d04_156.json.planned`: cut earlier (max at next crossing); 1 old event(s) dropped
- `grid60_chain_d04_156.json.physical`: cut earlier (max at next crossing); 1 old event(s) dropped
- `grid60_chain_d05_002.json.planned`: cut earlier (max at next crossing); 1 old event(s) dropped
- `grid60_mixed_d01_107.json.planned`: cut earlier (max at next crossing); 3 old event(s) dropped
- `grid60_mixed_d01_107.json.physical`: cut earlier (max at next crossing); 3 old event(s) dropped
- `grid60_mixed_d01_126.json.planned`: cut earlier (max at next crossing); 1 old event(s) dropped
- `grid60_mixed_d01_126.json.physical`: cut earlier (max at next crossing); 1 old event(s) dropped
- `grid60_mixed_d02_034.json.planned`: cut earlier (max at next crossing); 1 old event(s) dropped
- `grid60_mixed_d02_034.json.physical`: cut earlier (max at next crossing); 1 old event(s) dropped
- `grid90_chain_d01_150.json.planned`: cut earlier (max at next crossing); 1 old event(s) dropped

## Which GUT tests this touches

The fixture cases that change are the seven MAX_HITS cases above (`departure_rehit_straight`,
`line_through_inversion_center`, `portal_pop_departure_rehit`, `player_at_inversion_center_planned`,
`cursor_at_inversion_center`, `cursor_at_inversion_center_no_plan`, `cn_back_inside_click_near_planned`)
plus the 19 pure insertions; the fixture-replay tests compare event lists, so they go red until the
native solver emits the event (Stage 88c). Nothing in the game's visible output changes from the
event alone: `BeamTracer.to_path` already splits a wrapping straight step into its out/in escape
halves; 88c maps the event onto that split and tags the outgoing half with surface_id -3.

Regenerated by `python3 classify88.py` (kept with this file as `stage88b_classify.py`).
