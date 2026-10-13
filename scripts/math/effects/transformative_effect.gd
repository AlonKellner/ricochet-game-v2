class_name TransformativeEffect
extends Effect

# A conformal or anti-conformal map of the plane (reflection, inversion, rigid
# motion). The beam solver applies it in closed form (BeamEffects).

var _carrier: GeneralizedCircle

func kind() -> int:
	return Kind.TRANSFORMATIVE
