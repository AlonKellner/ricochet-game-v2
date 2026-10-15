// The pre-native goldens (bench/goldens/*.json, tools/dump_goldens.gd): every
// native tier must reproduce the GDScript stack bit for bit. Inputs are the
// bit patterns GDScript parsed (Godot's decimal parsing is not correctly rounded).
#include "goldens.hpp"

#include <doctest/doctest.h>

#include <filesystem>
#include <set>

using namespace rc;
using namespace rc::test;

namespace {

double fh(const std::string &h) { return from_hex_str(h); }

template <class F>
int count_checks(const F &f) {
	int n = 0;
	f(n);
	return n;
}

std::vector<SurfaceDef> defs_of(const ojson &surfaces_json, const ojson &inputs) {
	return defs_from_json_hex(surfaces_json, inputs);
}

// RC_GOLDEN_REBLESS=1: a pinned value that differs is printed as a replacement
// line "REBLESS-JSON\t<golden file>\t<key>\t<field path>\t<json>" for
// docs/research/apply_rebless.py (Stage 106; only with the user's approval of
// the behaviour change behind it). The CHECK still fails.
void rebless_json(const char *file, const std::string &key, const std::string &path, const std::string &got_json, const std::string &want_json) {
	if (got_json != want_json && std::getenv("RC_GOLDEN_REBLESS")) {
		std::printf("REBLESS-JSON\t%s\t%s\t%s\t%s\n", file, key.c_str(), path.c_str(), got_json.c_str());
	}
}

// RC_SWEEP_DUMP=<list file>: the listed sweep combos ("<group>\t<combo>" per
// line) are printed as one JSON line each, "DUMP\t{...}": the inputs, the
// surfaces and both traces with every event's point and beam (hex doubles).
// The evidence judge (docs/research/stage106_judge.py) compares such dumps of
// two builds; the list is any set of combos, e.g. the mismatches of a run.
template <Num N>
ojson dump_events(const std::vector<Event<N>> &evs) {
	ojson out = ojson::array();
	for (const Event<N> &e : evs) {
		ojson j;
		j["kind"] = event_kind_name(e.kind);
		j["sid"] = e.surface_id;
		j["applied"] = e.applied;
		j["rehit"] = e.rehit;
		j["popped"] = e.popped;
		j["teleport"] = e.teleport;
		j["left"] = e.left;
		if (e.point) {
			j["x"] = hex(e.point->fx());
			j["y"] = hex(e.point->fy());
		}
		if (e.beam) {
			j["beam"] = ojson::array({hex(e.beam->p.fx()), hex(e.beam->p.fy()), hex(e.beam->tau.fx()), hex(e.beam->tau.fy()), hex(e.beam->kappa.to_float())});
		}
		out.push_back(j);
	}
	return out;
}

ojson dump_climb(const Climb &c) {
	ojson j;
	j["limbs"] = c.limbs;
	j["events"] = c.limbs == 0 ? dump_events(c.f64_events) : dump_events(c.big_events);
	return j;
}

ojson dump_defs(const std::vector<SurfaceDef> &defs) {
	ojson out = ojson::array();
	for (const SurfaceDef &d : defs) {
		ojson j;
		j["id"] = d.id;
		j["line"] = d.line;
		j["full"] = d.full;
		j["pre"] = d.pre_transform;
		j["s"] = ojson::array({hex(d.sx), hex(d.sy)});
		j["e"] = ojson::array({hex(d.ex), hex(d.ey)});
		if (d.has_via) {
			j["v"] = ojson::array({hex(d.vx), hex(d.vy)});
		}
		if (d.has_circle) {
			j["circle"] = ojson::array({hex(d.cx), hex(d.cy), hex(d.radius)});
		}
		const std::optional<SideDef> &side = d.left ? d.left : d.right;
		if (side && side->pair >= 0) {
			j["portal"] = ojson::array({hex(side->dx), hex(side->dy), hex(side->theta)});
			j["pair"] = side->pair;
		}
		out.push_back(j);
	}
	return out;
}

std::string bs(const geom::Blocked &bl) {
	return std::string("[") + (bl.left ? "true" : "false") + ", " + (bl.right ? "true" : "false") + "]";
}

} // namespace

