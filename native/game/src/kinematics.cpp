#include "rcg/kinematics.hpp"

#include "rcg/geom.hpp"
#include "rcg/model.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace rcg::kinematics {
namespace {

constexpr double EPS = 1e-9;

// Is a point of the arc's circle (at direction (ux, uy) from its center) on the arc?
bool on_arc(const Collider &c, double ux, double uy) {
	if (c.full) {
		return true;
	}
	const V2 q = V2::of(c.cx + ux * c.r, c.cy + uy * c.r);
	const double t = param(c.seg, q);
	return t >= 0.0 && t <= span(c.seg);
}

// The earliest t in [0, 1] at which |P + t d - Q| = rad while approaching (the
// distance decreasing); none when it never reaches it from outside.
bool circle_toi(double px, double py, double dx, double dy, double qx, double qy, double rad, double &t) {
	const double fx = px - qx, fy = py - qy;
	const double a = dx * dx + dy * dy;
	const double b = 2.0 * (fx * dx + fy * dy);
	const double c = fx * fx + fy * fy - rad * rad;
	if (a == 0.0 || b >= 0.0) {
		return false; // not moving, or moving away
	}
	if (c <= 0.0) {
		t = 0.0; // already within it, moving in
		return true;
	}
	const double disc = b * b - 4.0 * a * c;
	if (disc < 0.0) {
		return false;
	}
	const double s = (-b - std::sqrt(disc)) / (2.0 * a);
	if (s < 0.0 || s > 1.0) {
		return false;
	}
	t = s;
	return true;
}

// The earliest t at which |P + t d - C| = rad while moving outward from inside
// (an arc's inner side); none when it does not get there.
bool circle_exit_toi(double px, double py, double dx, double dy, double qx, double qy, double rad, double &t) {
	const double fx = px - qx, fy = py - qy;
	const double a = dx * dx + dy * dy;
	const double b = 2.0 * (fx * dx + fy * dy);
	const double c = fx * fx + fy * fy - rad * rad;
	if (a == 0.0 || b <= 0.0) {
		return false; // not moving outward
	}
	if (c >= 0.0) {
		t = 0.0; // already beyond it, moving out
		return true;
	}
	const double disc = b * b - 4.0 * a * c;
	const double s = (-b + std::sqrt(std::max(disc, 0.0))) / (2.0 * a);
	if (s < 0.0 || s > 1.0) {
		return false;
	}
	t = s;
	return true;
}

struct Hit {
	double t = 2.0;
	double nx = 0, ny = 0;
};

// a floor: a contact normal within 45 degrees of up (0, -1)
bool floor_like(double, double ny) { return -ny >= FLOOR_MIN_DOT - 1e-12; }

// Stage 172: a platform's contacts are its floor-like ones only (met from above)
void consider(Hit &best, double t, double nx, double ny, bool floor_only) {
	if (floor_only && !floor_like(nx, ny)) {
		return;
	}
	if (t < best.t) {
		best = Hit{t, nx, ny};
	}
}

// The first contact of the moving circle (P along d) with a collider.
void sweep(const Collider &c, double px, double py, double dx, double dy, Hit &best, double R0) {
	if (!c.arc) {
		const double ex = c.bx - c.ax, ey = c.by - c.ay;
		const double len = std::sqrt(ex * ex + ey * ey);
		if (len > 0.0) {
			// the two offset lines
			const double mx = -ey / len, my = ex / len;
			const double side = (px - c.ax) * mx + (py - c.ay) * my;
			const double sgn = side >= 0.0 ? 1.0 : -1.0;
			const double approach = (dx * mx + dy * my) * sgn; // < 0: toward the line
			if (approach < 0.0) {
				// the signed distance falls from |side| at rate -approach; within R0 already: now
				const double dist = std::abs(side);
				const double t = dist >= R0 ? (dist - R0) / -approach : 0.0;
				if (t <= 1.0) {
					const double qx = px + dx * t, qy = py + dy * t;
					const double u = ((qx - c.ax) * ex + (qy - c.ay) * ey) / (len * len);
					if (u >= 0.0 && u <= 1.0) {
						consider(best, t, mx * sgn, my * sgn, c.platform);
					}
				}
			}
		}
		for (const auto &[qx, qy] : {std::pair{c.ax, c.ay}, std::pair{c.bx, c.by}}) {
			double t;
			if (circle_toi(px, py, dx, dy, qx, qy, R0, t)) {
				const double cx = px + dx * t - qx, cy = py + dy * t - qy;
				const double n = std::sqrt(cx * cx + cy * cy);
				if (n > 0.0) {
					consider(best, t, cx / n, cy / n, c.platform);
				}
			}
		}
		return;
	}
	// an arc: its circle from outside (r + R0), from inside (r - R0), its end caps.
	// Inside the outer circle (or outside the inner one) is no contact unless in
	// that side's band of the arc itself (the player within the arc's circle is free).
	double t;
	const double dist_c = std::hypot(px - c.cx, py - c.cy);
	if (dist_c >= c.r && circle_toi(px, py, dx, dy, c.cx, c.cy, c.r + R0, t)) {
		const double qx = px + dx * t - c.cx, qy = py + dy * t - c.cy;
		const double n = std::sqrt(qx * qx + qy * qy);
		if (n > 0.0 && on_arc(c, qx / n, qy / n)) {
			consider(best, t, qx / n, qy / n, c.platform);
		}
	}
	if (c.r > R0 && dist_c <= c.r && circle_exit_toi(px, py, dx, dy, c.cx, c.cy, c.r - R0, t)) {
		const double qx = px + dx * t - c.cx, qy = py + dy * t - c.cy;
		const double n = std::sqrt(qx * qx + qy * qy);
		if (n > 0.0 && on_arc(c, qx / n, qy / n)) {
			consider(best, t, -qx / n, -qy / n, c.platform);
		}
	}
	if (!c.full) {
		for (const auto &[qx, qy] : {std::pair{c.ax, c.ay}, std::pair{c.bx, c.by}}) {
			if (circle_toi(px, py, dx, dy, qx, qy, R0, t)) {
				const double cx = px + dx * t - qx, cy = py + dy * t - qy;
				const double n = std::sqrt(cx * cx + cy * cy);
				if (n > 0.0) {
					consider(best, t, cx / n, cy / n, c.platform);
				}
			}
		}
	}
}

} // namespace

