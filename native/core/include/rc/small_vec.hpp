// A vector of trivially copyable values that keeps up to N of them inline
// (Perf P2): BigFloat's limbs, so the values of the low tiers and of the exact
// predicates (2 to 8 limbs) never allocate. The subset of std::vector's
// interface BigFloat uses, with the same semantics.
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <new>
#include <type_traits>

namespace rc {

namespace small_vec_detail {

// Stage 124: freed heap blocks kept per thread by power-of-two size (64 B to
// 128 KiB), so a value past its inline limbs reuses one instead of going to
// malloc: the region's parallel casts made malloc's small zones contended (a
// third of their time at 8 threads). Bounded per size; a block freed on another
// thread joins that thread's cache; after the thread's cache is gone, plain free.
struct BlockCache {
	static constexpr size_t CLASSES = 12;
	static constexpr size_t KEEP = 256;
	std::array<void *[KEEP], CLASSES> blocks{};
	std::array<size_t, CLASSES> counts{};
	~BlockCache();
};
inline thread_local bool cache_gone = false;
inline BlockCache::~BlockCache() {
	cache_gone = true;
	for (size_t k = 0; k < CLASSES; k++) {
		for (size_t i = 0; i < counts[k]; i++) {
			std::free(blocks[k][i]);
		}
	}
}
inline BlockCache &cache() {
	thread_local BlockCache c;
	return c;
}
// the class holding `bytes` (64 << k >= bytes), or CLASSES past the largest
inline size_t class_of(size_t bytes) {
	size_t k = 0;
	while (k < BlockCache::CLASSES && (size_t(64) << k) < bytes) {
		k++;
	}
	return k;
}
// a block of at least `bytes`; its usable size in `got`
inline void *alloc(size_t bytes, size_t &got) {
	const size_t k = class_of(bytes);
	if (k >= BlockCache::CLASSES) {
		got = bytes;
		return std::malloc(bytes);
	}
	got = size_t(64) << k;
	if (!cache_gone) {
		BlockCache &c = cache();
		if (c.counts[k] > 0) {
			return c.blocks[k][--c.counts[k]];
		}
	}
	return std::malloc(got);
}
inline void release(void *p, size_t bytes) {
	const size_t k = class_of(bytes);
	if (k < BlockCache::CLASSES && (size_t(64) << k) == bytes && !cache_gone) {
		BlockCache &c = cache();
		if (c.counts[k] < BlockCache::KEEP) {
			c.blocks[k][c.counts[k]++] = p;
			return;
		}
	}
	std::free(p);
}

} // namespace small_vec_detail

template <class T, size_t N>
class SmallVec {
	static_assert(std::is_trivially_copyable_v<T>, "SmallVec holds trivially copyable values");

public:
	using value_type = T;
	using iterator = T *;
	using const_iterator = const T *;

	SmallVec() = default;
	SmallVec(size_t n, const T &v) { assign(n, v); }
	explicit SmallVec(size_t n) { assign(n, T{}); }
	SmallVec(std::initializer_list<T> il) {
		reserve(il.size());
		std::copy(il.begin(), il.end(), p_);
		n_ = il.size();
	}
	SmallVec(const SmallVec &o) { copy_from(o); }
	SmallVec(SmallVec &&o) noexcept { move_from(o); }
	SmallVec &operator=(const SmallVec &o) {
		if (this != &o) {
			n_ = 0;
			copy_from(o);
		}
		return *this;
	}
	SmallVec &operator=(SmallVec &&o) noexcept {
		if (this != &o) {
			release();
			move_from(o);
		}
		return *this;
	}
	~SmallVec() { release(); }

	size_t size() const { return n_; }
	bool empty() const { return n_ == 0; }
	T *data() { return p_; }
	const T *data() const { return p_; }
	T &operator[](size_t i) { return p_[i]; }
	const T &operator[](size_t i) const { return p_[i]; }
	T &back() { return p_[n_ - 1]; }
	const T &back() const { return p_[n_ - 1]; }
	iterator begin() { return p_; }
	iterator end() { return p_ + n_; }
	const_iterator begin() const { return p_; }
	const_iterator end() const { return p_ + n_; }

	void reserve(size_t c) {
		if (c <= cap_) {
			return;
		}
		size_t got = 0;
		T *np = static_cast<T *>(small_vec_detail::alloc(std::max(c, cap_ * 2) * sizeof(T), got));
		if (np == nullptr) {
			throw std::bad_alloc();
		}
		if (n_ > 0) {
			std::memcpy(np, p_, n_ * sizeof(T));
		}
		if (p_ != buf_) {
			small_vec_detail::release(p_, cap_ * sizeof(T));
		}
		p_ = np;
		cap_ = got / sizeof(T);
	}
	void assign(size_t n, const T &v) {
		reserve(n);
		std::fill(p_, p_ + n, v);
		n_ = n;
	}
	void resize(size_t n, const T &v = T{}) {
		reserve(n);
		if (n > n_) {
			std::fill(p_ + n_, p_ + n, v);
		}
		n_ = n;
	}
	void push_back(const T &v) {
		if (n_ == cap_) {
			const T copy = v; // v may live in this vector
			reserve(n_ + 1);
			p_[n_++] = copy;
			return;
		}
		p_[n_++] = v;
	}
	void pop_back() { n_--; }
	void clear() { n_ = 0; }
	iterator erase(const_iterator first, const_iterator last) {
		T *f = p_ + (first - p_);
		const size_t k = size_t(last - first);
		std::memmove(f, f + k, size_t(end() - (f + k)) * sizeof(T));
		n_ -= k;
		return f;
	}

	bool operator==(const SmallVec &o) const { return n_ == o.n_ && std::equal(begin(), end(), o.begin()); }
	bool operator!=(const SmallVec &o) const { return !(*this == o); }

private:
	T buf_[N];
	T *p_ = buf_;
	size_t n_ = 0;
	size_t cap_ = N;

	void release() {
		if (p_ != buf_) {
			small_vec_detail::release(p_, cap_ * sizeof(T));
		}
		p_ = buf_;
		cap_ = N;
		n_ = 0;
	}
	void copy_from(const SmallVec &o) {
		reserve(o.n_);
		if (o.n_ > 0) {
			std::memcpy(p_, o.p_, o.n_ * sizeof(T));
		}
		n_ = o.n_;
	}
	void move_from(SmallVec &o) {
		if (o.p_ == o.buf_) {
			p_ = buf_;
			cap_ = N;
			if (o.n_ > 0) {
				std::memcpy(buf_, o.buf_, o.n_ * sizeof(T));
			}
			n_ = o.n_;
		} else {
			p_ = o.p_;
			cap_ = o.cap_;
			n_ = o.n_;
			o.p_ = o.buf_;
			o.cap_ = N;
		}
		o.n_ = 0;
	}
};

} // namespace rc