TEST_CASE("goldens: BigFloat operations at every tier") {
	const ojson g = load_json("bench/goldens/bigfloat.json");
	int checks = 0;
	for (auto it = g.begin(); it != g.end(); ++it) {
		if (it.key() == "_schema") {
			continue;
		}
		const ojson &v = it.value();
		const int n = std::stoi(it.key().substr(0, it.key().find('/')));
		const std::string key = it.key();
		if (it.key().ends_with("/inf")) {
			const BigFloat inf = BigFloat::infinity(n);
			CHECK_MESSAGE(inf.debug_string() == v["inf"].get<std::string>(), key);
			CHECK_MESSAGE(inf.is_inf() == v["is_inf"].get<bool>(), key);
			CHECK_MESSAGE(hex(inf.to_float()) == v["to_float"].get<std::string>(), key);
			CHECK_MESSAGE(inf.cmp(BigFloat::from_int(1, n)) == int(v["cmp_one"].get<double>()), key);
			CHECK_MESSAGE(BigFloat::zero(n).debug_string() == v["zero"].get<std::string>(), key);
			CHECK_MESSAGE(BigFloat::from_int(-123456789, n).debug_string() == v["from_int"].get<std::string>(), key);
			CHECK_MESSAGE(BigFloat::limbs_for_bits(n * 28 - 3) == int(v["limbs_for_bits"].get<double>()), key);
			checks += 7;
			continue;
		}
		const std::string ia = v["in_a"], ib = v["in_b"];
		const bool dec = ia.size() != 16 || ia.find_first_not_of("0123456789abcdef") != std::string::npos;
		const BigFloat a = dec ? BigFloat::from_string(ia, n) : BigFloat::from_float(fh(ia), n);
		const BigFloat bb = dec ? BigFloat::from_string(ib, n) : BigFloat::from_float(fh(ib), n);
		const long long k = (long long)v["k"].get<double>();
		CHECK_MESSAGE(a.debug_string() == v["a"].get<std::string>(), key);
		CHECK_MESSAGE(bb.debug_string() == v["b"].get<std::string>(), key);
		CHECK_MESSAGE(a.add(bb).debug_string() == v["add"].get<std::string>(), key);
		CHECK_MESSAGE(a.sub(bb).debug_string() == v["sub"].get<std::string>(), key);
		CHECK_MESSAGE(a.mul(bb).debug_string() == v["mul"].get<std::string>(), key);
		if (v.contains("div")) {
			CHECK_MESSAGE(a.div(bb).debug_string() == v["div"].get<std::string>(), key);
		}
		CHECK_MESSAGE(a.cmp(bb) == int(v["cmp"].get<double>()), key);
		CHECK_MESSAGE(a.cmp_abs(bb) == int(v["cmp_abs"].get<double>()), key);
		CHECK_MESSAGE(a.neg().debug_string() == v["neg"].get<std::string>(), key);
		CHECK_MESSAGE(a.abs_val().debug_string() == v["abs"].get<std::string>(), key);
		CHECK_MESSAGE(a.mul_pow2(k).debug_string() == v["mul_pow2"].get<std::string>(), key);
		CHECK_MESSAGE(hex(a.to_float()) == v["to_float"].get<std::string>(), key);
		CHECK_MESSAGE(hex(a.mul(bb).to_float()) == v["to_float_mul"].get<std::string>(), key);
		CHECK_MESSAGE(a.with_limbs(std::max(2, n / 2)).debug_string() == v["with_limbs_half"].get<std::string>(), key);
		CHECK_MESSAGE(a.with_limbs(n * 2).debug_string() == v["with_limbs_double"].get<std::string>(), key);
		CHECK_MESSAGE(a.abs_val().sqrt_val().debug_string() == v["sqrt_abs"].get<std::string>(), key);
		CHECK_MESSAGE(a.top_exp() == (long long)v["top_exp"].get<double>(), key);
		CHECK_MESSAGE(a.is_zero() == v["is_zero"].get<bool>(), key);
		CHECK_MESSAGE(a.is_negative() == v["is_negative"].get<bool>(), key);
		checks += 19;
	}
	CHECK(checks > 4000);
}

TEST_CASE("goldens: Exact predicates, constructions and lazy lines") {
	using namespace rc::exact;
	const ojson g = load_json("bench/goldens/exact.json");
	int cases = 0;
	auto arr = [](const auto &a) { std::string s = "["; for (size_t i = 0; i < a.size(); i++) { if (i) { s += ","; } s += "b" + a[i].debug_string(); } return s + "]"; };
	for (auto it = g.begin(); it != g.end(); ++it) {
		if (it.key() == "_schema") {
			continue;
		}
		const ojson &v = it.value();
		const std::string key = it.key();
		const int i = std::stoi(key);
		const double ax = fh(v["in"][0]), ay = fh(v["in"][1]), bx = fh(v["in"][2]), by = fh(v["in"][3]), cx = fh(v["in"][4]), cy = fh(v["in"][5]);
		const double ocx = fh(v["on_circle_in"][0]), ocy = fh(v["on_circle_in"][1]), r = fh(v["on_circle_in"][2]), kx = fh(v["on_circle_in"][3]), ky = fh(v["on_circle_in"][4]);
		const ojson &ic = v["incircle_in"];
		const double p1x = fh(ic[0]), p1y = fh(ic[1]), p2x = fh(ic[2]), p2y = fh(ic[3]), p3x = fh(ic[4]), p3y = fh(ic[5]), p4x = fh(ic[6]), p4y = fh(ic[7]);
		const double ks[5] = {0.5, 0.25, 2.0, -1.0, 1.5};
		const double k = ks[i % 5];
		CHECK_MESSAGE(orient_zero(ax, ay, bx, by, cx, cy) == v["orient_zero"].get<bool>(), key);
		CHECK_MESSAGE(on_circle_zero(ocx, ocy, r, kx, ky) == v["on_circle_zero"].get<bool>(), key);
		CHECK_MESSAGE(incircle_zero(p1x, p1y, p2x, p2y, p3x, p3y, p4x, p4y) == v["incircle_zero"].get<bool>(), key);
		const Line3 L = line_through(ax, ay, bx, by);
		const Point3 X = {of(cx), of(cy), of(1.0)};
		const Point3 Cc = {of(ax), of(ay), of(1.0)};
		const BigFloat rw2 = of(r * r);
		CHECK_MESSAGE(on_line_h(L, X) == v["on_line_h"].get<bool>(), key);
		CHECK_MESSAGE(tangent_zero(L, Cc, rw2) == v["tangent_zero"].get<bool>(), key);
		const auto ln = Line::from_exact(L);
		CHECK_MESSAGE(ln->contains(cx, cy) == v["contains_c"].get<bool>(), key);
		CHECK_MESSAGE(ln->contains(bx, by) == v["contains_b"].get<bool>(), key);
		CHECK_MESSAGE(ln->concurrent(p1x, p1y, p2x, p2y, p3x, p3y, p4x, p4y) == v["concurrent"].get<bool>(), key);
		CHECK_MESSAGE(arr(L) == v["line_through"].get<std::string>(), key);
		CHECK_MESSAGE(arr(line_from_dir(ax, ay, bx - ax, by - ay)) == v["line_from_dir"].get<std::string>(), key);
		CHECK_MESSAGE(arr(reflect_point_h(X, ax, ay, bx, by)) == v["reflect_point_h"].get<std::string>(), key);
		CHECK_MESSAGE(arr(circumcenter_h(p1x, p1y, p2x, p2y, p3x, p3y)) == v["circumcenter_h"].get<std::string>(), key);
		const auto inv = invert_point_h(X, Cc, rw2);
		CHECK_MESSAGE((inv ? arr(*inv) : std::string("null")) == v["invert_point_h"].get<std::string>(), key);
		CHECK_MESSAGE(arr(reflect_line(L, p1x, p1y, p2x, p2y)) == v["reflect_line"].get<std::string>(), key);
		CHECK_MESSAGE(arr(line_through_h(bx, by, X)) == v["line_through_h"].get<std::string>(), key);
		CHECK_MESSAGE(arr(*ln->reflected(p1x, p1y, p2x, p2y)->exact()) == v["reflected"].get<std::string>(), key);
		CHECK_MESSAGE(arr(*ln->translated(k, -k)->exact()) == v["translated"].get<std::string>(), key);
		cases++;
	}
	CHECK(cases == 400);
}