double distance(const Collider &c, double px, double py, double &nx, double &ny) {
	double qx, qy;
	if (!c.arc) {
		const double ex = c.bx - c.ax, ey = c.by - c.ay;
		const double l2 = ex * ex + ey * ey;
		const double u = l2 > 0.0 ? std::clamp(((px - c.ax) * ex + (py - c.ay) * ey) / l2, 0.0, 1.0) : 0.0;
		qx = c.ax + ex * u;
		qy = c.ay + ey * u;
	} else {
		const double fx = px - c.cx, fy = py - c.cy;
		const double n = std::sqrt(fx * fx + fy * fy);
		if (n > 0.0 && on_arc(c, fx / n, fy / n)) {
			qx = c.cx + fx / n * c.r;
			qy = c.cy + fy / n * c.r;
		} else if (c.full) {
			qx = c.cx + c.r;
			qy = c.cy;
		} else {
			const double da = std::hypot(px - c.ax, py - c.ay), db = std::hypot(px - c.bx, py - c.by);
			qx = da <= db ? c.ax : c.bx;
			qy = da <= db ? c.ay : c.by;
		}
	}
	const double vx = px - qx, vy = py - qy;
	const double d = std::sqrt(vx * vx + vy * vy);
	if (d > 0.0) {
		nx = vx / d;
		ny = vy / d;
	} else {
		nx = 0.0;
		ny = -1.0;
	}
	return d;
}

