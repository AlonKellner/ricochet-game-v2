// A tiny RGB image with a PNG writer (stored deflate blocks): for debugging
// pictures from tests, no dependencies.
#pragma once

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace rc::test {

struct Image {
	int w = 0;
	int h = 0;
	std::vector<uint8_t> px; // RGB
	Image(int width, int height, uint8_t r = 0, uint8_t g = 0, uint8_t b = 0) : w(width), h(height), px(size_t(width * height * 3)) {
		for (int i = 0; i < w * h; i++) {
			px[size_t(i * 3)] = r;
			px[size_t(i * 3 + 1)] = g;
			px[size_t(i * 3 + 2)] = b;
		}
	}
	void set(int x, int y, uint8_t r, uint8_t g, uint8_t b) {
		if (x < 0 || y < 0 || x >= w || y >= h) return;
		uint8_t *p = &px[size_t((y * w + x) * 3)];
		p[0] = r;
		p[1] = g;
		p[2] = b;
	}
	void line(double x0, double y0, double x1, double y1, uint8_t r, uint8_t g, uint8_t b) {
		const int n = int(std::max(std::abs(x1 - x0), std::abs(y1 - y0))) + 1;
		if (n > 100000) return;
		for (int i = 0; i <= n; i++) {
			const double t = double(i) / n;
			set(int(std::lround(x0 + (x1 - x0) * t)), int(std::lround(y0 + (y1 - y0) * t)), r, g, b);
		}
	}
	void dot(double x, double y, int rad, uint8_t r, uint8_t g, uint8_t b) {
		for (int dy = -rad; dy <= rad; dy++)
			for (int dx = -rad; dx <= rad; dx++)
				if (dx * dx + dy * dy <= rad * rad) set(int(std::lround(x)) + dx, int(std::lround(y)) + dy, r, g, b);
	}

	static uint32_t crc(const uint8_t *d, size_t n, uint32_t c = 0xffffffffu) {
		for (size_t i = 0; i < n; i++) {
			c ^= d[i];
			for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xedb88320u & (0u - (c & 1u)));
		}
		return c;
	}
	bool write_png(const std::string &path) const {
		std::vector<uint8_t> raw;
		for (int y = 0; y < h; y++) {
			raw.push_back(0);
			raw.insert(raw.end(), px.begin() + long(y * w * 3), px.begin() + long((y + 1) * w * 3));
		}
		std::vector<uint8_t> z{0x78, 0x01};
		uint32_t a = 1, b2 = 0;
		for (uint8_t c : raw) {
			a = (a + c) % 65521;
			b2 = (b2 + a) % 65521;
		}
		for (size_t off = 0; off < raw.size(); off += 65535) {
			const size_t len = std::min<size_t>(65535, raw.size() - off);
			z.push_back(off + len == raw.size() ? 1 : 0);
			z.push_back(uint8_t(len & 0xff));
			z.push_back(uint8_t(len >> 8));
			z.push_back(uint8_t(~len & 0xff));
			z.push_back(uint8_t((~len >> 8) & 0xff));
			z.insert(z.end(), raw.begin() + long(off), raw.begin() + long(off + len));
		}
		const uint32_t ad = (b2 << 16) | a;
		for (int s = 24; s >= 0; s -= 8) z.push_back(uint8_t(ad >> s));
		std::FILE *f = std::fopen(path.c_str(), "wb");
		if (!f) return false;
		auto u32 = [&](uint32_t v) { const uint8_t q[4] = {uint8_t(v >> 24), uint8_t(v >> 16), uint8_t(v >> 8), uint8_t(v)}; std::fwrite(q, 1, 4, f); };
		auto chunk = [&](const char *type, const std::vector<uint8_t> &data) {
			u32(uint32_t(data.size()));
			std::vector<uint8_t> td(type, type + 4);
			td.insert(td.end(), data.begin(), data.end());
			std::fwrite(td.data(), 1, td.size(), f);
			u32(crc(td.data(), td.size()) ^ 0xffffffffu);
		};
		const uint8_t sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
		std::fwrite(sig, 1, 8, f);
		std::vector<uint8_t> ihdr;
		for (uint32_t v : {uint32_t(w), uint32_t(h)}) for (int s = 24; s >= 0; s -= 8) ihdr.push_back(uint8_t(v >> s));
		ihdr.insert(ihdr.end(), {8, 2, 0, 0, 0});
		chunk("IHDR", ihdr);
		chunk("IDAT", z);
		chunk("IEND", {});
		std::fclose(f);
		return true;
	}
};

} // namespace rc::test