namespace {

template <Num N>
Surf<N> surf_of(const SegDef &d, const std::string &lt, const std::string &rt, const N &like) {
	Surf<N> s;
	s.id = d.id;
	s.seg = geom::seg_from_def(d, like);
	s.line = d.kind == SegKind::Line;
	s.full = d.full;
	s.sx = d.sx;
	s.sy = d.sy;
	s.ex = d.ex;
	s.ey = d.ey;
	s.has_v = d.has_v;
	s.vx = d.vx;
	s.vy = d.vy;
	if (!s.line) {
		s.cx = s.seg.circ->c.fx();
		s.cy = s.seg.circ->c.fy();
		s.r = s.seg.circ->r.to_float();
		s.r2 = s.r * s.r;
	}
	s.left_type = effect_type_from_json(lt);
	s.right_type = effect_type_from_json(rt);
	return s;
}

template <Num N>
std::string beam_str(const Beam<N> &bm) { return cn(bm.p) + ";" + cn(bm.tau) + ";" + num(bm.kappa); }

std::string packed_beam(const Beam<Dual> &bm) {
	return packed({bm.p.re.v, bm.p.im.v, bm.tau.re.v, bm.tau.im.v, bm.kappa.v, bm.p.re.d, bm.p.im.d, bm.tau.re.d, bm.tau.im.d, bm.kappa.d});
}

} // namespace

