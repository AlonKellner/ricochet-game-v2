#include "rcg/model.hpp"

#include <algorithm>

namespace rcg {

EffectKind kind_of(const BuiltEffect &e) {
	if (e.type == "pass") {
		return EffectKind::Pass;
	}
	if (e.type == "terminal") {
		return EffectKind::Terminal;
	}
	if (e.type == "reflection" || e.type == "inversion" || e.type == "portal") {
		return EffectKind::Transformative;
	}
	return EffectKind::Projective;
}

bool self_inverse(const BuiltEffect &e) { return e.type == "reflection" || e.type == "inversion"; }

rc::FlagValue flag_value(const FlagValue &v) {
	if (const bool *b = std::get_if<bool>(&v)) {
		return *b;
	}
	if (const std::int64_t *i = std::get_if<std::int64_t>(&v)) {
		return double(*i);
	}
	if (const double *d = std::get_if<double>(&v)) {
		return *d;
	}
	return std::get<std::string>(v);
}

const BuiltSide *active_side(const BuiltSurface &s, Side side, const rc::Flags &flags) {
	if (s.resolver.empty()) {
		const std::optional<BuiltSide> &cfg = side == Side::Left ? s.left : s.right;
		return cfg ? &*cfg : nullptr;
	}
	const BuiltRow *row = nullptr;
	if (const rc::FlagValue *v = flags.get(s.resolver_key)) {
		const std::string k = rc::flag_key(*v);
		for (const BuiltRow &r : s.resolver) {
			if (r.value == k) {
				row = &r;
				break;
			}
		}
	}
	if (row == nullptr) {
		for (const BuiltRow &r : s.resolver) {
			if (r.value == "DEFAULT") {
				row = &r;
				break;
			}
		}
	}
	if (row == nullptr) {
		row = &s.resolver.front();
	}
	const std::optional<BuiltSide> &cfg = side == Side::Left ? row->left : row->right;
	return cfg ? &*cfg : nullptr;
}

bool present(const BuiltSurface &s, const rc::Flags &flags) { return holds(s.presence, flags); }

bool holds(const std::vector<PresenceSpec> &conditions, const rc::Flags &flags) {
	for (const PresenceSpec &c : conditions) {
		const rc::FlagValue *v = flags.get(c.key);
		const std::string k = v ? rc::flag_key(*v) : std::string("DEFAULT");
		if (std::find(c.values.begin(), c.values.end(), k) == c.values.end()) {
			return false;
		}
	}
	return true;
}

std::vector<rc::StateChangeDef> hit_writes(const HitSpec &h, int host_id) {
	std::vector<rc::StateChangeDef> out;
	{
		if (h.kind == "state") {
			const bool toggle = h.op == "toggle";
			out.push_back({h.key, toggle, flag_value(h.value)});
			for (const PresenceSpec &c : h.when) { // Stage 156
				out.back().when.push_back(rc::FlagCond{c.key, c.values});
			}
		} else if (h.kind == "target") {
			out.push_back({"target:" + std::to_string(host_id), false, true});
		} else if (h.kind == "reset") {
			for (const std::string &c : h.channels) {
				out.push_back({"chan:" + c, false, std::string("armed")});
			}
		} else if (h.kind == "spawn") {
			out.push_back({"chan:" + h.channel, false, std::string("pending")});
		}
	}
	return out;
}

std::vector<rc::StateChangeDef> writes(const BuiltSide &side, int host_id, bool deferred) {
	std::vector<rc::StateChangeDef> out;
	for (const HitSpec &h : side.hits) {
		if (h.deferred == deferred) {
			for (rc::StateChangeDef &w : hit_writes(h, host_id)) {
				out.push_back(std::move(w));
			}
		}
	}
	return out;
}

bool writes_key(const BuiltSide &side, const std::string &key, int host_id) {
	for (bool deferred : {false, true}) {
		for (const rc::StateChangeDef &w : writes(side, host_id, deferred)) {
			if (w.key == key) {
				return true;
			}
		}
	}
	return false;
}

std::optional<OverlayLink> overlay_link(const BuiltSurface &s, Side side, const rc::Flags &flags) {
	const BuiltSide *cfg = active_side(s, side, flags);
	if (cfg == nullptr || !cfg->interactive) {
		return std::nullopt;
	}
	const SideRef from{s.id, side};
	// the explicit links: a portal pair's sides link across (source LEFT <-> image RIGHT)
	if (s.portal_pair != 0) {
		return OverlayLink{from, SideRef{s.portal_pair, opposite(side)}};
	}
	const BuiltEffect &e = cfg->effect;
	switch (kind_of(e)) {
		case EffectKind::Pass:
			return OverlayLink{from, SideRef{s.id, opposite(side)}};
		case EffectKind::Terminal:
			return OverlayLink{from, std::nullopt};
		case EffectKind::Transformative:
			if (self_inverse(e)) {
				return OverlayLink{from, from};
			}
			return OverlayLink{from, SideRef{s.id, opposite(side)}};
		case EffectKind::Projective:
			return OverlayLink{from, e.back ? from : SideRef{s.id, opposite(side)}};
	}
	return std::nullopt;
}

} // namespace rcg
