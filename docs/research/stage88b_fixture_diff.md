# B9 applied: the `infinity` event in the committed corpora (final classification)

Produced on 2026-10-15 after the user approved B9. The oracle (`EMIT_INFINITY = True`, with the Stage
101 semantics: every beam passes the frame's image of infinity `Q = F(∞)`, at `t = ∞` on a straight beam,
at a finite point on a circle beam) regenerated `bench/fixtures/*.json`, `bench/scenes_corpus/expected`,
`bench/expected`, `bench/scenarios` and `bench/stress`; this compares them with the committed files of
commit 318be3d (the pre-Stage-88 traces). An earlier version of this file held the Stage 88b evidence
(straight-beam wraps only: 443 events, 79 cuts), the diff the user approved; the Stage 101 semantics add
the passages of finite infinity points (an inversion's center image), of the same three kinds.

Every difference is one of these kinds; no other kind occurs:

- **pure insertion**: `infinity` events appear where a beam passes the frame's infinity (no point at the
  real infinity; the point `Q` otherwise); removing them gives the old event list exactly.
- **cut earlier**: the trace already reached MAX_HITS; the inserted events count, so the old tail falls
  beyond the limit and the `max` event lands on the next old crossing.
- **cut at infinity**: the MAX_HITS cut lands on the passage itself: a `max` with surface id -3, at the
  real infinity (no point) or at the frame's infinity point `Q`.

| Corpus | files | changed | event lists | identical | pure insertion | cut earlier | cut at infinity | infinity events | old events now beyond MAX_HITS |
|---|---|---|---|---|---|---|---|---|---|
| fixtures | 10 | 7 | 746 | 712 | 24 | 8 | 0 | 96 | 59 |
| scenes_corpus | 348 | 235 | 1392 | 1085 | 249 | 48 | 10 | 583 | 292 |
| expected | 161 | 125 | 322 | 181 | 0 | 135 | 6 | 239 | 239 |
| stress | 40 | 33 | 80 | 47 | 0 | 31 | 2 | 62 | 62 |

The two "other" fixture lists are `stage88_infinity.json`'s own re-specification (a renamed case and the
inversion-then-∞ cases, whose entry is now the passage of the center); `stage101_infinity_point.json` is new.
The native solver reproduces every regenerated fixture at f64, Dual and 4 limbs (`native/tests/test_infinity.cpp`),
and its own diff of the 115 golden fixtures has the same counts as the oracle's (32 changed, 83 events, 8 cuts).

## Every cut, by corpus

### fixtures (8)

- `stage81_rules.json.fixtures[departure_rehit_straight].expected`: cut earlier (max at next crossing); 16 old event(s) dropped
- `stage81_rules.json.fixtures[line_through_inversion_center].expected`: cut earlier (max at next crossing); 3 old event(s) dropped
- `stage82_portals.json.fixtures[portal_pop_departure_rehit].expected`: cut earlier (max at next crossing); 11 old event(s) dropped
- `stage83_projective.json.fixtures[player_at_inversion_center].expected`: cut earlier (max at next crossing); 11 old event(s) dropped
- `stage83_projective.json.fixtures[player_at_inversion_center_planned].expected`: cut earlier (max at next crossing); 9 old event(s) dropped
- `stage83_projective.json.fixtures[cursor_at_inversion_center].expected`: cut earlier (max at next crossing); 7 old event(s) dropped
- `stage83_projective.json.fixtures[cursor_at_inversion_center_no_plan].expected`: cut earlier (max at next crossing); 1 old event(s) dropped
- `stage87_click_forks.json.fixtures[cn_back_inside_click_near_planned].expected`: cut earlier (max at next crossing); 1 old event(s) dropped

### scenes_corpus (58)