TEST_CASE("goldens: beam, segment and effect kernels on every tier") {
	const ojson g = load_json("bench/goldens/beam.json");
	std::vector<Surf<F64>> sf;
	std::vector<Surf<Dual>> sdl;
	std::vector<Surf<BigFloat>> sb;
	for (const ojson &sd : g["defs"]) {
		SegDef d;
		d.id = sd["id"].get<int>();
		d.kind = sd["line"].get<bool>() ? SegKind::Line : SegKind::Circle;
		d.full = sd.value("full", false) && !sd["line"].get<bool>();
		d.sx = sd["start"][0].get<double>();
		d.sy = sd["start"][1].get<double>();
		d.ex = sd["end"][0].get<double>();
		d.ey = sd["end"][1].get<double>();
		d.has_v = !sd["via"].is_null();
		if (d.has_v) {
			d.vx = sd["via"][0].get<double>();
			d.vy = sd["via"][1].get<double>();
		}
		if (sd.contains("circle") && !sd["circle"].is_null()) {
			d.cx = sd["circle"]["center"][0].get<double>();
			d.cy = sd["circle"]["center"][1].get<double>();
			d.r = sd["circle"]["radius"].get<double>();
		}
		const std::string lt = sd["left"]["type"], rt = sd["right"]["type"];
		sf.push_back(surf_of(d, lt, rt, F64{1}));
		sdl.push_back(surf_of(d, lt, rt, Dual{1, 0}));
		sb.push_back(surf_of(d, lt, rt, BigFloat::from_int(1, 4)));
	}
	int cases = 0;
	for (auto it = g.begin(); it != g.end(); ++it) {
		if (it.key() == "_schema" || it.key() == "defs") {
			continue;
		}
		const ojson &v = it.value();
		const std::string key = it.key();
		double in[11];
		for (int i = 0; i < 11; i++) {
			in[i] = fh(v["in"][size_t(i)]);
		}
		const double px = in[0], py = in[1], tx = in[2], ty = in[3], k = in[4], t = in[5], dt = in[6], zx = in[7], zy = in[8], dx = in[9], dy = in[10];
		const Beam<F64> b{{F64{px}, F64{py}}, {F64{tx}, F64{ty}}, F64{k}};
		const Beam<Dual> bd{{Dual{px, 0}, Dual{py, 0}}, {Dual{tx, -ty}, Dual{ty, tx}}, Dual{k, 0}};
		const Beam<Dual> bu{{Dual{px, 0}, Dual{py, 0}}, {Dual{tx, 0}, Dual{ty, 0}}, Dual{k, 0}};
		const BigFloat l4 = BigFloat::from_int(1, 4);
		const Beam<BigFloat> bb{{l4.like(px), l4.like(py)}, {l4.like(tx), l4.like(ty)}, l4.like(k)};
		const CN<Dual> at = bd.at(Dual{t, dt});
		CHECK_MESSAGE(packed({at.re.v, at.im.v, at.re.d, at.im.d}) == v["fast_at"].get<std::string>(), key);
		const CN<Dual> tg = bd.tangent_at(Dual{t, dt});
		CHECK_MESSAGE(packed({tg.re.v, tg.im.v, tg.re.d, tg.im.d}) == v["fast_tangent_at"].get<std::string>(), key);
		CHECK_MESSAGE(hex(b.param_of(CN<F64>{F64{zx}, F64{zy}}).v) == v["fast_param_of"].get<std::string>(), key);
		if (k != 0.0) {
			const CN<Dual> ap = *bu.at_infinity();
			CHECK_MESSAGE(packed({ap.re.v, ap.im.v, ap.re.d, ap.im.d}) == v["fast_antipode"].get<std::string>(), key);
		}
		CHECK_MESSAGE(cn(b.at(F64{t})) == v["f64_at"].get<std::string>(), key);
		CHECK_MESSAGE(cn(b.tangent_at(F64{t})) == v["f64_tangent_at"].get<std::string>(), key);
		CHECK_MESSAGE(num(b.param_of(CN<F64>{F64{zx}, F64{zy}})) == v["f64_param_of"].get<std::string>(), key);
		if (k != 0.0) {
			CHECK_MESSAGE(cn(*b.at_infinity()) == v["f64_at_infinity"].get<std::string>(), key);
		}
		const bool big = v.contains("big4_at");
		if (big) {
			CHECK_MESSAGE(cn(bb.at(l4.like(t))) == v["big4_at"].get<std::string>(), key);
			CHECK_MESSAGE(cn(bb.tangent_at(l4.like(t))) == v["big4_tangent_at"].get<std::string>(), key);
			CHECK_MESSAGE(num(bb.param_of(CN<BigFloat>{l4.like(zx), l4.like(zy)})) == v["big4_param_of"].get<std::string>(), key);
			if (k != 0.0) {
				CHECK_MESSAGE(cn(*bb.at_infinity()) == v["big4_at_infinity"].get<std::string>(), key);
			}
		}
		for (size_t si = 0; si < v["surfaces"].size(); si++) {
			const ojson &sv = v["surfaces"][si];
			const std::string sk = key + "/s" + std::to_string(si);
			const Surf<F64> &s = sf[si];
			const Surf<Dual> &sd = sdl[si];
			const CN<F64> z{F64{zx}, F64{zy}}, dir{F64{dx}, F64{dy}};
			const std::vector<Dual> hd = geom::hits(bd, sd.seg, false);
			std::vector<double> hv;
			for (const Dual &r : hd) {
				hv.push_back(r.v);
				hv.push_back(r.d);
			}
			CHECK_MESSAGE(packed(hv) == sv["fast_hits"].get<std::string>(), sk);
			CHECK_MESSAGE(geom::on_segment(s.seg, z) == sv["fast_on_segment"].get<bool>(), sk);
			CHECK_MESSAGE(geom::side_left(s.seg, z, dir) == sv["fast_side_left"].get<bool>(), sk);
			CHECK_MESSAGE(geom::endpoint_at(s.seg, z) == int(sv["fast_endpoint_at"][0].get<double>()), sk);
			CHECK_MESSAGE(geom::endpoint_at(s.seg, CN<F64>{F64{s.sx}, F64{s.sy}}) == int(sv["fast_endpoint_at"][1].get<double>()), sk);
			CHECK_MESSAGE(geom::endpoint_at(s.seg, CN<F64>{F64{s.ex}, F64{s.ey}}) == int(sv["fast_endpoint_at"][2].get<double>()), sk);
			CHECK_MESSAGE(bs(geom::blocked_sides(s.seg, 1, dir)) + bs(geom::blocked_sides(s.seg, 2, dir)) == sv["fast_blocked_sides"].get<std::string>(), sk);
			CHECK_MESSAGE(packed_beam(effects::apply(bd, Dual{t, dt}, sd)) == sv["fast_apply"].get<std::string>(), sk);
			if (!hd.empty()) {
				CHECK_MESSAGE(packed_beam(effects::apply(bd, hd[0], sd)) == sv["fast_apply_at_root"].get<std::string>(), sk);
			}
			const std::vector<F64> hf = geom::hits(b, s.seg, false);
			CHECK_MESSAGE(nums(hf) == sv["f64_hits"].get<std::string>(), sk);
			CHECK_MESSAGE(beam_str(effects::apply(b, F64{t}, s)) == sv["f64_apply"].get<std::string>(), sk);
			if (!hf.empty()) {
				CHECK_MESSAGE(beam_str(effects::apply(b, hf[0], s)) == sv["f64_apply_at_root"].get<std::string>(), sk);
			}
			if (big) {
				const Surf<BigFloat> &sbb = sb[si];
				const CN<BigFloat> zb{l4.like(zx), l4.like(zy)}, dirb{l4.like(dx), l4.like(dy)};
				const std::vector<BigFloat> hb = geom::hits(bb, sbb.seg, false);
				CHECK_MESSAGE(nums(hb) == sv["big4_hits"].get<std::string>(), sk);
				CHECK_MESSAGE(geom::on_segment(sbb.seg, zb) == sv["big4_on_segment"].get<bool>(), sk);
				CHECK_MESSAGE(geom::side_left(sbb.seg, zb, dirb) == sv["big4_side_left"].get<bool>(), sk);
				CHECK_MESSAGE(geom::endpoint_at(sbb.seg, zb) == int(sv["big4_endpoint_at"].get<double>()), sk);
				CHECK_MESSAGE(bs(geom::blocked_sides(sbb.seg, 1, dirb)) == sv["big4_blocked_sides"].get<std::string>(), sk);
				CHECK_MESSAGE(beam_str(effects::apply(bb, l4.like(t), sbb)) == sv["big4_apply"].get<std::string>(), sk);
				if (!hb.empty()) {
					CHECK_MESSAGE(beam_str(effects::apply(bb, hb[0], sbb)) == sv["big4_apply_at_root"].get<std::string>(), sk);
				}
			}
		}
		cases++;
	}
	CHECK(cases == 200);
}

namespace {

template <Num N>
std::pair<std::vector<Event<N>>, std::vector<double>> walk_fixture(const ojson &fx, const ojson &inp, const std::vector<SurfaceDef> &defs, const N &like) {
	Scene<N> scene = Scene<N>::from_defs(defs, like);
	SolveCache<N> cache;
	Solver<N> solver(scene, &cache);
	const CN<N> player{like.like(fh(inp["player"][0])), like.like(fh(inp["player"][1]))};
	std::optional<CN<N>> cursor;
	if (!fx["cursor"].is_null()) {
		cursor = CN<N>{like.like(fh(inp["cursor"][0])), like.like(fh(inp["cursor"][1]))};
	}
	const Mode mode = fx["mode"].get<std::string>() == "physical" ? Mode::Physical : Mode::Planned;
	std::vector<PlanItem> plan = plan_from_json(fx["plan"]);
	for (size_t k = 0; k < plan.size() && k < inp["clicks"].size(); k++) {
		if (!inp["clicks"][k].is_null()) {
			plan[k].hint = effects::F32Point{float(fh(inp["clicks"][k][0])), float(fh(inp["clicks"][k][1]))};
		}
	}
	const Flags flags = fx.contains("flags") ? flags_from_json(fx["flags"]) : Flags();
	if (!fx["direction"].is_null()) {
		Walk<N> w = solver.run_direction(player, CN<N>{like.like(fh(inp["direction"][0])), like.like(fh(inp["direction"][1]))}, plan, cursor, mode, MAX_HITS, flags);
		return {w.events, w.gains};
	}
	std::optional<Walk<N>> w = solver.run(player, *cursor, plan, mode, MAX_HITS, flags);
	if (!w) {
		return {{}, {}};
	}
	return {w->events, w->gains};
}

} // namespace

