class_name ChevronOverlay
extends RefCounted

var side: int = -1
var has_incoming: bool = true
var has_outgoing: bool = true
var outgoing_side: int = -1
var incoming_color: Color = Color.WHITE
var outgoing_color: Color = Color.WHITE
var gradient_color: Color = Color.WHITE
# Stage 108: the piece of its surface it belongs to (-1: every piece)
var piece: int = -1

# Whether an overlay (or plan entry) of piece `of` is drawn on a node drawing
# piece `node` (-1 on either side: the whole surface).
static func for_piece(of: int, node: int) -> bool:
	return of < 0 or node < 0 or of == node

static func hover(p_side: int, p_has_outgoing: bool, p_outgoing_side: int,
		hover_color: Color, p_has_incoming: bool = true) -> ChevronOverlay:
	var o := ChevronOverlay.new()
	o.side = p_side
	o.has_incoming = p_has_incoming
	o.has_outgoing = p_has_outgoing
	o.outgoing_side = p_outgoing_side
	o.incoming_color = hover_color
	o.outgoing_color = hover_color
	o.gradient_color = hover_color
	return o

static func plan(p_side: int, p_has_outgoing: bool, p_outgoing_side: int,
		p_incoming_color: Color, p_outgoing_color: Color,
		p_gradient_color: Color, p_has_incoming: bool = true) -> ChevronOverlay:
	var o := ChevronOverlay.new()
	o.side = p_side
	o.has_incoming = p_has_incoming
	o.has_outgoing = p_has_outgoing
	o.outgoing_side = p_outgoing_side
	o.incoming_color = p_incoming_color
	o.outgoing_color = p_outgoing_color
	o.gradient_color = p_gradient_color
	return o

# Value equality: the same chevrons in the same colours.
func equals(o: ChevronOverlay) -> bool:
	return o != null and side == o.side and has_incoming == o.has_incoming \
		and has_outgoing == o.has_outgoing and outgoing_side == o.outgoing_side \
		and incoming_color == o.incoming_color and outgoing_color == o.outgoing_color \
		and gradient_color == o.gradient_color and piece == o.piece

# Two overlay lists with equal overlays in the same order.
static func same_list(a: Array, b: Array) -> bool:
	if a.size() != b.size():
		return false
	for i in a.size():
		if not a[i].equals(b[i]):
			return false
	return true
