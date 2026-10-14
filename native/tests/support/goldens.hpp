// Shared encoders for the goldens tests: the compact formats tools/dump_goldens.gd writes.
#pragma once
#include "json_defs.hpp"
#include "rc/ladder.hpp"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <string>
#include <vector>
namespace rc::test {
inline std::string repo_root() { return RC_REPO_ROOT; }
inline ojson load_json(const std::string &rel) {
	std::ifstream in(repo_root() + "/" + rel);
	if (!in) { throw std::runtime_error("cannot open " + rel); }
	return ojson::parse(in);
}
inline std::string hex(double d) { unsigned char b[8]; std::memcpy(b, &d, 8); char out[17]; for (int i = 0; i < 8; i++) { std::snprintf(out + i * 2, 3, "%02x", b[i]); } return std::string(out, 16); }
inline std::string num(F64 x) { return "f" + hex(x.v); }
inline std::string num(Dual x) { return "f" + hex(x.v); } // the shipped float64 walk exposes values
inline std::string num(const BigFloat &x) { return "b" + x.debug_string(); }
template <Num N> std::string cn(const CN<N> &z) { return num(z.re) + "/" + num(z.im); }
template <Num N> std::string cn(const std::optional<CN<N>> &z) { return z ? cn(*z) : "null"; }
template <Num N> std::string num(const std::optional<N> &t) { return t ? num(*t) : "null"; }
template <Num N> std::string nums(const std::vector<N> &v) { std::string s = "["; for (size_t i = 0; i < v.size(); i++) { if (i) { s += ","; } s += num(v[i]); } return s + "]"; }
inline std::string packed(const std::vector<double> &v) { std::string s = "["; for (size_t i = 0; i < v.size(); i++) { if (i) { s += ","; } s += hex(v[i]); } return s + "]"; }
inline std::string b(bool v) { return v ? "1" : "0"; }
// {"beams": [...], "events": [...]} as JSON text, the goldens' event encoding
template <Num N> std::string encode_events(const std::vector<Event<N>> &evs, const std::vector<double> *gains) {
	std::map<int, int> ids; std::vector<std::string> beams, out;
	for (size_t i = 0; i < evs.size(); i++) {
		const Event<N> &e = evs[i];
		if (e.beam && !ids.count(e.beam_view)) { ids[e.beam_view] = int(beams.size()); beams.push_back(cn(e.beam->p) + ";" + cn(e.beam->tau) + ";" + num(e.beam->kappa)); }
		std::string s = std::string(event_kind_name(e.kind)) + ";" + std::to_string(e.surface_id) + ";" + b(e.left) + ";" + b(e.applied) + ";" + b(e.terminal) + ";" + b(e.on_segment) + ";" + b(e.rehit) + ";" + b(e.popped) + ";" + b(e.teleport) + ";" + b(e.projective) + ";" + b(e.state) + ";" + b(e.anchored) + ";" + b(e.seeded) + ";" + std::to_string(e.epoch) + ";" + std::to_string(e.beam ? ids[e.beam_view] : -1) + ";" + hex(e.margin) + ";" + cn(e.point) + ";" + num(e.t_from) + ";" + num(e.t_to) + ";" + e.flags.canonical_key();
		if (gains && i < gains->size()) { s += ";" + hex((*gains)[i]); }
		out.push_back(s);
	}
	ojson j; j["beams"] = beams; j["events"] = out; return j.dump();
}
inline std::string climb_events(const Climb &c) { return c.limbs == 0 ? encode_events(c.f64_events, nullptr) : encode_events(c.big_events, nullptr); }
// Report the first differing event of two encoded event lists.
inline std::string first_event_diff(const std::string &got, const std::string &want) {
	ojson g = ojson::parse(got), w = ojson::parse(want);
	for (size_t i = 0; i < std::max(g["events"].size(), w["events"].size()); i++) {
		std::string a = i < g["events"].size() ? g["events"][i].get<std::string>() : "-";
		std::string bb = i < w["events"].size() ? w["events"][i].get<std::string>() : "-";
		if (a != bb) { return "event " + std::to_string(i) + "\n  got  " + a + "\n  want " + bb; }
	}
	return "beams differ";
}
} // namespace rc::test
