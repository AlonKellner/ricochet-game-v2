#include "rcg/world.hpp"

#include <algorithm>

namespace rcg {

World::World(std::vector<BuiltSurface> static_surfaces, const std::vector<Object> &objects) : static_(std::move(static_surfaces)) {
	for (const BuiltSurface &s : static_) {
		next_id_ = std::max(next_id_, s.id + 1);
	}
	for (const Object &o : objects) {
		Body b;
		b.object = o;
		b.id = next_id_++;
		b.pos = o.center;
		b.built = build_spec(object_spec(o, o.center), b.id, o.label).front();
		bodies_.push_back(std::move(b));
	}
	rebuild();
	initial_ = snapshot();
}

int World::body_of(int surface_id) const {
	for (size_t i = 0; i < bodies_.size(); i++) {
		if (bodies_[i].id == surface_id) {
			return int(i);
		}
	}
	return -1;
}

void World::set_body(size_t index, const V2 &pos, const V2 &vel, bool on_floor) {
	Body &b = bodies_.at(index);
	b.vel = vel;
	b.on_floor = on_floor;
	if (pos == b.pos) {
		return;
	}
	b.pos = pos;
	b.built = build_spec(object_spec(b.object, pos), b.id, b.object.label).front();
	rebuild();
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
	for (const Body &b : bodies_) {
		surfaces_.push_back(b.built);
	}
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

World::Snapshot World::snapshot() const {
	Snapshot s{groups_, generations_, {}};
	for (const Body &b : bodies_) {
		s.bodies.push_back({b.pos, b.vel, b.on_floor});
	}
	return s;
}

World::Change World::restore(const Snapshot &snap) {
	std::vector<int> before;
	for (const BuiltSurface &s : surfaces_) {
		before.push_back(s.id);
	}
	groups_ = snap.groups;
	generations_ = snap.generations;
	for (size_t i = 0; i < bodies_.size() && i < snap.bodies.size(); i++) {
		Body &b = bodies_[i];
		b.vel = snap.bodies[i].vel;
		b.on_floor = snap.bodies[i].on_floor;
		if (b.pos != snap.bodies[i].pos) {
			b.pos = snap.bodies[i].pos;
			b.built = build_spec(object_spec(b.object, b.pos), b.id, b.object.label).front();
		}
	}
	rebuild();
	return diff(before);
}

} // namespace rcg
