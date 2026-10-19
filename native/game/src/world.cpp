#include "rcg/world.hpp"

#include <algorithm>

namespace rcg {

World::World(std::vector<BuiltSurface> static_surfaces) : static_(std::move(static_surfaces)) {
	for (const BuiltSurface &s : static_) {
		next_id_ = std::max(next_id_, s.id + 1);
	}
	rebuild();
	initial_ = snapshot();
}

const BuiltSurface *World::by_id(int id) const {
	for (const BuiltSurface &s : surfaces_) {
		if (s.id == id) {
			return &s;
		}
	}
	return nullptr;
}

const World::Group *World::group_of(const std::string &channel) const {
	auto it = groups_.find(channel);
	return it == groups_.end() ? nullptr : &it->second;
}

const Pieces &World::pieces() const {
	if (!pieces_ || pieces_version_ != version_) {
		pieces_ = std::make_unique<Pieces>(build_pieces(surfaces_));
		pieces_version_ = version_;
	}
	return *pieces_;
}

const std::vector<rc::SurfaceDef> &World::defs() const {
	if (defs_version_ != version_) {
		defs_ = defs_of(surfaces_);
		defs_version_ = version_;
	}
	return defs_;
}

void World::rebuild() {
	surfaces_ = static_;
	for (const auto &[ch, g] : groups_) { // channels in sorted order
		surfaces_.insert(surfaces_.end(), g.surfaces.begin(), g.surfaces.end());
	}
	version_++;
}

World::Change World::diff(const std::vector<int> &before) const {
	Change c;
	for (const BuiltSurface &s : surfaces_) {
		if (std::find(before.begin(), before.end(), s.id) == before.end()) {
			c.added.push_back(s.id);
		}
	}
	for (int id : before) {
		if (by_id(id) == nullptr) {
			c.removed.push_back(id);
		}
	}
	return c;
}

World::Change World::land(const Landing &landing, std::vector<std::string> *errors) {
	std::vector<int> before;
	for (const BuiltSurface &s : surfaces_) {
		before.push_back(s.id);
	}
	generations_ = landing.generations;
	for (const Placed &p : landing.placed) {
		const std::vector<std::string> errs = validate_spec(p.spec);
		if (!errs.empty()) {
			if (errors) {
				for (const std::string &e : errs) {
					errors->push_back("spawn into " + p.channel + ": " + e);
				}
			}
			continue;
		}
		Group g;
		g.generation = p.generation;
		g.surfaces = build_spec(p.spec, next_id_, std::string("Spawned"));
		next_id_ += int(g.surfaces.size());
		groups_[p.channel] = std::move(g);
	}
	for (auto it = groups_.begin(); it != groups_.end();) {
		const rc::FlagValue *v = landing.flags.get("chan:" + it->first);
		const std::string *str = v ? std::get_if<std::string>(v) : nullptr;
		if (str == nullptr || *str != "g" + std::to_string(it->second.generation)) {
			it = groups_.erase(it);
		} else {
			++it;
		}
	}
	rebuild();
	return diff(before);
}

World::Snapshot World::snapshot() const { return Snapshot{groups_, generations_}; }

World::Change World::restore(const Snapshot &snap) {
	std::vector<int> before;
	for (const BuiltSurface &s : surfaces_) {
		before.push_back(s.id);
	}
	groups_ = snap.groups;
	generations_ = snap.generations;
	rebuild();
	return diff(before);
}

} // namespace rcg
