class_name TerminalEffect
extends Effect

func kind() -> int:
	return Kind.TERMINAL

func get_display_name() -> String:
	return "block"

func get_display_color() -> Color:
	return Color.RED

func to_spec() -> Dictionary:
	return {"type": "terminal"}
