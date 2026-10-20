// Stage 119: the GUT suite's solver scenarios, natively (docs/research/gut_ledger.tsv:
// the `capture` rows). Each was recorded from its GUT test in the GDScript game
// (tools/capture_gut_traces.py -> bench/gut_traces): every trace_pair call's
// inputs and output, and the crossings probe's pairs with the GDScript answers.
// Replayed here on rc::Tracer / rcg::crossings_of: bit for bit.
#include "json_defs.hpp"

#include "rcg/digest.hpp"
#include "rcg/geom.hpp"
#include "rcg/trace_json.hpp"

#include <doctest/doctest.h>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

using json = nlohmann::json;
namespace fs = std::filesystem;

const fs::path TRACES = fs::path(RC_REPO_ROOT) / "bench" / "gut_traces";

// a digest float ("f" + the float64's little-endian hex) or a JSON number
double num(const json &v) {
	if (v.is_string()) {
		const std::string h = v.get<std::string>().substr(1);
		unsigned char b[8];
		for (int i = 0; i < 8; i++) {
			b[i] = static_cast<unsigned char>(std::stoul(h.substr(size_t(i) * 2, 2), nullptr, 16));
		}
		double d;
		std::memcpy(&d, b, 8);
		return d;
	}
	return v.get<double>();
}
rc::V2 v2(const json &a) { return rc::V2::of(num(a[0]), num(a[1])); }

rcg::SegGeom seg_of(const json &d) {
	if (d.contains("center")) {
		return rc::arc::SegGeom::full_from_carrier(rc::arc::from_circle(v2(d["center"]), num(d["radius"])));
	}
	return rc::arc::SegGeom::make(v2(d["start"]), v2(d["end"]), v2(d["via"]));
}

// the crossings probe: crossings, params and incidence of points, spans -- as GDScript computed them
void check_crossings(const json &row) {
	const rcg::SegGeom sa = seg_of(row["a"]), sb = seg_of(row["b"]);
	const json &gd = row["gd"];
	const std::vector<rcg::Crossing> xs = rcg::crossings_of(sa, sb);
	REQUIRE(xs.size() == gd["crossings"].size());
	for (size_t i = 0; i < xs.size(); i++) {
		const json &g = gd["crossings"][i];
		CHECK(xs[i].point == v2(g[0]));
		CHECK(xs[i].ta == num(g[1]));
		CHECK(xs[i].tb == num(g[2]));
		CHECK(xs[i].enters == g[3].get<bool>());
	}
	for (size_t i = 0; i < row["points"].size(); i++) {
		const rc::V2 p = v2(row["points"][i]);
		CHECK(rcg::param(sa, p) == num(gd["params"][i]));
		CHECK(rcg::incident(sa, p) == gd["incident"][i].get<bool>());
	}
	CHECK(rcg::span(sa) == num(gd["span_a"]));
	CHECK(rcg::span(sb) == num(gd["span_b"]));
}

// a trace_pair call: the same inputs give the same pair, bit for bit
void check_trace(const json &row, const std::string &line) {
	const ojson in = ojson::parse(line); // in order (a resolver's table order matters)
	const std::vector<rc::SurfaceDef> defs = rc::defs_from_json(in["defs"]);
	CHECK(rcg::trace_json::defs(defs) == in["defs"]); // the defs read back as written
	rc::Tracer t;
	t.set_emit_infinity(row["emit_infinity"].get<bool>());
	t.set_defs(defs);
	const std::optional<rc::TracePair> pair =
		t.trace_pair(v2(row["player"]), v2(row["cursor"]), rc::plan_from_json(in["plan"]), rc::flags_from_json(in["flags"]));
	// (the output is held in the digest encoding: an infinite end point, a signed zero)
	CHECK(rcg::digest::encode(rcg::trace_json::pair(pair)) == row["out"].get<std::string>());
}

} // namespace

TEST_CASE("gut traces: the GUT suite's solver scenarios replay bit for bit") {
	REQUIRE(fs::is_directory(TRACES));
	int files = 0, rows = 0;
	for (const fs::directory_entry &e : fs::directory_iterator(TRACES)) {
		if (e.path().extension() != ".jsonl") {
			continue;
		}
		files++;
		std::ifstream in(e.path());
		std::string line;
		while (std::getline(in, line)) {
			if (line.empty()) {
				continue;
			}
			const json row = json::parse(line);
			INFO(row["key"].get<std::string>());
			if (row.value("kind", std::string("trace")) == "crossings") {
				check_crossings(row);
			} else {
				check_trace(row, line);
			}
			rows++;
		}
	}
	CHECK(files == 26);
	CHECK(rows == 3600);
}
