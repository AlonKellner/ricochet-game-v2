class_name Effect
extends RefCounted

enum Kind { PASS, TERMINAL, TRANSFORMATIVE, PROJECTIVE }

func kind() -> int:
	return Kind.PASS

func is_terminal() -> bool:
	return kind() == Kind.TERMINAL

func is_transformative() -> bool:
	return kind() == Kind.TRANSFORMATIVE

# An involution (reflection, inversion): applying it twice is the identity, so
# a side with it links to itself (plan chevrons) and re-hitting it pops.
func is_self_inverse() -> bool:
	return false

func get_display_name() -> String:
	return "pass"

func get_display_color() -> Color:
	return Color.GRAY
