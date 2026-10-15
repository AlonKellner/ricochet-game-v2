#include "rc/exact.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace rc {
namespace exact {

// ---------------------------------------------------------------- running error pairs

EB emul(EB x, EB y) {
	const double v = x.v * y.v;
	return EB{v, std::fabs(x.v) * y.e + std::fabs(y.v) * x.e + x.e * y.e + U * std::fabs(v)};
}

EB eadd(EB x, EB y) {
	const double v = x.v + y.v;
	return EB{v, x.e + y.e + U * std::fabs(v)};
}

EB esub(EB x, EB y) {
	const double v = x.v - y.v;
	return EB{v, x.e + y.e + U * std::fabs(v)};
}

// ---------------------------------------------------------------- exact ops

BigFloat of(double x) {
	return BigFloat::from_float(x, 2); // 53 bits fit in 2 limbs (56 bits)
}

BigFloat mul(const BigFloat &a, const BigFloat &b) {
	if (a.is_zero() || b.is_zero()) {
		return BigFloat::zero(2);
	}
	return a.with_limbs(int(a.limbs.size() + b.limbs.size())).mul(b);
}

BigFloat add(const BigFloat &a, const BigFloat &b) {
	if (a.is_zero()) {
		return b;
	}
	if (b.is_zero()) {
		return a;
	}
	const int64_t gap = std::llabs(a.top_exp() - b.top_exp());
	const int n = int(std::max(a.limbs.size(), b.limbs.size())) + int(gap / BigFloat::BITS) + 3;
	return a.with_limbs(n).add(b.with_limbs(n));
}

BigFloat sub(const BigFloat &a, const BigFloat &b) {
	return add(a, b.neg());
}

// ---------------------------------------------------------------- predicates

bool orient_zero(double ax, double ay, double bx, double by, double cx, double cy) {
	const double l = (bx - ax) * (cy - ay);
	const double r = (by - ay) * (cx - ax);
	const double det = l - r;
	if (std::fabs(det) > 4.0 * U * (std::fabs(l) + std::fabs(r))) {
		return false;
	}
	const BigFloat e = sub(mul(sub(of(bx), of(ax)), sub(of(cy), of(ay))), mul(sub(of(by), of(ay)), sub(of(cx), of(ax))));
	return e.is_zero();
}

bool on_circle_zero(double cx, double cy, double r, double kx, double ky) {
	const double dx = kx - cx;
	const double dy = ky - cy;
	const double a = dx * dx + dy * dy;
	const double rr = r * r;
	if (std::fabs(a - rr) > 8.0 * U * (a + rr)) {
		return false;
	}
	const BigFloat ex = sub(of(kx), of(cx));
	const BigFloat ey = sub(of(ky), of(cy));
	const BigFloat e = sub(add(mul(ex, ex), mul(ey, ey)), mul(of(r), of(r)));
	return e.is_zero();
}

bool incircle_zero(double ax, double ay, double bx, double by, double cx, double cy, double dx, double dy) {
	const double adx = ax - dx;
	const double ady = ay - dy;
	const double bdx = bx - dx;
	const double bdy = by - dy;
	const double cdx = cx - dx;
	const double cdy = cy - dy;
	const double alift = adx * adx + ady * ady;
	const double blift = bdx * bdx + bdy * bdy;
	const double clift = cdx * cdx + cdy * cdy;
	const double bc = bdx * cdy - cdx * bdy;
	const double ca = cdx * ady - adx * cdy;
	const double ab = adx * bdy - bdx * ady;
	const double det = alift * bc + blift * ca + clift * ab;
	const double perm = (std::fabs(bdx * cdy) + std::fabs(cdx * bdy)) * alift + (std::fabs(cdx * ady) + std::fabs(adx * cdy)) * blift
		+ (std::fabs(adx * bdy) + std::fabs(bdx * ady)) * clift;
	if (std::fabs(det) > 16.0 * U * perm) {
		return false;
	}
	const BigFloat eax = sub(of(ax), of(dx));
	const BigFloat eay = sub(of(ay), of(dy));
	const BigFloat ebx = sub(of(bx), of(dx));
	const BigFloat eby = sub(of(by), of(dy));
	const BigFloat ecx = sub(of(cx), of(dx));
	const BigFloat ecy = sub(of(cy), of(dy));
	const BigFloat ea = add(mul(eax, eax), mul(eay, eay));
	const BigFloat eb = add(mul(ebx, ebx), mul(eby, eby));
	const BigFloat ec = add(mul(ecx, ecx), mul(ecy, ecy));
	const BigFloat ebc = sub(mul(ebx, ecy), mul(ecx, eby));
	const BigFloat eca = sub(mul(ecx, eay), mul(eax, ecy));
	const BigFloat eab = sub(mul(eax, eby), mul(ebx, eay));
	return add(add(mul(ea, ebc), mul(eb, eca)), mul(ec, eab)).is_zero();
}

// ---------------------------------------------------------------- exact lines

Line3 line_from_dir(double px, double py, double dx, double dy) {
	const BigFloat a = of(dy).neg();
	const BigFloat b = of(dx);
	return {a, b, add(mul(a, of(px)), mul(b, of(py)))};
}

Line3 line_through(double px, double py, double qx, double qy) {
	const BigFloat a = sub(of(qy), of(py)).neg();
	const BigFloat b = sub(of(qx), of(px));
	return {a, b, add(mul(a, of(px)), mul(b, of(py)))};
}

Point3 reflect_point_h(const Point3 &X, double sx, double sy, double ex, double ey) {
	const BigFloat mx = sub(of(ex), of(sx));
	const BigFloat my = sub(of(ey), of(sy));
	const BigFloat mm = add(mul(mx, mx), mul(my, my));
	const BigFloat m2r = sub(mul(mx, mx), mul(my, my));
	const BigFloat m2i = add(mul(mx, my), mul(my, mx));
	const BigFloat &w = X[2];
	// conj(X - s w)
	const BigFloat cx = sub(X[0], mul(of(sx), w));
	const BigFloat cy = sub(mul(of(sy), w), X[1]);
	const BigFloat swm = mul(w, mm);
	const BigFloat rx = add(mul(of(sx), swm), sub(mul(m2r, cx), mul(m2i, cy)));
	const BigFloat ry = add(mul(of(sy), swm), add(mul(m2r, cy), mul(m2i, cx)));
	return {rx, ry, swm};
}

std::optional<Point3> invert_point_h(const Point3 &X, const Point3 &C, const BigFloat &rw2) {
	const BigFloat &w = X[2];
	const BigFloat &cw = C[2];
	const BigFloat dx = sub(mul(X[0], cw), mul(C[0], w));
	const BigFloat dy = sub(mul(X[1], cw), mul(C[1], w));
	const BigFloat dd = add(mul(dx, dx), mul(dy, dy));
	if (dd.is_zero()) {
		return std::nullopt;
	}
	const BigFloat rw = mul(rw2, w);
	Point3 out = {add(mul(C[0], dd), mul(rw, dx)), add(mul(C[1], dd), mul(rw, dy)), mul(cw, dd)};
	if (out[2].is_negative()) {
		out = {out[0].neg(), out[1].neg(), out[2].neg()};
	}
	return out;
}

// ---------------------------------------------------------------- the exact pole

namespace {

// complex product / conjugate on exact pairs
struct C2 {
	BigFloat re, im;
};
C2 cmul(const C2 &a, const C2 &b) {
	return {sub(mul(a.re, b.re), mul(a.im, b.im)), add(mul(a.re, b.im), mul(a.im, b.re))};
}
C2 conj(const C2 &a) {
	return {a.re, a.im.neg()};
}
// The same value without its zero low limbs (exact multiplication allocates
// the sum of both factors' limbs; small integers carry many zero limbs), so
// the record grows with its significant bits only.
BigFloat trim(BigFloat b) {
	size_t z = 0;
	while (z + 2 < b.limbs.size() && b.limbs[z] == 0) {
		z++;
	}
	if (z > 0 && !b.is_zero()) {
		b.limbs.erase(b.limbs.begin(), b.limbs.begin() + long(z));
		b.exp += int64_t(BigFloat::BITS) * int64_t(z);
	}
	return b;
}
std::optional<ExactPole> pole_ok(const Point3 &Q, const C2 &D) {
	if (D.re.is_zero() && D.im.is_zero()) {
		return std::nullopt;
	}
	return ExactPole{{trim(Q[0]), trim(Q[1]), trim(Q[2])}, trim(D.re), trim(D.im)};
}
// Q - C up to the positive factor Qw Cw
C2 rel(const Point3 &Q, const Point3 &C) {
	return {sub(mul(Q[0], C[2]), mul(C[0], Q[2])), sub(mul(Q[1], C[2]), mul(C[1], Q[2]))};
}
// a float point minus a homogeneous point, up to the positive factor w
C2 rel_f(double px, double py, const Point3 &X) {
	return {sub(mul(of(px), X[2]), X[0]), sub(mul(of(py), X[2]), X[1])};
}

} // namespace

std::optional<ExactPole> pole_reflect(const ExactPole &p, double sx, double sy, double ex, double ey) {
	const C2 M{sub(of(ex), of(sx)), sub(of(ey), of(sy))};
	return pole_ok(reflect_point_h(p.Q, sx, sy, ex, ey), cmul(cmul(M, M), conj(C2{p.dx, p.dy})));
}

std::optional<ExactPole> pole_invert(const ExactPole &p, const Point3 &C, const BigFloat &rw2) {
	const std::optional<Point3> Q = invert_point_h(p.Q, C, rw2);
	if (!Q) {
		return std::nullopt; // Q was the center: it goes to infinity
	}
	const C2 Uq = rel(p.Q, C);
	// the inversion's differential: t -> -conj(t) (Q - C)^2 / |Q - C|^4
	return pole_ok(*Q, cmul(conj(C2{p.dx, p.dy}), cmul(Uq, Uq)));
}

ExactPole pole_translate(const ExactPole &p, double dx, double dy) {
	return ExactPole{{trim(add(p.Q[0], mul(of(dx), p.Q[2]))), trim(add(p.Q[1], mul(of(dy), p.Q[2]))), p.Q[2]}, p.dx, p.dy};
}

std::optional<ExactPole> pole_from_line(const Line3 &L, const Point3 &C) {
	return pole_ok(C, C2{L[1], L[0].neg()});
}

std::optional<ExactPole> pole_start_direction(double px, double py, double dx, double dy, const Point3 &Q0) {
	// invert about Q0: the circle becomes a line parallel to its tangent at Q0,
	// in the direction the differential at P gives d: conj(d) (P - Q0)^2
	const C2 W = rel_f(px, py, Q0);
	return pole_ok(Q0, cmul(conj(C2{of(dx), of(dy)}), cmul(W, W)));
}

std::optional<ExactPole> pole_start_aimed(double px, double py, const Point3 &X, const Point3 &Q0) {
	// conj(P - X) (X - Q0) (P - Q0)
	const C2 PX = rel_f(px, py, X);
	const C2 XQ = rel(X, Q0);
	const C2 PQ = rel_f(px, py, Q0);
	return pole_ok(Q0, cmul(conj(PX), cmul(XQ, PQ)));
}

bool pole_on_line(const ExactPole &p, double sx, double sy, double ex, double ey) {
	return on_line_h(line_through(sx, sy, ex, ey), p.Q);
}

bool pole_on_circle(const ExactPole &p, const Point3 &C, const BigFloat &rw2) {
	const C2 Uq = rel(p.Q, C);
	// |Q - C|^2 = r^2: |Uq|^2 = rw2 Qw^2 (rw2 = r^2 Cw^2)
	return sub(add(mul(Uq.re, Uq.re), mul(Uq.im, Uq.im)), mul(rw2, mul(p.Q[2], p.Q[2]))).is_zero();
}

bool pole_tangent_line(const ExactPole &p, double sx, double sy, double ex, double ey) {
	if (!pole_on_line(p, sx, sy, ex, ey)) {
		return false;
	}
	const BigFloat mx = sub(of(ex), of(sx));
	const BigFloat my = sub(of(ey), of(sy));
	return sub(mul(p.dx, my), mul(p.dy, mx)).is_zero();
}

bool pole_tangent_circle(const ExactPole &p, const Point3 &C, const BigFloat &rw2) {
	if (!pole_on_circle(p, C, rw2)) {
		return false;
	}
	const C2 Uq = rel(p.Q, C);
	return add(mul(p.dx, Uq.re), mul(p.dy, Uq.im)).is_zero();
}

int pole_limbs(const ExactPole &p) {
	int n = 0;
	for (const BigFloat *b : {&p.Q[0], &p.Q[1], &p.Q[2], &p.dx, &p.dy}) {
		n = std::max(n, int(b->limbs.size()));
	}
	return n;
}

Line3 line_through_h(double px, double py, const Point3 &X) {
	const BigFloat &w = X[2];
	const BigFloat a = sub(X[1], mul(of(py), w)).neg();
	const BigFloat b = sub(X[0], mul(of(px), w));
	return {a, b, add(mul(a, of(px)), mul(b, of(py)))};
}

Point3 circumcenter_h(double ax, double ay, double bx, double by, double cx, double cy) {
	const BigFloat eax = of(ax);
	const BigFloat eay = of(ay);
	const BigFloat ebx = of(bx);
	const BigFloat eby = of(by);
	const BigFloat ecx = of(cx);
	const BigFloat ecy = of(cy);
	const BigFloat la = add(mul(eax, eax), mul(eay, eay));
	const BigFloat lb = add(mul(ebx, ebx), mul(eby, eby));
	const BigFloat lc = add(mul(ecx, ecx), mul(ecy, ecy));
	const BigFloat byc = sub(eby, ecy);
	const BigFloat cya = sub(ecy, eay);
	const BigFloat ayb = sub(eay, eby);
	const BigFloat w = mul(of(2.0), add(add(mul(eax, byc), mul(ebx, cya)), mul(ecx, ayb)));
	const BigFloat nx = add(add(mul(la, byc), mul(lb, cya)), mul(lc, ayb));
	const BigFloat ny = add(add(mul(la, sub(ecx, ebx)), mul(lb, sub(eax, ecx))), mul(lc, sub(ebx, eax)));
	return {nx, ny, w};
}

bool on_line_h(const Line3 &L, const Point3 &X) {
	return sub(add(mul(L[0], X[0]), mul(L[1], X[1])), mul(L[2], X[2])).is_zero();
}

bool tangent_zero(const Line3 &L, const Point3 &C, const BigFloat &rw2) {
	const BigFloat d = sub(add(mul(L[0], C[0]), mul(L[1], C[1])), mul(L[2], C[2]));
	const BigFloat n2 = add(mul(L[0], L[0]), mul(L[1], L[1]));
	return sub(mul(d, d), mul(rw2, n2)).is_zero();
}

Line3 reflect_line(const Line3 &L, double sx, double sy, double ex, double ey) {
	const BigFloat mx = sub(of(ex), of(sx));
	const BigFloat my = sub(of(ey), of(sy));
	const BigFloat mm = add(mul(mx, mx), mul(my, my));
	const BigFloat m2r = sub(mul(mx, mx), mul(my, my));
	const BigFloat m2i = add(mul(mx, my), mul(my, mx));
	const BigFloat &a = L[0];
	const BigFloat &b = L[1];
	const BigFloat &c = L[2];
	const BigFloat esx = of(sx);
	const BigFloat esy = of(sy);
	const BigFloat a2 = add(mul(a, m2r), mul(b, m2i));
	const BigFloat b2 = sub(mul(a, m2i), mul(b, m2r));
	// c' = c|M|^2 - (a Sx + b Sy)|M|^2 + a' Sx + b' Sy
	const BigFloat c2 = add(sub(mul(c, mm), mul(add(mul(a, esx), mul(b, esy)), mm)), add(mul(a2, esx), mul(b2, esy)));
	return {a2, b2, c2};
}

// ---------------------------------------------------------------- lazy exact lines

std::shared_ptr<Line> Line::from_exact(const Line3 &L) {
	auto o = std::make_shared<Line>();
	o->exact_ = L;
	o->fa = L[0].to_float();
	o->fb = L[1].to_float();
	o->fc = L[2].to_float();
	o->ea = 2.0 * U * std::fabs(o->fa);
	o->eb = 2.0 * U * std::fabs(o->fb);
	o->ec = 2.0 * U * std::fabs(o->fc);
	return o;
}

std::shared_ptr<Line> Line::from_approx(EB a, EB b, EB c, Source source) {
	auto o = std::make_shared<Line>();
	o->fa = a.v;
	o->ea = a.e;
	o->fb = b.v;
	o->eb = b.e;
	o->fc = c.v;
	o->ec = c.e;
	o->source_ = std::move(source);
	return o;
}

std::optional<Line3> Line::exact() const {
	if (!exact_ && source_) {
		if (!source_done_) {
			source_done_ = true;
			exact_ = source_();
		}
		return exact_;
	}
	if (!exact_) {
		const std::optional<Line3> L = parent->exact();
		if (!L) {
			return std::nullopt;
		}
		if (mirror.size() == 2) {
			// translated by (dx, dy): c' = c + a dx + b dy
			exact_ = Line3{(*L)[0], (*L)[1], add((*L)[2], add(mul((*L)[0], of(mirror[0])), mul((*L)[1], of(mirror[1]))))};
		} else {
			exact_ = reflect_line(*L, mirror[0], mirror[1], mirror[2], mirror[3]);
		}
	}
	return exact_;
}

std::shared_ptr<Line> Line::translated(double dx, double dy) const {
	auto o = std::make_shared<Line>();
	o->parent = std::shared_ptr<const Line>(std::make_shared<Line>(*this));
	o->mirror = {dx, dy};
	o->fa = fa;
	o->ea = ea;
	o->fb = fb;
	o->eb = eb;
	const EB c2 = eadd(EB{fc, ec}, eadd(emul(EB{fa, ea}, EB{dx, 0.0}), emul(EB{fb, eb}, EB{dy, 0.0})));
	o->fc = c2.v;
	o->ec = c2.e;
	return o;
}

std::shared_ptr<Line> Line::reflected(double sx, double sy, double ex, double ey) const {
	auto o = std::make_shared<Line>();
	o->parent = std::shared_ptr<const Line>(std::make_shared<Line>(*this));
	o->mirror = {sx, sy, ex, ey};
	const EB mx = esub(EB{ex, 0.0}, EB{sx, 0.0});
	const EB my = esub(EB{ey, 0.0}, EB{sy, 0.0});
	const EB mm = eadd(emul(mx, mx), emul(my, my));
	const EB m2r = esub(emul(mx, mx), emul(my, my));
	const EB m2i = eadd(emul(mx, my), emul(my, mx));
	const EB a{fa, ea};
	const EB b{fb, eb};
	const EB c{fc, ec};
	const EB esx{sx, 0.0};
	const EB esy{sy, 0.0};
	const EB a2 = eadd(emul(a, m2r), emul(b, m2i));
	const EB b2 = esub(emul(a, m2i), emul(b, m2r));
	const EB c2 = eadd(esub(emul(c, mm), emul(eadd(emul(a, esx), emul(b, esy)), mm)), eadd(emul(a2, esx), emul(b2, esy)));
	o->fa = a2.v;
	o->ea = a2.e;
	o->fb = b2.v;
	o->eb = b2.e;
	o->fc = c2.v;
	o->ec = c2.e;
	return o;
}

bool Line::concurrent(double px, double py, double qx, double qy, double rx, double ry, double sx, double sy) const {
	// float filter (scalar; first-order rigorous bound with generous constants)
	const double a1 = py - qy;
	const double b1 = qx - px;
	const double c1 = a1 * px + b1 * py;
	const double a2 = ry - sy;
	const double b2 = sx - rx;
	const double c2 = a2 * rx + b2 * ry;
	const double c1m = std::fabs(a1 * px) + std::fabs(b1 * py);
	const double c2m = std::fabs(a2 * rx) + std::fabs(b2 * ry);
	const double m1 = b1 * c2 - b2 * c1;
	const double m2 = a1 * c2 - a2 * c1;
	const double m3 = a1 * b2 - a2 * b1;
	const double m1m = std::fabs(b1) * c2m + std::fabs(b2) * c1m;
	const double m2m = std::fabs(a1) * c2m + std::fabs(a2) * c1m;
	const double m3m = std::fabs(a1 * b2) + std::fabs(a2 * b1);
	const double det = fa * m1 - fb * m2 + fc * m3;
	const double bound = 32.0 * U * (std::fabs(fa) * m1m + std::fabs(fb) * m2m + std::fabs(fc) * m3m)
		+ ea * m1m + eb * m2m + ec * m3m;
	if (std::fabs(det) > bound) {
		return false;
	}
	const std::optional<Line3> L = exact();
	if (!L) {
		return false;
	}
	const Line3 A = line_through(px, py, qx, qy);
	const Line3 B = line_through(rx, ry, sx, sy);
	const BigFloat e1 = sub(mul(A[1], B[2]), mul(B[1], A[2]));
	const BigFloat e2 = sub(mul(A[0], B[2]), mul(B[0], A[2]));
	const BigFloat e3 = sub(mul(A[0], B[1]), mul(B[0], A[1]));
	return add(sub(mul((*L)[0], e1), mul((*L)[1], e2)), mul((*L)[2], e3)).is_zero();
}

bool Line::contains(double x, double y) const {
	const double l = fa * x;
	const double r = fb * y;
	const double v = l + r - fc;
	if (std::fabs(v) > 4.0 * U * (std::fabs(l) + std::fabs(r) + std::fabs(fc)) + ea * std::fabs(x) + eb * std::fabs(y) + ec) {
		return false;
	}
	const std::optional<Line3> L = exact();
	if (!L) {
		return false;
	}
	return sub(add(mul((*L)[0], of(x)), mul((*L)[1], of(y))), (*L)[2]).is_zero();
}

bool Line::contains_h(const Point3 &X, double x, double y, double exy) const {
	const EB v = eadd(esub(emul(EB{fa, ea}, EB{x, exy}), EB{fc, ec}), emul(EB{fb, eb}, EB{y, exy}));
	if (std::fabs(v.v) > v.e) {
		return false;
	}
	const std::optional<Line3> L = exact();
	return L && on_line_h(*L, X);
}

bool Line::tangent_to(const Point3 &C, const BigFloat &rw2, double cx, double cy, double rr, double ecc, double err) const {
	const EB d = eadd(esub(emul(EB{fa, ea}, EB{cx, ecc}), EB{fc, ec}), emul(EB{fb, eb}, EB{cy, ecc}));
	const EB n2 = eadd(emul(EB{fa, ea}, EB{fa, ea}), emul(EB{fb, eb}, EB{fb, eb}));
	const EB v = esub(emul(d, d), emul(EB{rr, err}, n2));
	if (std::fabs(v.v) > v.e) {
		return false;
	}
	const std::optional<Line3> L = exact();
	return L && tangent_zero(*L, C, rw2);
}

} // namespace exact
} // namespace rc