TEST_CASE("goldens: every stage fixture, generic f64, the float64 walk with gains, and 4 limbs") {
	const ojson gold = load_json("bench/goldens/fixtures.json");
	const char *files[] = {"stage81_rules", "stage82_portals", "stage83_projective", "stage85_flags", "stage86_ghost", "stage87_projective_plans", "stage87_click_forks"};
	int fixtures = 0;
	for (const char *f : files) {
		const ojson fxs = load_json(std::string("bench/fixtures/") + f + ".json");
		for (const ojson &fx : fxs["fixtures"]) {
			const std::string key = std::string(f) + "/" + fx["name"].get<std::string>();
			const ojson &inp = gold[key]["inputs"];
			const std::vector<SurfaceDef> defs = defs_of(fx["surfaces"], inp);
			const auto r64 = walk_fixture<F64>(fx, inp, defs, F64{1});
			const std::string g64 = encode_events(r64.first, nullptr);
			rebless_json("fixtures", key, "f64", g64, gold[key]["f64"].dump());
			CHECK_MESSAGE(g64 == gold[key]["f64"].dump(), (key + " f64: " + first_event_diff(g64, gold[key]["f64"].dump())));
			const auto rd = walk_fixture<Dual>(fx, inp, defs, Dual{1, 0});
			const std::string gd = encode_events(rd.first, &rd.second);
			rebless_json("fixtures", key, "fast", gd, gold[key]["fast"].dump());
			CHECK_MESSAGE(gd == gold[key]["fast"].dump(), (key + " fast: " + first_event_diff(gd, gold[key]["fast"].dump())));
			const auto rb = walk_fixture<BigFloat>(fx, inp, defs, BigFloat::from_int(1, 4));
			const std::string gb = encode_events(rb.first, nullptr);
			rebless_json("fixtures", key, "big4", gb, gold[key]["big4"].dump());
			CHECK_MESSAGE(gb == gold[key]["big4"].dump(), (key + " big4: " + first_event_diff(gb, gold[key]["big4"].dump())));
			fixtures++;
		}
	}
	CHECK(fixtures == 115);
}