namespace {

// Stage 172: is the centre on a platform's upper side? A line: the side its
// up-facing normal points to, along the whole carrier (a line steeper than 45
// degrees has none); an arc: where the normal from its nearest point is floor-like.
bool above(const Collider &c, double px, double py) {
	if (!c.arc) {
		const double ex = c.bx - c.ax, ey = c.by - c.ay;
		const double len = std::sqrt(ex * ex + ey * ey);
		if (len == 0.0) {
			return false;
		}
		double ux = -ey / len, uy = ex / len;
		if (uy > 0.0) {
			ux = -ux;
			uy = -uy;
		}
		return floor_like(ux, uy) && (px - c.ax) * ux + (py - c.ay) * uy > 0.0;
	}
	double nx, ny;
	return distance(c, px, py, nx, ny) > 0.0 && floor_like(nx, ny);
}

// Out of one collider's overlap (a circle `drop` below (px, py)); a platform only
// up onto its top. True: moved.
bool push_out(const Collider &c, double &px, double &py, double drop, double R0, bool &on_floor) {
	double nx, ny;
	const double d = distance(c, px, py + drop, nx, ny);
	if (d >= R0 - EPS || (c.platform && !floor_like(nx, ny))) {
		return false;
	}
	px += nx * (R0 - d);
	py += ny * (R0 - d);
	on_floor = on_floor || floor_like(nx, ny);
	return true;
}

} // namespace

Collider collider_of(const rc::arc::SegGeom &seg) {
	Collider c;
	c.ax = seg.s.x;
	c.ay = seg.s.y;
	c.bx = seg.e.x;
	c.by = seg.e.y;
	const rc::arc::GeneralizedCircle &g = seg.get_carrier();
	if (!g.is_line()) {
		c.arc = true;
		c.cx = g.center_x();
		c.cy = g.center_y();
		c.r = g.radius();
		c.full = seg.full;
		c.seg = seg;
	}
	return c;
}

Collider segment_collider(const V2 &a, const V2 &b) {
	Collider c;
	c.ax = a.x;
	c.ay = a.y;
	c.bx = b.x;
	c.by = b.y;
	return c;
}

double distance(const Collider &c, const Shape &shape, double px, double py, double &nx, double &ny) {
	double best = std::numeric_limits<double>::infinity();
	for (int k = 0; k < shape.n; k++) {
		double cx, cy;
		const double d = distance(c, px, py + shape.drop[k], cx, cy);
		if (d < best) {
			best = d;
			nx = cx;
			ny = cy;
		}
	}
	return best;
}

std::vector<Collider> colliders_of(const std::vector<BuiltSurface> &world, const rc::Flags &flags, const Level &level,
		const std::vector<int> &except) {
	std::vector<Collider> out;
	for (const BuiltSurface &s : world) {
		if ((s.player_solid || s.platform) && !s.pre_transform && present(s, flags) &&
				std::find(except.begin(), except.end(), s.id) == except.end()) {
			out.push_back(collider_of(s.seg));
			out.back().platform = s.platform;
		}
	}
	for (const auto &k : level.colliders) {
		out.push_back(segment_collider(k.a, k.b));
	}
	return out;
}

void step(Body &body, const Input &in, const V2 &gravity, const std::vector<Collider> &colliders) { step_body(body, in, gravity, colliders, PLAYER); }

void step_body(Body &body, const Input &in, const V2 &gravity, const std::vector<Collider> &colliders, double radius) {
	step_body(body, in, gravity, colliders, Shape{radius});
}

