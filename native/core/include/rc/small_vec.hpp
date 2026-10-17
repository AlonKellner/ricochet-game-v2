// A vector of trivially copyable values that keeps up to N of them inline
// (Perf P2): BigFloat's limbs, so the values of the low tiers and of the exact
// predicates (2 to 8 limbs) never allocate. The subset of std::vector's
// interface BigFloat uses, with the same semantics.
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <new>
#include <type_traits>

namespace rc {

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
		size_t nc = std::max(c, cap_ * 2);
		T *np = static_cast<T *>(std::malloc(nc * sizeof(T)));
		if (np == nullptr) {
			throw std::bad_alloc();
		}
		if (n_ > 0) {
			std::memcpy(np, p_, n_ * sizeof(T));
		}
		if (p_ != buf_) {
			std::free(p_);
		}
		p_ = np;
		cap_ = nc;
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
			std::free(p_);
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
