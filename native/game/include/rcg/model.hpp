// Stage 116.3: the surface model's behaviour on a built surface (rcg/build.hpp):
// the side a surface shows under the game state (FixedResolver /
// CategoricalResolver), its presence (PresenceCond), the flag writes of a
// side (HitEffect: in shot and at landing), and the link its plan / hover
// chevrons draw (Surface.overlay_link). The game state is rc::Flags.
#pragma once

#include "rc/flags.hpp"
#include "rcg/build.hpp"

#include <optional>
#include <vector>

namespace rcg {

enum class Side { Left = 0, Right = 1 };
inline Side opposite(Side s) { return s == Side::Left ? Side::Right : Side::Left; }

// Effect.kind / is_self_inverse of a built effect ("pass": no effect)
enum class EffectKind { Pass, Terminal, Transformative, Projective };
EffectKind kind_of(const BuiltEffect &e);
bool self_inverse(const BuiltEffect &e);

// A level's flag value in the game state (an int is a GDScript number: a float there).
rc::FlagValue flag_value(const FlagValue &v);

// CategoricalResolver.entry / resolve: the row of the flag's value, else
// "DEFAULT", else the first row; FixedResolver: the fixed side. nullptr: none.
const BuiltSide *active_side(const BuiltSurface &s, Side side, const rc::Flags &flags);

// Surface.present: every presence condition holds (a missing key reads "DEFAULT").
bool present(const BuiltSurface &s, const rc::Flags &flags);

// SideConfig.in_shot_writes / landing writes: the hit effects' flag writes in
// order (the deferred ones at landing); host_id: the surface carrying the side.
std::vector<rc::StateChangeDef> writes(const BuiltSide &side, int host_id, bool deferred);
// SideConfig.writes_key (in shot or at landing)
bool writes_key(const BuiltSide &side, const std::string &key, int host_id);

// A side of a surface.
struct SideRef {
	int surface = 0;
	Side side = Side::Left;
	bool operator==(const SideRef &) const = default;
};

// Surface.overlay_link: for an interactive side under the flags, where the beam
// leaves -- the same side (reflection, inversion, a projective "back"), the
// opposite one (a pass, a toggler, a projective "through"), a portal's
// partner side, or nowhere (a block: no outgoing). nullopt: not interactive.
struct OverlayLink {
	SideRef from;
	std::optional<SideRef> outgoing;
};
std::optional<OverlayLink> overlay_link(const BuiltSurface &s, Side side, const rc::Flags &flags);

} // namespace rcg
