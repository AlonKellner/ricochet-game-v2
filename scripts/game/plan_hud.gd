extends Control

var _label: Label

func _ready() -> void:
	_label = Label.new()
	_label.position = Vector2(10, 10)
	_label.add_theme_color_override("font_color", Color.WHITE)
	add_child(_label)

func update_plan(plan: PlanManager, surfaces: Array) -> void:
	if plan.is_empty():
		_label.text = ""
		return
	var lines: PackedStringArray = []
	lines.append("Plan:")
	for i in plan.entries.size():
		var entry: PlanManager.PlanEntry = plan.entries[i]
		var name := _surface_name(entry.surface_id, surfaces)
		var side_str := "L" if entry.side == Side.Value.LEFT else "R"
		lines.append("  %d. %s (%s)" % [i + 1, name, side_str])
	_label.text = "\n".join(lines)

func _surface_name(surface_id: int, surfaces: Array) -> String:
	for surf in surfaces:
		if surf.id == surface_id:
			var config: SideConfig = surf.active_side_config(Side.Value.LEFT, GameState.new())
			if config != null and config.effect != null:
				var dn: String = config.effect.get_display_name()
				if dn == "reflect":
					return "Mirror %d" % surface_id
				elif dn == "block":
					return "Wall %d" % surface_id
			return "Surface %d" % surface_id
	return "Surface %d" % surface_id
