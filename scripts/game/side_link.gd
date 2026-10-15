class_name SideLink
extends RefCounted

# Links and surfaces would form reference cycles (a self-link, a pair, link
# <-> surface) and leak every level (Stage 103): a link refers to its surface
# weakly (surfaces are owned by the level), and to its outgoing link strongly
# unless that would close a cycle (itself, or a partner that already holds
# this link strongly), then weakly. Whoever holds a pair's first link keeps
# the pair alive.
var _surface_ref: WeakRef = null
var surface: Surface:
	get:
		return _surface_ref.get_ref() if _surface_ref != null else null
	set(v):
		_surface_ref = weakref(v) if v != null else null
var side: Side.Value
var _out_strong: SideLink = null
var _out_weak: WeakRef = null
var outgoing: SideLink:
	get:
		if _out_strong != null:
			return _out_strong
		return _out_weak.get_ref() if _out_weak != null else null
	set(v):
		_out_strong = null
		_out_weak = null
		if v == null:
			return
		if v == self or v._out_strong == self:
			_out_weak = weakref(v)
		else:
			_out_strong = v

static func from_self(surf: Surface, p_side: Side.Value) -> SideLink:
	var link := SideLink.new()
	link.surface = surf
	link.side = p_side
	link.outgoing = link
	return link

static func from_pair(surf_a: Surface, side_a: Side.Value,
		surf_b: Surface, side_b: Side.Value) -> SideLink:
	var a := SideLink.new()
	var b := SideLink.new()
	a.surface = surf_a
	a.side = side_a
	a.outgoing = b
	b.surface = surf_b
	b.side = side_b
	b.outgoing = a
	return a
