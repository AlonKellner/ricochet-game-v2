#include "rcg/render.hpp"

#include "rcg/digest.hpp"

#include <algorithm>
#include <cmath>

namespace rcg::render {
namespace {

using json = nlohmann::json;

json color_json(const Color &c) { return json::array({double(c[0]), double(c[1]), double(c[2]), double(c[3])}); }
json pts_json(const std::vector<V2> &pts) {
	json out = json::array();
	for (const V2 &p : pts) {
		out.push_back(digest::vec(p));
	}
	return out;
}

} // namespace

uint64_t NodeKey::id() const {
	return (uint64_t(kind) << 56) | (uint64_t(uint32_t(a)) << 20) | uint64_t(uint32_t(b + 1) & 0xFFFFF);
}

json NodeKey::json() const {
	static const char *const NAMES[] = {"background", "player", "cursor", "path", "arrow", "hud", "surface", "edge", "visibility", "spawn", "editor",
		"goal", "detector"};
	nlohmann::json k = nlohmann::json::array({NAMES[size_t(kind)]});
	if (kind == NodeKind::Surface || kind == NodeKind::Editor) {
		k.push_back(a);
		k.push_back(b);
	} else if (kind == NodeKind::Detector) {
		k.push_back(a);
	}
	return k;
}

uint64_t content_hash(const std::vector<Call> &calls) {
	uint64_t h = 0xcbf29ce484222325ull;
	auto mix = [&](const void *p, size_t n) {
		const unsigned char *c = static_cast<const unsigned char *>(p);
		for (size_t i = 0; i < n; i++) {
			h = (h ^ c[i]) * 0x100000001b3ull;
		}
	};
	for (const Call &c : calls) {
		mix(&c.kind, sizeof c.kind);
		mix(c.pts.data(), c.pts.size() * sizeof(V2));
		mix(c.colors.data(), c.colors.size() * sizeof(Color));
		mix(c.indices.data(), c.indices.size() * sizeof(int));
		for (const double d : {c.radius, c.start, c.end, c.width, c.dash, c.rotation}) {
			mix(&d, sizeof d);
		}
		for (const int v : {c.point_count, c.alignment, c.font_size, int(c.pts.size()), int(c.text.size())}) {
			mix(&v, sizeof v);
		}
		mix(c.text.data(), c.text.size());
	}
	return h;
}

Color hex(std::uint32_t p) {
	const float a = float(p & 0xFF) / 255.0f;
	p >>= 8;
	const float b = float(p & 0xFF) / 255.0f;
	p >>= 8;
	const float g = float(p & 0xFF) / 255.0f;
	p >>= 8;
	const float r = float(p & 0xFF) / 255.0f;
	return {r, g, b, a};
}

Color with_alpha(const Color &c, double a) { return {c[0], c[1], c[2], float(a)}; }

json call_json(const Call &c) {
	const json col = c.colors.empty() ? json(nullptr) : color_json(c.colors.front());
	switch (c.kind) {
		case Kind::Line:
			return json::array({"line", digest::vec(c.pts[0]), digest::vec(c.pts[1]), col, c.width});
		case Kind::Arc:
			return json::array({"arc", digest::vec(c.pts[0]), c.radius, c.start, c.end, c.point_count, col, c.width});
		case Kind::Polygon: {
			json cols = json::array();
			for (const Color &k : c.colors) {
				cols.push_back(color_json(k));
			}
			return json::array({"polygon", pts_json(c.pts), cols});
		}
		case Kind::ColoredPolygon:
			return json::array({"colored_polygon", pts_json(c.pts), col});
		case Kind::Circle:
			return json::array({"circle", digest::vec(c.pts[0]), c.radius, col});
		case Kind::Rect:
			return json::array({"rect", digest::vec(c.pts[0]), digest::vec(c.pts[1]), col});
		case Kind::Polyline:
			return json::array({"polyline", pts_json(c.pts), col, c.width});
		case Kind::Multiline:
			return json::array({"multiline", pts_json(c.pts), col, c.width});
		case Kind::DashedLine:
			return json::array({"dashed_line", digest::vec(c.pts[0]), digest::vec(c.pts[1]), col, c.width, c.dash});
		case Kind::Text:
			return json::array({"text", digest::vec(c.pts[0]), c.text, c.alignment, c.font_size, col});
		case Kind::Triangles:
			return json::array({"triangles", pts_json(c.pts), c.indices, col});
		case Kind::Polygon2D:
			return json::array({"polygon2d", pts_json(c.pts), col, c.rotation});
		case Kind::Label:
			return json::array({"label", c.text});
	}
	return nullptr;
}

void sort(std::vector<Node> &nodes) {
	std::stable_sort(nodes.begin(), nodes.end(), [](const Node &a, const Node &b) { return a.z < b.z; });
}

std::vector<V2> tessellate_arc(const V2 &center, double radius, double start, double end, int point_count) {
	std::vector<V2> pts;
	const float tau = 6.28318530717958647692f;
	const float delta = std::clamp(float(end) - float(start), -tau, tau);
	for (int i = 0; i < point_count; i++) {
		const float theta = (float(i) / (float(point_count) - 1.0f)) * delta + float(start);
		pts.push_back(center + V2{std::cos(theta), std::sin(theta)} * float(radius));
	}
	return pts;
}

json list_json(const std::vector<Node> &nodes) {
	// the digest's order: by z, then the key's encoding (independent of the tree order)
	std::vector<std::pair<std::string, const Node *>> sorted;
	for (const Node &n : nodes) {
		sorted.push_back({digest::encode(n.key), &n});
	}
	std::stable_sort(sorted.begin(), sorted.end(), [](const auto &a, const auto &b) {
		return a.second->z != b.second->z ? a.second->z < b.second->z : a.first < b.first;
	});
	json out = json::array();
	for (const auto &[key, n] : sorted) {
		json calls = json::array();
		for (const Call &c : n->calls) {
			calls.push_back(call_json(c));
		}
		out.push_back(json::array({n->key, n->z, digest::vec(n->position), color_json(n->modulate), calls}));
	}
	return out;
}

} // namespace rcg::render
