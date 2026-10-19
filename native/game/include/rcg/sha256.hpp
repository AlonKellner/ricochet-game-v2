// Minimal SHA-256 (FIPS 180-4): the session digest's hashes (Godot's
// String.sha256_text) and the sweep goldens' (HashingContext). Stage 116: moved
// here from the test support (which includes it).
#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <string>

namespace rcg {

class Sha256 {
public:
	Sha256() { reset(); }

	void reset() {
		h_ = {0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au, 0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u};
		len_ = 0;
		buf_len_ = 0;
	}

	void update(const unsigned char *data, size_t n) {
		len_ += n;
		while (n > 0) {
			const size_t take = std::min(n, size_t(64) - buf_len_);
			std::memcpy(buf_.data() + buf_len_, data, take);
			buf_len_ += take;
			data += take;
			n -= take;
			if (buf_len_ == 64) {
				block(buf_.data());
				buf_len_ = 0;
			}
		}
	}

	std::array<unsigned char, 32> finish() {
		const uint64_t bits = len_ * 8;
		const unsigned char one = 0x80;
		update(&one, 1);
		const unsigned char zero = 0;
		while (buf_len_ != 56) {
			update(&zero, 1);
		}
		unsigned char lenb[8];
		for (int i = 0; i < 8; i++) {
			lenb[i] = static_cast<unsigned char>(bits >> (56 - 8 * i));
		}
		update(lenb, 8);
		std::array<unsigned char, 32> out{};
		for (int i = 0; i < 8; i++) {
			out[size_t(i) * 4] = static_cast<unsigned char>(h_[size_t(i)] >> 24);
			out[size_t(i) * 4 + 1] = static_cast<unsigned char>(h_[size_t(i)] >> 16);
			out[size_t(i) * 4 + 2] = static_cast<unsigned char>(h_[size_t(i)] >> 8);
			out[size_t(i) * 4 + 3] = static_cast<unsigned char>(h_[size_t(i)]);
		}
		return out;
	}

	// hex digest of a string
	static std::string hex(const std::string &s) {
		Sha256 ctx;
		ctx.update(reinterpret_cast<const unsigned char *>(s.data()), s.size());
		const auto d = ctx.finish();
		static const char *digits = "0123456789abcdef";
		std::string out;
		out.reserve(64);
		for (unsigned char c : d) {
			out.push_back(digits[c >> 4]);
			out.push_back(digits[c & 15]);
		}
		return out;
	}

private:
	static uint32_t rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

	void block(const unsigned char *p) {
		static const uint32_t K[64] = {
			0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
			0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
			0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
			0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
			0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
			0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
			0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
			0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u};
		uint32_t w[64];
		for (int i = 0; i < 16; i++) {
			w[i] = (uint32_t(p[i * 4]) << 24) | (uint32_t(p[i * 4 + 1]) << 16) | (uint32_t(p[i * 4 + 2]) << 8) | uint32_t(p[i * 4 + 3]);
		}
		for (int i = 16; i < 64; i++) {
			const uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
			const uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
			w[i] = w[i - 16] + s0 + w[i - 7] + s1;
		}
		uint32_t a = h_[0], b = h_[1], c = h_[2], d = h_[3], e = h_[4], f = h_[5], g = h_[6], h = h_[7];
		for (int i = 0; i < 64; i++) {
			const uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
			const uint32_t ch = (e & f) ^ (~e & g);
			const uint32_t t1 = h + S1 + ch + K[i] + w[i];
			const uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
			const uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
			const uint32_t t2 = S0 + maj;
			h = g;
			g = f;
			f = e;
			e = d + t1;
			d = c;
			c = b;
			b = a;
			a = t1 + t2;
		}
		h_[0] += a;
		h_[1] += b;
		h_[2] += c;
		h_[3] += d;
		h_[4] += e;
		h_[5] += f;
		h_[6] += g;
		h_[7] += h;
	}

	std::array<uint32_t, 8> h_{};
	std::array<unsigned char, 64> buf_{};
	size_t buf_len_ = 0;
	uint64_t len_ = 0;
};

} // namespace rcg