TEST_CASE("goldens: the corpus: float64 walks, the ladder, and 4 limbs") {
	const ojson gold = load_json("bench/goldens/corpus.json");
	int scenarios = 0;
	int unpinned = 0;
	for (const char *sub : {"", "stress", "scenes_corpus"}) {
		const std::string dir = repo_root() + "/bench/" + (std::string(sub).empty() ? "" : std::string(sub) + "/") + "scenarios";
		std::vector<std::string> names;
		for (const auto &e : std::filesystem::directory_iterator(dir)) {
			if (e.path().extension() == ".json") {
				names.push_back(e.path().stem().string());
			}
		}
		std::sort(names.begin(), names.end());
		for (const std::string &name : names) {
			ojson scn;
			{
				std::ifstream in(dir + "/" + name + ".json");
				scn = ojson::parse(in);
			}
			const std::string key = std::string(std::string(sub).empty() ? "main" : sub) + "/" + name;
			if (!gold.contains(key)) {
				// a scenario added after tag pre-native (Stage 103: the scenes
				// dumped since): no GDScript golden; the GUT corpus test checks it
				// against the oracle
				unpinned++;
				continue;
			}
			const ojson &inp = gold[key]["inputs"];
			const std::vector<SurfaceDef> defs = defs_of(scn["surfaces"], inp);
			std::vector<PlanItem> plan;
			for (const ojson &e : scn["plan"]) {
				plan.push_back(PlanItem{e["surface_id"].get<int>(), e["side"].get<std::string>() == "LEFT", std::nullopt});
			}
			const double px = fh(inp["player"][0]), py = fh(inp["player"][1]), cx = fh(inp["cursor"][0]), cy = fh(inp["cursor"][1]);
			{
				Scene<Dual> sc = Scene<Dual>::from_defs(defs, Dual{1, 0});
				SolveCache<Dual> cache;
				Solver<Dual> sv(sc, &cache);
				const CN<Dual> P{Dual{px, 0}, Dual{py, 0}}, C{Dual{cx, 0}, Dual{cy, 0}};
				auto wp = sv.run(P, C, plan, Mode::Planned);
				const std::string gp = wp ? encode_events(wp->events, &wp->gains) : encode_events(std::vector<Event<Dual>>{}, nullptr);
				rebless_json("corpus", key, "fast_planned", gp, gold[key]["fast_planned"].dump());
				CHECK_MESSAGE(gp == gold[key]["fast_planned"].dump(), (key + " fast_planned: " + first_event_diff(gp, gold[key]["fast_planned"].dump())));
				auto wh = sv.run(P, C, plan, Mode::Physical);
				const std::string gh = wh ? encode_events(wh->events, &wh->gains) : encode_events(std::vector<Event<Dual>>{}, nullptr);
				rebless_json("corpus", key, "fast_physical", gh, gold[key]["fast_physical"].dump());
				CHECK_MESSAGE(gh == gold[key]["fast_physical"].dump(), (key + " fast_physical: " + first_event_diff(gh, gold[key]["fast_physical"].dump())));
			}
			{
				LadderContext lctx(defs);
				Ladder lad(lctx, []() { return 0LL; });
				const LadderResult lr = lad.solve(px, py, cx, cy, plan, 32, 32, 0.01, false);
				const ojson &gl = gold[key]["ladder"];
				rebless_json("corpus", key, "ladder/planned", climb_events(lr.planned), gl["planned"].dump());
				CHECK_MESSAGE(climb_events(lr.planned) == gl["planned"].dump(), (key + " ladder planned"));
				rebless_json("corpus", key, "ladder/physical", climb_events(lr.physical), gl["physical"].dump());
				CHECK_MESSAGE(climb_events(lr.physical) == gl["physical"].dump(), (key + " ladder physical"));
				rebless_json("corpus", key, "ladder/planned_err", ojson(packed(lr.planned.err)).dump(), gl["planned_err"].dump());
				CHECK_MESSAGE(packed(lr.planned.err) == gl["planned_err"].get<std::string>(), (key + " ladder planned_err"));
				rebless_json("corpus", key, "ladder/physical_err", ojson(packed(lr.physical.err)).dump(), gl["physical_err"].dump());
				CHECK_MESSAGE(packed(lr.physical.err) == gl["physical_err"].get<std::string>(), (key + " ladder physical_err"));
				rebless_json("corpus", key, "ladder/planned_tier", ojson(lr.planned.limbs).dump(), ojson(int(gl["planned_tier"].get<double>())).dump());
				CHECK_MESSAGE(lr.planned.limbs == int(gl["planned_tier"].get<double>()), (key + " ladder planned_tier"));
				rebless_json("corpus", key, "ladder/physical_tier", ojson(lr.physical.limbs).dump(), ojson(int(gl["physical_tier"].get<double>())).dump());
				CHECK_MESSAGE(lr.physical.limbs == int(gl["physical_tier"].get<double>()), (key + " ladder physical_tier"));
				rebless_json("corpus", key, "ladder/converged", ojson(lr.physical.converged).dump(), gl["converged"].dump());
				CHECK_MESSAGE(lr.physical.converged == gl["converged"].get<bool>(), (key + " ladder converged"));
				ojson tried = ojson::array();
				for (int tt : lr.physical.tried) {
					tried.push_back(tt);
				}
				rebless_json("corpus", key, "ladder/tiers_tried", tried.dump(), gl["tiers_tried"].dump());
				CHECK_MESSAGE(tried.dump() == gl["tiers_tried"].dump(), (key + " ladder tiers_tried"));
				rebless_json("corpus", key, "ladder/planned_from_physical", ojson(lr.planned_from_physical).dump(), gl["planned_from_physical"].dump());
				CHECK_MESSAGE(lr.planned_from_physical == gl["planned_from_physical"].get<bool>(), (key + " ladder planned_from_physical"));
			}
			{
				const BigFloat l = BigFloat::from_int(1, 4);
				Scene<BigFloat> sc = Scene<BigFloat>::from_defs(defs, l);
				SolveCache<BigFloat> cache;
				Solver<BigFloat> sv(sc, &cache);
				const CN<BigFloat> P{l.like(px), l.like(py)}, C{l.like(cx), l.like(cy)};
				const std::string gp = encode_events(sv.planned(P, C, plan), nullptr);
				rebless_json("corpus", key, "big4_planned", gp, gold[key]["big4_planned"].dump());
				CHECK_MESSAGE(gp == gold[key]["big4_planned"].dump(), (key + " big4_planned: " + first_event_diff(gp, gold[key]["big4_planned"].dump())));
				const std::string gh = encode_events(sv.physical(P, C, plan), nullptr);
				rebless_json("corpus", key, "big4_physical", gh, gold[key]["big4_physical"].dump());
				CHECK_MESSAGE(gh == gold[key]["big4_physical"].dump(), (key + " big4_physical: " + first_event_diff(gh, gold[key]["big4_physical"].dump())));
			}
			scenarios++;
		}
	}
	CHECK(scenarios == 549); // every pinned scenario was replayed
	MESSAGE("corpus: " << scenarios << " pinned scenarios replayed, " << unpinned << " added since (oracle-checked in GUT)");
}

namespace {

template <Num N>
void check_chain(const ojson &scn, const ojson &inputs, const ojson &want, const N &like, const std::string &key) {
	const std::vector<SurfaceDef> defs = defs_of(scn["surfaces"], inputs);
	std::vector<PlanItem> plan;
	for (const ojson &e : scn["plan"]) {
		plan.push_back(PlanItem{e["surface_id"].get<int>(), e["side"].get<std::string>() == "LEFT", std::nullopt});
	}
	Scene<N> sc = Scene<N>::from_defs(defs, like);
	Solver<N> sv(sc, nullptr);
	const CN<N> P{like.like(fh(inputs["player"][0])), like.like(fh(inputs["player"][1]))};
	const CN<N> C{like.like(fh(inputs["cursor"][0])), like.like(fh(inputs["cursor"][1]))};
	std::optional<CN<N>> X = C;
	size_t i = 0;
	for (size_t jj = plan.size(); jj-- > 0 && i < want["images"].size(); i++) {
		X = effects::map_point(X, *sc.find(plan[jj].surface_id), true);
		CHECK_MESSAGE(cn(X) == want["images"][i].get<std::string>(), (key + " image " + std::to_string(i)));
		if (!X) {
			break;
		}
	}
	effects::Poles<N> pole{P, std::nullopt, std::nullopt};
	for (size_t j = 0; j < plan.size() && j < want["poles"].size(); j++) {
		pole = effects::map_pole(pole, *sc.find(plan[j].surface_id));
		CHECK_MESSAGE(cn(pole.P) + "|" + cn(pole.Q) + "|" + cn(pole.delta) == want["poles"][j].get<std::string>(), (key + " pole " + std::to_string(j)));
		if (!pole.P) {
			break;
		}
	}
	const auto chain = sv.anchor_chain(P, C, plan, Flags());
	REQUIRE_MESSAGE(bool(chain) == !want.is_null(), (key + " chain exists"));
	if (!chain) {
		return;
	}
	std::map<const Beam<N> *, int> ids;
	std::vector<std::string> beams;
	std::string idx;
	for (const auto &bm : chain->beams) {
		if (!ids.count(bm.get())) {
			ids[bm.get()] = int(beams.size());
			beams.push_back(cn(bm->p) + ";" + cn(bm->tau) + ";" + num(bm->kappa));
		}
		idx += std::to_string(ids[bm.get()]) + ",";
	}
	std::string widx;
	for (const auto &x : want["index"]) {
		widx += std::to_string(int(x.get<double>())) + ",";
	}
	CHECK_MESSAGE(beams.size() == want["beams"].size(), (key + " beam count"));
	for (size_t k = 0; k < std::min(beams.size(), want["beams"].size()); k++) {
		CHECK_MESSAGE(beams[k] == want["beams"][k].get<std::string>(), (key + " beam " + std::to_string(k)));
	}
	CHECK_MESSAGE(idx == widx, (key + " beam index"));
}

} // namespace

