// Stage 116.4: the level's surfaces over time (GAME_SPEC 10.6; the port of
// World): the static surfaces (hosts included -- presence hides spent ones)
// and, per channel, the group the last landed spawn placed. A group stays
// while its channel's flag names its generation; a later spawn or a reset
// drops it; undo brings it back from a snapshot (the same surfaces, so ids
// and plans hold). Spawned surfaces take the next ids after the level's
// (the GDScript game's global counter, per world here).
#pragma once

#include "rcg/build.hpp"
#include "rcg/geom.hpp"
#include "rcg/landing.hpp"

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace rcg {

class World {
public:
	struct Group {
		int generation = 0;
		std::vector<BuiltSurface> surfaces;
	};
	struct Snapshot {
		std::map<std::string, Group> groups;
		std::map<std::string, int> generations;
	};
	// a structural change: the ids that joined and left
	struct Change {
		std::vector<int> added, removed;
	};

	explicit World(std::vector<BuiltSurface> static_surfaces);

	// the static surfaces, then the groups by channel name
	const std::vector<BuiltSurface> &surfaces() const { return surfaces_; }
	const BuiltSurface *by_id(int id) const;
	const std::map<std::string, int> &generations() const { return generations_; }
	const Group *group_of(const std::string &channel) const;
	// bumped by every structural change (the defs and pieces are cached per version)
	int version() const { return version_; }
	int next_id() const { return next_id_; }

	const Pieces &pieces() const;
	const std::vector<rc::SurfaceDef> &defs() const;

	// World.land: the placed groups join (built with the next ids), the groups no
	// longer current under the landed flags leave. A placed spec that does not
	// validate is skipped (the errors appended to `errors`).
	Change land(const Landing &landing, std::vector<std::string> *errors = nullptr);
	Snapshot snapshot() const;
	Change restore(const Snapshot &snap);
	const Snapshot &initial_snapshot() const { return initial_; }

private:
	void rebuild();
	Change diff(const std::vector<int> &before) const;

	std::vector<BuiltSurface> static_;
	std::map<std::string, Group> groups_;
	std::map<std::string, int> generations_;
	std::vector<BuiltSurface> surfaces_;
	Snapshot initial_;
	int next_id_ = 1;
	int version_ = 0;
	mutable int pieces_version_ = -1;
	mutable std::unique_ptr<Pieces> pieces_;
	mutable int defs_version_ = -1;
	mutable std::vector<rc::SurfaceDef> defs_;
};

} // namespace rcg
