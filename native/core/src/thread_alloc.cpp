// F22 (Stage 135): the compute threads' allocation cache (rc/thread_alloc.hpp).
#include "rc/thread_alloc.hpp"

#include <cstddef>
#include <cstdlib>
#include <new>

#if defined(__APPLE__)
#include <malloc/malloc.h>
#define RC_BLOCK_SIZE(p) malloc_size(p)
#elif defined(__GLIBC__)
#include <malloc.h>
#define RC_BLOCK_SIZE(p) malloc_usable_size(p)
#endif

#if defined(__has_feature)
#if __has_feature(address_sanitizer) || __has_feature(thread_sanitizer)
#define RC_NO_ALLOC_CACHE 1
#endif
#endif
#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__) || !defined(RC_BLOCK_SIZE)
#define RC_NO_ALLOC_CACHE 1
#endif

namespace {

#if !defined(RC_NO_ALLOC_CACHE)
constexpr std::size_t GRAIN = 16;
constexpr std::size_t FINE = 64;         // 16-byte classes, blocks up to 1 KiB
constexpr std::size_t COARSE = 3;        // then 2, 4 and 8 KiB
constexpr std::size_t CLASSES = FINE + COARSE;
constexpr std::size_t MAX_BLOCK = std::size_t(1024) << COARSE;
constexpr std::size_t CLASS_BYTES = 65536; // a thread keeps at most this many bytes of free blocks of a size

// the class whose blocks hold n bytes, and its blocks' size
std::size_t class_for(std::size_t n) {
	if (n <= GRAIN * FINE) {
		return (n - 1) / GRAIN;
	}
	std::size_t k = FINE, sz = 2048;
	while (sz < n) {
		sz <<= 1;
		k++;
	}
	return k;
}
std::size_t class_size(std::size_t k) { return k < FINE ? (k + 1) * GRAIN : std::size_t(1024) << (k - FINE + 1); }
// the largest class a block of s bytes serves (CLASSES: none)
std::size_t class_holding(std::size_t s) {
	if (s < GRAIN) {
		return CLASSES;
	}
	if (s <= GRAIN * FINE) {
		return s / GRAIN - 1;
	}
	if (s < 2048) {
		return FINE - 1;
	}
	std::size_t k = FINE;
	while (k + 1 < CLASSES && class_size(k + 1) <= s) {
		k++;
	}
	return k;
}

struct Node {
	Node *next;
};

struct Cache {
	Node *head[CLASSES] = {};
	std::size_t count[CLASSES] = {};
	bool on = false;
	~Cache() {
		on = false;
		for (std::size_t c = 0; c < CLASSES; c++) {
			while (Node *n = head[c]) {
				head[c] = n->next;
				std::free(n);
			}
		}
	}
};

thread_local Cache tl_cache;

void *allocate(std::size_t n) {
	if (n == 0) {
		n = 1;
	}
	Cache &c = tl_cache;
	if (c.on && n <= MAX_BLOCK) {
		const std::size_t k = class_for(n);
		if (Node *b = c.head[k]) {
			c.head[k] = b->next;
			c.count[k]--;
			return b;
		}
		if (void *p = std::malloc(class_size(k))) {
			return p;
		}
		throw std::bad_alloc();
	}
	if (void *p = std::malloc(n)) {
		return p;
	}
	throw std::bad_alloc();
}

void release(void *p) noexcept {
	if (p == nullptr) {
		return;
	}
	Cache &c = tl_cache;
	if (c.on) {
		// a block holding s bytes serves the classes up to s (any malloc block)
		const std::size_t s = RC_BLOCK_SIZE(p);
		const std::size_t k = class_holding(s);
		if (k < CLASSES) {
			if (c.count[k] * class_size(k) < CLASS_BYTES) {
				Node *b = static_cast<Node *>(p);
				b->next = c.head[k];
				c.head[k] = b;
				c.count[k]++;
				return;
			}
		}
	}
	std::free(p);
}
#else
void *allocate(std::size_t n) {
	if (void *p = std::malloc(n == 0 ? 1 : n)) {
		return p;
	}
	throw std::bad_alloc();
}
void release(void *p) noexcept { std::free(p); }
#endif

} // namespace

namespace rc {

bool set_compute_thread(bool on) {
#if !defined(RC_NO_ALLOC_CACHE)
	const bool was = tl_cache.on;
	tl_cache.on = on;
	return was;
#else
	(void)on;
	return false;
#endif
}

} // namespace rc

void *operator new(std::size_t n) { return allocate(n); }
void *operator new[](std::size_t n) { return allocate(n); }
void *operator new(std::size_t n, const std::nothrow_t &) noexcept {
	try {
		return allocate(n);
	} catch (...) {
		return nullptr;
	}
}
void *operator new[](std::size_t n, const std::nothrow_t &) noexcept {
	try {
		return allocate(n);
	} catch (...) {
		return nullptr;
	}
}
void operator delete(void *p) noexcept { release(p); }
void operator delete[](void *p) noexcept { release(p); }
void operator delete(void *p, std::size_t) noexcept { release(p); }
void operator delete[](void *p, std::size_t) noexcept { release(p); }
void operator delete(void *p, const std::nothrow_t &) noexcept { release(p); }
void operator delete[](void *p, const std::nothrow_t &) noexcept { release(p); }