TEST_CASE("goldens: anchored chains, image chains and pole chains of the main corpus") {
	const ojson gold = load_json("bench/goldens/chain.json");
	int n = 0;
	for (auto it = gold.begin(); it != gold.end(); ++it) {
		if (it.key() == "_schema") {
			continue;
		}
		ojson scn;
		{
			std::ifstream in(repo_root() + "/bench/scenarios/" + it.key() + ".json");
			scn = ojson::parse(in);
		}
		const ojson &inputs = it.value()["inputs"];
		if (!it.value()["f64"].is_null()) {
			check_chain<F64>(scn, inputs, it.value()["f64"], F64{1}, it.key() + "/f64");
		}
		if (!it.value()["big4"].is_null()) {
			check_chain<BigFloat>(scn, inputs, it.value()["big4"], BigFloat::from_int(1, 4), it.key() + "/big4");
		}
		n++;
	}
	CHECK(n == 161);
}

// ---------------------------------------------------------------- sweeps (Stage 96)

#include "rc/tracer.hpp"
#include "sha256.hpp"

namespace {

// tools/dump_goldens.gd: h32v / h64 / step_canon / path_canon / pair_canon / sha16.
std::string h32v(const V2 &v) {
	unsigned char b[8];
	std::memcpy(b, &v.x, 4);
	std::memcpy(b + 4, &v.y, 4);
	char out[17];
	for (int i = 0; i < 8; i++) {
		std::snprintf(out + i * 2, 3, "%02x", b[i]);
	}
	return std::string(out, 16);
}
std::string tf(bool v) { return v ? "true" : "false"; }
std::string int_list(std::vector<int> v) {
	std::sort(v.begin(), v.end());
	std::string s = "[";
	for (size_t i = 0; i < v.size(); i++) {
		if (i) {
			s += ", ";
		}
		s += std::to_string(v[i]);
	}
	return s + "]";
}
std::string step_canon(const TraceStep &s, const std::vector<SurfaceDef> &defs) {
	std::string hit = "-";
	if (s.hit) {
		const TraceHit &h = *s.hit;
		std::string seg = "-";
		for (const SurfaceDef &d : defs) {
			if (d.id == h.surface_id) {
				seg = h32v(V2::of(d.sx, d.sy)) + h32v(V2::of(d.ex, d.ey));
				break;
			}
		}
		hit = hex(h.t) + ";" + h32v(h.point) + ";" + seg + ";" + std::to_string(int(h.side)) + ";" + tf(h.on_segment);
	}
	return h32v(s.start) + "," + h32v(s.end) + "," + h32v(s.via) + "," + std::to_string(s.frame_id) + "," + std::to_string(int(s.type)) + "," +
		std::to_string(s.surface_id) + "," + std::to_string(s.hit_side) + "," + tf(s.hit_on_segment) + "," + tf(s.after_portal) + "," + tf(s.ghost) + "," +
		tf(s.is_arc_step) + "," + hex(s.est_error) + "," + std::to_string(s.precision_bits) + "," + hit;
}
std::string path_canon(const TracePath &p, const std::vector<SurfaceDef> &defs) {
	std::string steps;
	for (size_t i = 0; i < p.steps.size(); i++) {
		if (i) {
			steps += "|";
		}
		steps += step_canon(p.steps[i], defs);
	}
	return "ci=" + std::to_string(p.cursor_index) + ";hc=" + std::to_string(p.hit_count) + ";targets=" + int_list(p.targets_hit) + ";steps=" + steps;
}
std::string pair_canon(const std::optional<TracePair> &pair, const std::vector<SurfaceDef> &defs) {
	if (!pair) {
		return "null";
	}
	const LadderResult &r = pair->result;
	return "P:" + path_canon(pair->planned, defs) + "\nH:" + path_canon(pair->physical, defs) + "\nF:" + pair->final_flags.canonical_key() + "\nR:" +
		std::to_string(r.planned.limbs) + "," + std::to_string(r.physical.limbs) + "," + tf(r.physical.converged) + "," + int_list(r.physical.tried);
}
std::string sha16(const std::string &s) { return rc_test::Sha256::hex(s).substr(0, 16); }
// the first differing position of two canonical strings, with context
std::string first_diff(const std::string &got, const std::string &want) {
	size_t i = 0;
	while (i < got.size() && i < want.size() && got[i] == want[i]) {
		i++;
	}
	const size_t from = i > 60 ? i - 60 : 0;
	return "at " + std::to_string(i) + "\n  got  ..." + got.substr(from, 200) + "\n  want ..." + want.substr(from, 200);
}

V2 v2_of_hex(const ojson &pair) {
	float f[2];
	for (int k = 0; k < 2; k++) {
		const std::string h = pair[size_t(k)].get<std::string>();
		unsigned char b[4];
		for (int i = 0; i < 4; i++) {
			b[i] = static_cast<unsigned char>(std::stoul(h.substr(size_t(i) * 2, 2), nullptr, 16));
		}
		std::memcpy(&f[k], b, 4);
	}
	return V2{f[0], f[1]};
}

} // namespace

