// Stage 124: up to C values kept inline, never allocating -- a beam's roots on
// a carrier (at most two crossings and the antipode of a curved beam). The
// subset of std::vector's interface its users need; converts to a vector.
#pragma once

#include <array>
#include <cstddef>
#include <initializer_list>
#include <stdexcept>
#include <utility>
#include <vector>

namespace rc {

template <class T, size_t C>
class InlineVec {
public:
	using value_type = T;
	using iterator = T *;
	using const_iterator = const T *;

	InlineVec() = default;
	InlineVec(std::initializer_list<T> il) {
		for (const T &v : il) {
			push_back(v);
		}
	}

	size_t size() const { return n_; }
	bool empty() const { return n_ == 0; }
	T &operator[](size_t i) { return v_[i]; }
	const T &operator[](size_t i) const { return v_[i]; }
	T &back() { return v_[n_ - 1]; }
	const T &back() const { return v_[n_ - 1]; }
	iterator begin() { return v_.data(); }
	iterator end() { return v_.data() + n_; }
	const_iterator begin() const { return v_.data(); }
	const_iterator end() const { return v_.data() + n_; }

	void push_back(const T &v) {
		if (n_ == C) {
			throw std::length_error("InlineVec: full");
		}
		v_[n_++] = v;
	}
	void clear() { n_ = 0; }
	iterator erase(const_iterator pos) {
		T *p = begin() + (pos - begin());
		for (T *q = p; q + 1 < end(); q++) {
			*q = std::move(*(q + 1));
		}
		n_--;
		return p;
	}

	operator std::vector<T>() const { return std::vector<T>(begin(), end()); }

private:
	std::array<T, C> v_{};
	size_t n_ = 0;
};

} // namespace rc