- `expected/main__02.json.planned`: cut earlier (max at next crossing); 2 old event(s) dropped
- `expected/playground__directional_arcs__11.json.planned`: cut earlier (max at next crossing); 3 old event(s) dropped
- `expected/playground__inversion_grid__00.json.planned`: cut earlier (max at next crossing); 2 old event(s) dropped
- `expected/playground__inversion_grid__00.json.physical`: cut earlier (max at next crossing); 1 old event(s) dropped
- `expected/playground__inversion_grid__01.json.planned`: cut earlier (max at next crossing); 2 old event(s) dropped
- `expected/playground__inversion_grid__02.json.planned`: cut at infinity (max at the frame's infinity point); 6 old event(s) dropped
- `expected/playground__inversion_grid__03.json.planned`: cut earlier (max at next crossing); 8 old event(s) dropped
- `expected/playground__inversion_grid__03.json.physical`: cut earlier (max at next crossing); 1 old event(s) dropped
- `expected/playground__inversion_grid__05.json.planned`: cut at infinity (max at the frame's infinity point); 8 old event(s) dropped
- `expected/playground__inversion_grid__05.json.physical`: cut earlier (max at next crossing); 11 old event(s) dropped
- `expected/playground__inversion_grid__06.json.planned`: cut earlier (max at next crossing); 1 old event(s) dropped
- `expected/playground__inversion_grid__06.json.physical`: cut earlier (max at next crossing); 11 old event(s) dropped
- `expected/playground__inversion_grid__07.json.planned`: cut at infinity (max at the frame's infinity point); 7 old event(s) dropped
- `expected/playground__inversion_grid__08.json.planned`: cut earlier (max at next crossing); 2 old event(s) dropped
- `expected/playground__inversion_grid__09.json.planned`: cut earlier (max at next crossing); 4 old event(s) dropped
- `expected/playground__inversion_grid__09.json.physical`: cut earlier (max at next crossing); 1 old event(s) dropped
- `expected/playground__inversion_grid__11.json.planned`: cut earlier (max at next crossing); 3 old event(s) dropped
- `expected/playground__inversion_grid__11.json.physical`: cut earlier (max at next crossing); 2 old event(s) dropped
- `expected/playground__plannable_inversion__07.json.planned`: cut earlier (max at next crossing); 2 old event(s) dropped
- `expected/playground__portals__01.json.physical`: cut earlier (max at next crossing); 6 old event(s) dropped
- `expected/playground__portals__02.json.physical`: cut earlier (max at next crossing); 4 old event(s) dropped
- `expected/playground__projective__02.json.planned`: cut earlier (max at next crossing); 2 old event(s) dropped
- `expected/playground__projective__03.json.planned`: cut at infinity; 1 old event(s) dropped
- `expected/playground__projective__04.json.planned`: cut earlier (max at next crossing); 2 old event(s) dropped
- `expected/playground__projective__06.json.planned`: cut earlier (max at next crossing); 2 old event(s) dropped
- `expected/playground__projective_arcs__04.json.planned`: cut earlier (max at next crossing); 3 old event(s) dropped
- `expected/playground__two_sided__01.json.planned`: cut earlier (max at next crossing); 2 old event(s) dropped
- `expected/test_levels__circle_no_walls__00.json.planned`: cut earlier (max at next crossing); 3 old event(s) dropped
- `expected/test_levels__circle_no_walls__00.json.physical`: cut earlier (max at next crossing); 2 old event(s) dropped
- `expected/test_levels__circle_no_walls__01.json.planned`: cut earlier (max at next crossing); 5 old event(s) dropped
- `expected/test_levels__circle_no_walls__01.json.physical`: cut earlier (max at next crossing); 4 old event(s) dropped
- `expected/test_levels__circle_no_walls__03.json.planned`: cut at infinity (max at inf); 4 old event(s) dropped
- `expected/test_levels__circle_no_walls__03.json.physical`: cut earlier (max at next crossing); 11 old event(s) dropped
- `expected/test_levels__circle_no_walls__04.json.planned`: cut earlier (max at next crossing); 4 old event(s) dropped
- `expected/test_levels__circle_no_walls__04.json.physical`: cut earlier (max at next crossing); 4 old event(s) dropped
- `expected/test_levels__circle_no_walls__05.json.planned`: cut earlier (max at next crossing); 4 old event(s) dropped
- `expected/test_levels__circle_no_walls__05.json.physical`: cut earlier (max at next crossing); 3 old event(s) dropped
- `expected/test_levels__circle_no_walls__06.json.planned`: cut earlier (max at next crossing); 10 old event(s) dropped
- `expected/test_levels__circle_no_walls__06.json.physical`: cut at infinity (max at inf); 4 old event(s) dropped
- `expected/test_levels__circle_no_walls__07.json.planned`: cut earlier (max at next crossing); 10 old event(s) dropped
- `expected/test_levels__circle_no_walls__07.json.physical`: cut earlier (max at next crossing); 3 old event(s) dropped
- `expected/test_levels__circle_no_walls__08.json.planned`: cut earlier (max at next crossing); 5 old event(s) dropped
- `expected/test_levels__circle_no_walls__08.json.physical`: cut earlier (max at next crossing); 11 old event(s) dropped
- `expected/test_levels__circle_no_walls__09.json.planned`: cut at infinity (max at the frame's infinity point); 6 old event(s) dropped
- `expected/test_levels__circle_no_walls__09.json.physical`: cut earlier (max at next crossing); 5 old event(s) dropped
- `expected/test_levels__circle_no_walls__11.json.planned`: cut earlier (max at next crossing); 5 old event(s) dropped
- `expected/test_levels__circle_no_walls__11.json.physical`: cut earlier (max at next crossing); 4 old event(s) dropped
- `expected/test_levels__full_circle_mirror__00.json.planned`: cut at infinity (max at the frame's infinity point); 10 old event(s) dropped
- `expected/test_levels__full_circle_mirror__00.json.physical`: cut at infinity (max at the frame's infinity point); 10 old event(s) dropped
- `expected/test_levels__full_circle_mirror__01.json.planned`: cut earlier (max at next crossing); 10 old event(s) dropped
- `expected/test_levels__full_circle_mirror__02.json.physical`: cut earlier (max at next crossing); 11 old event(s) dropped
- `expected/test_levels__full_circle_mirror__03.json.planned`: cut earlier (max at next crossing); 6 old event(s) dropped
- `expected/test_levels__full_circle_mirror__04.json.planned`: cut earlier (max at next crossing); 7 old event(s) dropped
- `expected/test_levels__full_circle_mirror__06.json.physical`: cut earlier (max at next crossing); 11 old event(s) dropped
- `expected/test_levels__full_circle_mirror__10.json.planned`: cut earlier (max at next crossing); 10 old event(s) dropped
- `expected/test_levels__full_circle_mirror__11.json.planned`: cut earlier (max at next crossing); 7 old event(s) dropped
- `expected/test_levels__three_mirrors__07.json.planned`: cut at infinity; 1 old event(s) dropped
- `expected/test_levels__two_sided_mirror__00.json.planned`: cut earlier (max at next crossing); 3 old event(s) dropped

### expected (140)

- `dump_7_4_1_3_6_6_6.json.planned`: cut earlier (max at next crossing); 1 old event(s) dropped
- `dump_7_4_1_3_6_6_6_f32.json.planned`: cut earlier (max at next crossing); 1 old event(s) dropped
- `grid30_chain_d01_073.json.planned`: cut earlier (max at next crossing); 4 old event(s) dropped
- `grid30_chain_d01_073.json.physical`: cut earlier (max at next crossing); 4 old event(s) dropped
- `grid30_chain_d01_088.json.planned`: cut earlier (max at next crossing); 3 old event(s) dropped
- `grid30_chain_d01_088.json.physical`: cut earlier (max at next crossing); 3 old event(s) dropped
- `grid30_chain_d02_100.json.planned`: cut earlier (max at next crossing); 2 old event(s) dropped
- `grid30_chain_d02_100.json.physical`: cut earlier (max at next crossing); 2 old event(s) dropped
- `grid30_chain_d03_018.json.planned`: cut earlier (max at next crossing); 1 old event(s) dropped
- `grid30_chain_d03_018.json.physical`: cut earlier (max at next crossing); 1 old event(s) dropped
- `grid30_chain_d03_026.json.planned`: cut earlier (max at next crossing); 2 old event(s) dropped
- `grid30_chain_d03_026.json.physical`: cut earlier (max at next crossing); 1 old event(s) dropped
- `grid30_chain_d04_082.json.planned`: cut earlier (max at next crossing); 2 old event(s) dropped
- `grid30_chain_d04_082.json.physical`: cut earlier (max at next crossing); 1 old event(s) dropped
- `grid30_chain_d06_061.json.planned`: cut earlier (max at next crossing); 2 old event(s) dropped
- `grid30_chain_d06_061.json.physical`: cut earlier (max at next crossing); 1 old event(s) dropped
- `grid30_chain_d07_093.json.planned`: cut earlier (max at next crossing); 2 old event(s) dropped
- `grid30_chain_d07_102.json.planned`: cut earlier (max at next crossing); 1 old event(s) dropped
- `grid30_chain_d09_015.json.planned`: cut earlier (max at next crossing); 1 old event(s) dropped
- `grid30_chain_d09_105.json.planned`: cut earlier (max at next crossing); 2 old event(s) dropped
- `grid30_chain_d10_022.json.planned`: cut earlier (max at next crossing); 1 old event(s) dropped
- `grid30_chain_d10_138.json.planned`: cut earlier (max at next crossing); 3 old event(s) dropped
- `grid30_chain_d11_012.json.planned`: cut earlier (max at next crossing); 2 old event(s) dropped
- `grid30_chain_d11_030.json.planned`: cut earlier (max at next crossing); 1 old event(s) dropped
- `grid30_chain_d11_043.json.planned`: cut earlier (max at next crossing); 3 old event(s) dropped
- `grid30_chain_d11_125.json.planned`: cut earlier (max at next crossing); 1 old event(s) dropped
- `grid30_chain_d12_090.json.planned`: cut earlier (max at next crossing); 3 old event(s) dropped
- `grid30_chain_d12_110.json.planned`: cut earlier (max at next crossing); 3 old event(s) dropped
- `grid30_chain_d13_062.json.planned`: cut earlier (max at next crossing); 1 old event(s) dropped
- `grid30_chain_d14_033.json.planned`: cut earlier (max at next crossing); 3 old event(s) dropped
- `grid30_chain_d14_079.json.planned`: cut earlier (max at next crossing); 1 old event(s) dropped
- `grid30_chain_d14_135.json.planned`: cut earlier (max at next crossing); 3 old event(s) dropped
- `grid30_chain_d14_155.json.planned`: cut earlier (max at next crossing); 3 old event(s) dropped
- `grid60_chain_d01_025.json.planned`: cut at infinity (max at inf); 1 old event(s) dropped
- `grid60_chain_d01_025.json.physical`: cut earlier (max at next crossing); 2 old event(s) dropped
- `grid60_chain_d01_063.json.planned`: cut earlier (max at next crossing); 1 old event(s) dropped
- `grid60_chain_d01_063.json.physical`: cut earlier (max at next crossing); 1 old event(s) dropped
- `grid60_chain_d02_020.json.planned`: cut earlier (max at next crossing); 3 old event(s) dropped
- `grid60_chain_d02_020.json.physical`: cut earlier (max at next crossing); 1 old event(s) dropped
- `grid60_chain_d02_058.json.planned`: cut at infinity (max at inf); 1 old event(s) dropped
- `grid60_chain_d02_058.json.physical`: cut earlier (max at next crossing); 2 old event(s) dropped
- `grid60_chain_d03_072.json.planned`: cut earlier (max at next crossing); 1 old event(s) dropped
- `grid60_chain_d03_075.json.planned`: cut earlier (max at next crossing); 2 old event(s) dropped
- `grid60_chain_d03_101.json.planned`: cut earlier (max at next crossing); 2 old event(s) dropped
- `grid60_chain_d04_028.json.planned`: cut at infinity (max at inf); 1 old event(s) dropped
- `grid60_chain_d04_120.json.planned`: cut earlier (max at next crossing); 2 old event(s) dropped
- `grid60_chain_d04_139.json.planned`: cut at infinity (max at inf); 1 old event(s) dropped
- `grid60_chain_d04_143.json.planned`: cut earlier (max at next crossing); 1 old event(s) dropped
- `grid60_chain_d04_156.json.planned`: cut earlier (max at next crossing); 1 old event(s) dropped
- `grid60_chain_d04_156.json.physical`: cut earlier (max at next crossing); 1 old event(s) dropped
- `grid60_chain_d05_002.json.planned`: cut earlier (max at next crossing); 1 old event(s) dropped
- `grid60_chain_d05_008.json.planned`: cut earlier (max at next crossing); 1 old event(s) dropped
- `grid60_chain_d05_044.json.planned`: cut earlier (max at next crossing); 2 old event(s) dropped
- `grid60_chain_d07_017.json.planned`: cut earlier (max at next crossing); 1 old event(s) dropped
- `grid60_chain_d07_046.json.planned`: cut earlier (max at next crossing); 1 old event(s) dropped
- `grid60_chain_d07_085.json.planned`: cut earlier (max at next crossing); 1 old event(s) dropped
- `grid60_chain_d07_113.json.planned`: cut earlier (max at next crossing); 3 old event(s) dropped
- `grid60_chain_d08_065.json.planned`: cut earlier (max at next crossing); 2 old event(s) dropped
- `grid60_chain_d08_068.json.planned`: cut earlier (max at next crossing); 1 old event(s) dropped
- `grid60_chain_d08_086.json.planned`: cut earlier (max at next crossing); 1 old event(s) dropped
- `grid60_chain_d08_095.json.planned`: cut earlier (max at next crossing); 1 old event(s) dropped
- `grid60_chain_d08_131.json.planned`: cut earlier (max at next crossing); 2 old event(s) dropped
- `grid60_chain_d09_027.json.planned`: cut earlier (max at next crossing); 1 old event(s) dropped
- `grid60_chain_d09_089.json.planned`: cut earlier (max at next crossing); 1 old event(s) dropped
- `grid60_chain_d09_116.json.planned`: cut earlier (max at next crossing); 2 old event(s) dropped
- `grid60_chain_d10_003.json.planned`: cut earlier (max at next crossing); 2 old event(s) dropped
- `grid60_chain_d10_007.json.planned`: cut earlier (max at next crossing); 3 old event(s) dropped
- `grid60_chain_d10_032.json.planned`: cut earlier (max at next crossing); 4 old event(s) dropped
- `grid60_chain_d10_036.json.planned`: cut earlier (max at next crossing); 2 old event(s) dropped
- `grid60_chain_d10_054.json.planned`: cut earlier (max at next crossing); 2 old event(s) dropped
- `grid60_chain_d10_055.json.planned`: cut at infinity (max at the frame's infinity point); 1 old event(s) dropped
- `grid60_chain_d10_099.json.planned`: cut earlier (max at next crossing); 1 old event(s) dropped
- `grid60_chain_d10_115.json.planned`: cut earlier (max at next crossing); 1 old event(s) dropped
- `grid60_chain_d10_142.json.planned`: cut earlier (max at next crossing); 2 old event(s) dropped
- `grid60_chain_d11_016.json.planned`: cut earlier (max at next crossing); 1 old event(s) dropped
- `grid60_chain_d11_059.json.planned`: cut earlier (max at next crossing); 1 old event(s) dropped
- `grid60_chain_d11_083.json.planned`: cut earlier (max at next crossing); 2 old event(s) dropped
- `grid60_chain_d11_097.json.planned`: cut earlier (max at next crossing); 2 old event(s) dropped
- `grid60_chain_d11_130.json.planned`: cut earlier (max at next crossing); 3 old event(s) dropped
- `grid60_chain_d12_021.json.planned`: cut earlier (max at next crossing); 2 old event(s) dropped
- `grid60_chain_d12_081.json.planned`: cut earlier (max at next crossing); 2 old event(s) dropped
- `grid60_chain_d12_109.json.planned`: cut earlier (max at next crossing); 2 old event(s) dropped
- `grid60_chain_d13_001.json.planned`: cut earlier (max at next crossing); 1 old event(s) dropped
- `grid60_chain_d13_047.json.planned`: cut earlier (max at next crossing); 3 old event(s) dropped
- `grid60_chain_d13_060.json.planned`: cut earlier (max at next crossing); 1 old event(s) dropped
- `grid60_chain_d13_069.json.planned`: cut earlier (max at next crossing); 1 old event(s) dropped
- `grid60_chain_d13_096.json.planned`: cut earlier (max at next crossing); 3 old event(s) dropped
- `grid60_chain_d13_119.json.planned`: cut earlier (max at next crossing); 1 old event(s) dropped
- `grid60_chain_d13_132.json.planned`: cut earlier (max at next crossing); 2 old event(s) dropped
- `grid60_chain_d14_010.json.planned`: cut earlier (max at next crossing); 2 old event(s) dropped
- `grid60_chain_d14_041.json.planned`: cut earlier (max at next crossing); 3 old event(s) dropped
- `grid60_chain_d14_045.json.planned`: cut earlier (max at next crossing); 2 old event(s) dropped
- `grid60_chain_d14_066.json.planned`: cut earlier (max at next crossing); 1 old event(s) dropped
- `grid60_chain_d14_111.json.planned`: cut earlier (max at next crossing); 1 old event(s) dropped
- `grid60_chain_d14_117.json.planned`: cut earlier (max at next crossing); 3 old event(s) dropped
- `grid60_chain_d15_029.json.planned`: cut earlier (max at next crossing); 2 old event(s) dropped
- `grid60_chain_d15_035.json.planned`: cut earlier (max at next crossing); 1 old event(s) dropped
- `grid60_chain_d15_051.json.planned`: cut earlier (max at next crossing); 2 old event(s) dropped
- `grid60_chain_d15_067.json.planned`: cut earlier (max at next crossing); 3 old event(s) dropped
- `grid60_chain_d15_087.json.planned`: cut earlier (max at next crossing); 1 old event(s) dropped
- `grid60_chain_d15_133.json.planned`: cut earlier (max at next crossing); 2 old event(s) dropped
- `grid60_mixed_d01_107.json.planned`: cut earlier (max at next crossing); 3 old event(s) dropped
- `grid60_mixed_d01_107.json.physical`: cut earlier (max at next crossing); 3 old event(s) dropped
- `grid60_mixed_d01_126.json.planned`: cut earlier (max at next crossing); 1 old event(s) dropped
- `grid60_mixed_d01_126.json.physical`: cut earlier (max at next crossing); 1 old event(s) dropped
- `grid60_mixed_d02_034.json.planned`: cut earlier (max at next crossing); 2 old event(s) dropped
- `grid60_mixed_d02_034.json.physical`: cut earlier (max at next crossing); 1 old event(s) dropped
- `grid60_mixed_d02_076.json.planned`: cut earlier (max at next crossing); 1 old event(s) dropped
- `grid60_mixed_d06_064.json.planned`: cut earlier (max at next crossing); 2 old event(s) dropped
- `grid60_mixed_d06_106.json.planned`: cut earlier (max at next crossing); 2 old event(s) dropped
- `grid60_mixed_d07_146.json.planned`: cut earlier (max at next crossing); 2 old event(s) dropped
- `grid60_mixed_d11_013.json.planned`: cut earlier (max at next crossing); 2 old event(s) dropped
- `grid60_mixed_d12_114.json.planned`: cut earlier (max at next crossing); 1 old event(s) dropped
- `grid90_chain_d01_150.json.planned`: cut earlier (max at next crossing); 1 old event(s) dropped
- `grid90_chain_d04_136.json.planned`: cut earlier (max at next crossing); 1 old event(s) dropped
- `grid90_chain_d04_147.json.planned`: cut earlier (max at next crossing); 1 old event(s) dropped
- `grid90_chain_d05_121.json.planned`: cut earlier (max at next crossing); 1 old event(s) dropped
- `grid90_chain_d07_092.json.planned`: cut earlier (max at next crossing); 1 old event(s) dropped
- `grid90_chain_d07_151.json.planned`: cut earlier (max at next crossing); 1 old event(s) dropped
- `grid90_chain_d08_078.json.planned`: cut earlier (max at next crossing); 2 old event(s) dropped
- `grid90_chain_d08_129.json.planned`: cut earlier (max at next crossing); 1 old event(s) dropped
- `grid90_chain_d09_137.json.planned`: cut earlier (max at next crossing); 2 old event(s) dropped
- `grid90_chain_d11_084.json.planned`: cut earlier (max at next crossing); 2 old event(s) dropped
- `grid90_chain_d11_127.json.planned`: cut earlier (max at next crossing); 2 old event(s) dropped
- `grid90_chain_d11_154.json.planned`: cut earlier (max at next crossing); 2 old event(s) dropped
- `grid90_chain_d12_104.json.planned`: cut earlier (max at next crossing); 1 old event(s) dropped
- `grid90_chain_d13_004.json.planned`: cut earlier (max at next crossing); 2 old event(s) dropped
- `grid90_chain_d13_091.json.planned`: cut earlier (max at next crossing); 2 old event(s) dropped
- `grid90_chain_d13_108.json.planned`: cut earlier (max at next crossing); 1 old event(s) dropped
- `grid90_chain_d13_122.json.planned`: cut earlier (max at next crossing); 1 old event(s) dropped
- `grid90_chain_d13_122.json.physical`: cut earlier (max at next crossing); 1 old event(s) dropped
- `grid90_chain_d13_159.json.planned`: cut earlier (max at next crossing); 2 old event(s) dropped
- `grid90_chain_d14_098.json.planned`: cut earlier (max at next crossing); 2 old event(s) dropped
- `grid90_chain_d15_024.json.planned`: cut earlier (max at next crossing); 4 old event(s) dropped
- `grid90_chain_d15_049.json.planned`: cut earlier (max at next crossing); 1 old event(s) dropped
- `grid90_chain_d15_070.json.planned`: cut earlier (max at next crossing); 1 old event(s) dropped
- `grid90_mixed_d06_052.json.planned`: cut earlier (max at next crossing); 1 old event(s) dropped
- `grid90_mixed_d10_019.json.planned`: cut earlier (max at next crossing); 1 old event(s) dropped
- `grid90_mixed_d10_039.json.planned`: cut earlier (max at next crossing); 2 old event(s) dropped
- `grid90_mixed_d13_144.json.planned`: cut earlier (max at next crossing); 1 old event(s) dropped

### stress (33)

- `expected/grid30_chain_d16_038.json.planned`: cut earlier (max at next crossing); 2 old event(s) dropped
- `expected/grid30_chain_d18_019.json.planned`: cut earlier (max at next crossing); 1 old event(s) dropped
- `expected/grid30_chain_d19_027.json.planned`: cut earlier (max at next crossing); 4 old event(s) dropped
- `expected/grid30_chain_d19_036.json.planned`: cut earlier (max at next crossing); 2 old event(s) dropped
- `expected/grid30_chain_d20_018.json.planned`: cut earlier (max at next crossing); 2 old event(s) dropped
- `expected/grid30_chain_d21_005.json.planned`: cut earlier (max at next crossing); 1 old event(s) dropped
- `expected/grid30_chain_d23_023.json.planned`: cut earlier (max at next crossing); 2 old event(s) dropped
- `expected/grid30_chain_d23_039.json.planned`: cut earlier (max at next crossing); 2 old event(s) dropped
- `expected/grid30_mixed_d19_002.json.planned`: cut earlier (max at next crossing); 1 old event(s) dropped
- `expected/grid60_chain_d16_009.json.planned`: cut earlier (max at next crossing); 2 old event(s) dropped
- `expected/grid60_chain_d16_026.json.planned`: cut earlier (max at next crossing); 2 old event(s) dropped
- `expected/grid60_chain_d16_033.json.planned`: cut at infinity (max at the frame's infinity point); 4 old event(s) dropped
- `expected/grid60_chain_d17_016.json.planned`: cut earlier (max at next crossing); 1 old event(s) dropped
- `expected/grid60_chain_d17_029.json.planned`: cut earlier (max at next crossing); 2 old event(s) dropped
- `expected/grid60_chain_d17_031.json.planned`: cut earlier (max at next crossing); 1 old event(s) dropped
- `expected/grid60_chain_d17_035.json.planned`: cut earlier (max at next crossing); 2 old event(s) dropped
- `expected/grid60_chain_d18_008.json.planned`: cut earlier (max at next crossing); 3 old event(s) dropped
- `expected/grid60_chain_d18_013.json.planned`: cut earlier (max at next crossing); 1 old event(s) dropped
- `expected/grid60_chain_d19_022.json.planned`: cut earlier (max at next crossing); 2 old event(s) dropped
- `expected/grid60_chain_d20_000.json.planned`: cut earlier (max at next crossing); 3 old event(s) dropped
- `expected/grid60_chain_d21_007.json.planned`: cut earlier (max at next crossing); 2 old event(s) dropped
- `expected/grid60_chain_d22_003.json.planned`: cut earlier (max at next crossing); 2 old event(s) dropped
- `expected/grid60_chain_d23_017.json.planned`: cut earlier (max at next crossing); 1 old event(s) dropped
- `expected/grid60_chain_d23_021.json.planned`: cut earlier (max at next crossing); 2 old event(s) dropped
- `expected/grid60_chain_d25_001.json.planned`: cut earlier (max at next crossing); 1 old event(s) dropped
- `expected/grid60_chain_d25_004.json.planned`: cut earlier (max at next crossing); 2 old event(s) dropped
- `expected/grid60_chain_d25_010.json.planned`: cut earlier (max at next crossing); 2 old event(s) dropped
- `expected/grid60_mixed_d16_020.json.planned`: cut earlier (max at next crossing); 1 old event(s) dropped
- `expected/grid60_mixed_d16_034.json.planned`: cut earlier (max at next crossing); 1 old event(s) dropped
- `expected/grid90_chain_d19_012.json.planned`: cut earlier (max at next crossing); 2 old event(s) dropped
- `expected/grid90_chain_d20_030.json.planned`: cut at infinity (max at the frame's infinity point); 1 old event(s) dropped
- `expected/grid90_chain_d22_006.json.planned`: cut earlier (max at next crossing); 3 old event(s) dropped
- `expected/grid90_chain_d24_024.json.planned`: cut earlier (max at next crossing); 2 old event(s) dropped

Regenerated by `python3 stage88b_classify.py <dir>` with `<dir>/repo88/bench` the new and `<dir>/repo88_base/bench` the old corpora.