TEST_CASE("sha256: FIPS 180-4 test vectors") {
	CHECK(rc_test::Sha256::hex("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
	CHECK(rc_test::Sha256::hex("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
	CHECK(rc_test::Sha256::hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") == "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
}

// Every (player, cursor, plan) combo of the ten sweep tests, as BeamTracer.trace_pair
// produced it in GDScript: planned + physical TracePaths (float32 bit patterns and all
// meta fields), the ladder's tiers and climb record, the final flags. RC_SWEEP_STRIDE
// thins the combos (default: the goldens' own stride, i.e. all of them).
TEST_CASE("goldens: the sweep combos of Tracer.trace_pair") {
	const ojson gold = load_json("bench/goldens/sweeps.json");
	int extra_stride = 1;
	if (const char *s = std::getenv("RC_SWEEP_STRIDE")) {
		extra_stride = std::max(1, std::atoi(s));
	}
	// RC_SWEEP_PART=k/n: replay every n-th of the selected combos, from the k-th
	// (ctest runs the parts as separate entries, in parallel)
	std::set<std::pair<std::string, long>> dump_list;
	if (const char *s = std::getenv("RC_SWEEP_DUMP")) {
		std::ifstream in(s);
		std::string line;
		while (std::getline(in, line)) {
			const size_t tab = line.find('\t');
			if (tab != std::string::npos) {
				dump_list.emplace(line.substr(0, tab), std::atol(line.c_str() + tab + 1));
			}
		}
	}
	long part_k = 0;
	long part_n = 1;
	if (const char *s = std::getenv("RC_SWEEP_PART")) {
		if (std::sscanf(s, "%ld/%ld", &part_k, &part_n) != 2 || part_n < 1 || part_k < 0 || part_k >= part_n) {
			part_k = 0;
			part_n = 1;
		}
	}
	long selected = 0;
	int groups = 0;
	int skipped = 0;
	long combos = 0;
	long mismatches = 0;
	std::string first_mismatch;
	std::vector<std::string> mismatch_keys;
	for (auto it = gold.begin(); it != gold.end(); ++it) {
		if (it.key() == "_schema") {
			continue;
		}
		const ojson &g = it.value();
		if (!g.contains("inputs")) {
			skipped++;
			continue;
		}
		groups++;
		const ojson &inputs = g["inputs"];
		const std::vector<SurfaceDef> defs = defs_of(inputs["defs"], inputs);
		std::vector<V2> positions;
		for (const ojson &p : inputs["positions"]) {
			positions.push_back(v2_of_hex(p));
		}
		std::vector<PlanItem> plan;
		for (const ojson &e : g["plan"]) {
			plan.push_back(PlanItem{e["surface_id"].get<int>(), e["side"].get<std::string>() == "LEFT", std::nullopt});
		}
		const int stride = g["stride"].get<int>();
		const bool skip_equal = g["skip_equal"].get<bool>();
		const std::string hashes = g["hashes"].get<std::string>();
		const ojson &samples = g["samples"];
		Tracer tracer;
		tracer.set_defs(defs);
		long idx = 0;
		size_t k = 0;
		for (const V2 &p : positions) {
			for (const V2 &c : positions) {
				if (skip_equal && p == c) {
					continue;
				}
				const long i = idx++;
				if (i % stride != 0) {
					continue;
				}
				const size_t hk = k++;
				if ((i / stride) % extra_stride != 0) {
					continue;
				}
				if (selected++ % part_n != part_k) {
					continue;
				}
				const std::optional<TracePair> pair = tracer.trace_pair(p, c, plan, Flags());
				if (dump_list.count({it.key(), i})) {
					ojson d;
					d["group"] = it.key();
					d["combo"] = i;
					d["player"] = ojson::array({hex(p.x), hex(p.y)});
					d["cursor"] = ojson::array({hex(c.x), hex(c.y)});
					d["plan"] = g["plan"];
					d["defs"] = dump_defs(defs);
					if (pair) {
						d["planned"] = dump_climb(pair->result.planned);
						d["physical"] = dump_climb(pair->result.physical);
					} else {
						d["planned"] = nullptr;
						d["physical"] = nullptr;
					}
					std::printf("DUMP\t%s\n", d.dump().c_str());
				}
				const std::string canon = pair_canon(pair, defs);
				combos++;
				const std::string key = it.key() + " combo " + std::to_string(i);
				if (samples.contains(std::to_string(i))) {
					const std::string want = samples[std::to_string(i)].get<std::string>();
					CHECK_MESSAGE(canon == want, (key + " sample: " + first_diff(canon, want)));
				}
				if (hk * 16 + 16 > hashes.size() || sha16(canon) != hashes.substr(hk * 16, 16)) {
					mismatches++;
					// RC_SWEEP_REBLESS=1: print what an approved behaviour change
					// regenerates (group, hash slot, combo, new hash, the canon as a
					// JSON string: it spans lines) for docs/research/apply_rebless.py
					if (std::getenv("RC_SWEEP_REBLESS")) {
						std::printf("REBLESS\t%s\t%zu\t%ld\t%s\t%s\n", it.key().c_str(), hk, i, sha16(canon).c_str(), ojson(canon).dump().c_str());
					}
					if (mismatch_keys.size() < 12) {
						mismatch_keys.push_back(key + " tiers " + std::to_string(pair ? pair->result.planned.limbs : -1) + "/" +
							std::to_string(pair ? pair->result.physical.limbs : -1));
					}
					if (first_mismatch.empty()) {
						first_mismatch = key + ": " + canon.substr(0, 400);
					}
				}
			}
		}
		CHECK_MESSAGE(k * 16 == hashes.size(), (it.key() + ": hash count"));
	}
	MESSAGE("sweeps: " << groups << " groups, " << combos << " combos, " << skipped << " groups without inputs (old goldens), " << mismatches << " mismatches");
	for (const std::string &k : mismatch_keys) {
		MESSAGE("  mismatch: " << k);
	}
	CHECK_MESSAGE(mismatches == 0, ("first mismatch: " + first_mismatch));
	CHECK(groups + skipped == 117);
}
