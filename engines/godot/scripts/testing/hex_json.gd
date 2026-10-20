class_name HexJson
extends RefCounted

# The engine-reference recorders' encoding (was SessionDigest.encode): canonical
# JSON -- sorted keys, no whitespace, every float as "f" + the hex of its
# float64 bytes (little-endian; a Vector2 component is its float32 widened),
# ints and floats kept apart. Godot never prints or parses one of these numbers.

static func encode(v) -> String:
	match typeof(v):
		TYPE_NIL:
			return "null"
		TYPE_BOOL:
			return "true" if v else "false"
		TYPE_INT:
			return str(v)
		TYPE_FLOAT:
			return "\"f" + PackedFloat64Array([v]).to_byte_array().hex_encode() + "\""
		TYPE_STRING, TYPE_STRING_NAME:
			return JSON.stringify(String(v))
		TYPE_VECTOR2:
			return "[%s,%s]" % [encode(float(v.x)), encode(float(v.y))]
		TYPE_ARRAY, TYPE_PACKED_VECTOR2_ARRAY, TYPE_PACKED_FLOAT64_ARRAY, TYPE_PACKED_FLOAT32_ARRAY, \
				TYPE_PACKED_INT32_ARRAY, TYPE_PACKED_INT64_ARRAY:
			var parts := PackedStringArray()
			for x in v:
				parts.append(encode(x))
			return "[" + ",".join(parts) + "]"
		TYPE_DICTIONARY:
			var keys: Array = v.keys()
			keys.sort_custom(func(a, b): return str(a) < str(b))
			var parts := PackedStringArray()
			for k in keys:
				parts.append(JSON.stringify(str(k)) + ":" + encode(v[k]))
			return "{" + ",".join(parts) + "}"
	assert(false, "HexJson.encode: unsupported %s" % type_string(typeof(v)))
	return "\"?%s\"" % type_string(typeof(v))
