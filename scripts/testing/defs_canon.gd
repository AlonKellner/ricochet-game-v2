class_name DefsCanon
extends RefCounted

# A canonical, bit-exact spelling of scene defs (Stage 107 parity): sorted
# dictionary keys, floats as their IEEE-754 bits (hex), ints and floats kept
# apart. Two defs arrays describe the same scene iff their canons are equal.

static func canon(v) -> String:
	match typeof(v):
		TYPE_NIL:
			return "null"
		TYPE_BOOL:
			return "true" if v else "false"
		TYPE_INT:
			return "i%d" % v
		TYPE_FLOAT:
			return "f" + PackedFloat64Array([v]).to_byte_array().hex_encode()
		TYPE_STRING, TYPE_STRING_NAME:
			return JSON.stringify(String(v))
		TYPE_VECTOR2:
			return "v(%s,%s)" % [canon(float(v.x)), canon(float(v.y))]
		TYPE_ARRAY:
			var parts: Array[String] = []
			for x in v:
				parts.append(canon(x))
			return "[" + ",".join(parts) + "]"
		TYPE_DICTIONARY:
			var keys: Array = v.keys()
			keys.sort_custom(func(a, b): return str(a) < str(b))
			var parts: Array[String] = []
			for k in keys:
				parts.append("%s:%s" % [JSON.stringify(str(k)), canon(v[k])])
			return "{" + ",".join(parts) + "}"
	return "?" + type_string(typeof(v)) + ":" + str(v)