void step_body(Body &body, const Input &in, const V2 &gravity, const std::vector<Collider> &colliders, const Shape &shape) {
	const double R0 = shape.radius + SKIN;
	// the velocity (player.gd), float32 as the engine keeps it
	V2 v = body.vel;
	const bool has_gravity = gravity.length_squared() > 0.0f;
	if (has_gravity) {
		if (!body.on_floor) {
			v = v + gravity * float(STEP);
		} else {
			v.y = 0.0f;
		}
		v.x = in.axis_x * float(SPEED);
		if (body.on_floor && in.jump) {
			v.y = -float(JUMP_VELOCITY);
		}
	} else {
		V2 dir{in.axis_x, in.axis_y};
		if (dir.length_squared() > 0.0f) {
			dir = dir.normalized();
		}
		v = dir * float(SPEED);
	}
	// Stage 172: a platform takes part only while the centre (Stage 173: the feet's)
	// is above it at the step's start and the body is not dropping (down held)
	const bool drop = in.axis_y > 0.0f;
	std::vector<char> off(colliders.size(), 0);
	for (size_t i = 0; i < colliders.size(); i++) {
		off[i] = colliders[i].platform && (drop || !above(colliders[i], body.pos.x, body.pos.y + shape.feet()));
	}
	// the move: swept, sliding along what it meets
	double px = body.pos.x, py = body.pos.y;
	double vx = v.x, vy = v.y;
	double mx = vx * STEP, my = vy * STEP;
	bool on_floor = false;
	for (int slide = 0; slide < MAX_SLIDES; slide++) {
		if (mx * mx + my * my < 1e-18) {
			break;
		}
		Hit best;
		for (size_t i = 0; i < colliders.size(); i++) {
			for (int k = 0; k < shape.n && !off[i]; k++) {
				sweep(colliders[i], px, py + shape.drop[k], mx, my, best, R0);
			}
		}
		if (best.t > 1.0) {
			px += mx;
			py += my;
			break;
		}
		px += mx * best.t;
		py += my * best.t;
		on_floor = on_floor || floor_like(best.nx, best.ny);
		double rx = mx * (1.0 - best.t), ry = my * (1.0 - best.t);
		const double into = rx * best.nx + ry * best.ny;
		rx -= best.nx * into;
		ry -= best.ny * into;
		const double vin = vx * best.nx + vy * best.ny;
		if (vin < 0.0) {
			vx -= best.nx * vin;
			vy -= best.ny * vin;
		}
		mx = rx;
		my = ry;
	}
	// out of any overlap; a platform not while rising (the jump carries on, then lands on top)
	const bool rising = vy < 0.0;
	for (int it = 0; it < 4; it++) {
		bool moved = false;
		for (size_t i = 0; i < colliders.size(); i++) {
			for (int k = 0; k < shape.n && !off[i] && !(rising && colliders[i].platform); k++) {
				moved = push_out(colliders[i], px, py, shape.drop[k], R0, on_floor) || moved;
			}
		}
		if (!moved) {
			break;
		}
	}
	// the floor: touching one (within the skin), or -- having stood on one and
	// not rising -- within the snap below (stay on it)
	if (has_gravity && vy >= 0.0 && !on_floor) {
		double best_d = std::numeric_limits<double>::infinity(), bnx = 0, bny = 0;
		for (size_t i = 0; i < colliders.size(); i++) {
			if (off[i]) {
				continue;
			}
			double nx, ny;
			const double d = distance(colliders[i], shape, px, py, nx, ny);
			if (d <= R0 + FLOOR_SNAP && floor_like(nx, ny) && d < best_d) {
				best_d = d;
				bnx = nx;
				bny = ny;
			}
		}
		if (best_d <= R0 + 1e-6) {
			on_floor = true;
		} else if (best_d < std::numeric_limits<double>::infinity() && body.on_floor) {
			px -= bnx * (best_d - R0);
			py -= bny * (best_d - R0);
			on_floor = true;
		}
	}
	body.pos = V2::of(px, py);
	body.vel = V2::of(vx, vy);
	body.on_floor = on_floor;
}

void settle(Body &body, const std::vector<Collider> &colliders, const Shape &shape) {
	const double R0 = shape.radius + SKIN;
	double px = body.pos.x, py = body.pos.y;
	bool on_floor = false;
	for (int it = 0; it < 4; it++) {
		bool moved = false;
		for (const Collider &c : colliders) {
			if (c.platform && above(c, body.pos.x, body.pos.y + shape.feet())) {
				moved = push_out(c, px, py, shape.feet(), R0, on_floor) || moved;
			}
		}
		if (!moved) {
			break;
		}
	}
	body.pos = V2::of(px, py);
	body.on_floor = body.on_floor || on_floor;
}

double sweep_fraction(double px, double py, double dx, double dy, const std::vector<Collider> &colliders, const Shape &shape) {
	Hit best;
	for (const Collider &c : colliders) {
		for (int k = 0; k < shape.n; k++) {
			sweep(c, px, py + shape.drop[k], dx, dy, best, shape.radius + SKIN);
		}
	}
	return std::min(best.t, 1.0);
}

Collider circle_collider(const V2 &center, double r) {
	Collider c;
	c.arc = true;
	c.full = true;
	c.cx = center.x;
	c.cy = center.y;
	c.r = r;
	c.ax = c.bx = double(center.x) + r;
	c.ay = c.by = center.y;
	c.seg = rc::arc::SegGeom::full_from_carrier(rc::arc::from_circle(center, r));
	return c;
}

} // namespace rcg::kinematics
