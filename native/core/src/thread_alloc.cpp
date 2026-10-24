// F22 (Stage 135): the compute threads' allocation cache (rc/thread_alloc.hpp).
#include "rc/thread_alloc.hpp"

#include <algorithm>
#include <atomic>
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

// The allocation counter (rc_tool regions --allocs): a flag read per
// allocation, a relaxed atomic increment while it is on.
std::atomic<bool> counting{false};
std::atomic<long long> allocations{0}, allocations_system{0}, releases_unsized{0};
std::atomic<long long> system_by_class[72]; // (--allocs) the system mallocs by class (71: a block over MAX_BLOCK, or no cache)

inline void count_alloc() {
	if (counting.load(std::memory_order_relaxed)) {
		allocations.fetch_add(1, std::memory_order_relaxed);
	}
}
inline void count_system() {
	if (counting.load(std::memory_order_relaxed)) {
		allocations_system.fetch_add(1, std::memory_order_relaxed);
	}
}
inline void count_unsized() {
	if (counting.load(std::memory_order_relaxed)) {
		releases_unsized.fetch_add(1, std::memory_order_relaxed);
	}
}

#if !defined(RC_NO_ALLOC_CACHE)
constexpr std::size_t GRAIN = 16;
constexpr std::size_t FINE = 64;         // 16-byte classes, blocks up to 1 KiB
constexpr std::size_t COARSE = 3;        // then 2, 4 and 8 KiB
constexpr std::size_t CLASSES = FINE + COARSE;
constexpr std::size_t MAX_BLOCK = std::size_t(1024) << COARSE;
constexpr std::size_t CLASS_BYTES = 262144; // a thread keeps at most this many bytes of free blocks of a size (B.4: 64 KiB spilled to the system allocator mid-cast)

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
	count_alloc();
	Cache &c = tl_cache;
	if (c.on && n <= MAX_BLOCK) {
		const std::size_t k = class_for(n);
		if (Node *b = c.head[k]) {
			c.head[k] = b->next;
			c.count[k]--;
			return b;
		}
		count_system();
		if (counting.load(std::memory_order_relaxed)) {
			system_by_class[k].fetch_add(1, std::memory_order_relaxed);
		}
		if (void *p = std::malloc(class_size(k))) {
			return p;
		}
		throw std::bad_alloc();
	}
	count_system();
	if (counting.load(std::memory_order_relaxed)) {
		system_by_class[71].fetch_add(1, std::memory_order_relaxed);
	}
	// (B.4) a block of up to MAX_BLOCK bytes is its class's size on any thread, so a
	// compute thread's sized free files it by the class the request takes from
	if (void *p = std::malloc(n <= MAX_BLOCK ? class_size(class_for(n)) : n)) {
		return p;
	}
	throw std::bad_alloc();
}

// A.10: the sized delete knows the bytes asked for, and every block of up to
// MAX_BLOCK bytes from this allocator is its class's size (allocate, both
// branches): the block serves the class the request took from, without asking
// malloc_size (a zone lookup per free). (B.4: it was filed under the largest
// class at most n -- one below, for any n not a class size -- so each free moved
// the block down a class and each allocation of its size missed: 61k system
// mallocs per region on crossings.)
void release_sized(void *p, std::size_t n) noexcept {
	if (p == nullptr) {
		return;
	}
	Cache &c = tl_cache;
	if (c.on) {
		const std::size_t k = n <= MAX_BLOCK ? class_for(n == 0 ? 1 : n) : CLASSES;
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

void release(void *p) noexcept {
	if (p == nullptr) {
		return;
	}
	Cache &c = tl_cache;
	if (c.on) {
		count_unsized();
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
	count_alloc();
	if (void *p = std::malloc(n == 0 ? 1 : n)) {
		return p;
	}
	throw std::bad_alloc();
}
void release(void *p) noexcept { std::free(p); }
void release_sized(void *p, std::size_t) noexcept { std::free(p); }
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

bool alloc_counting(bool on) { return counting.exchange(on); }
long long alloc_count() { return allocations.load(std::memory_order_relaxed); }
long long alloc_system_count() { return allocations_system.load(std::memory_order_relaxed); }
long long alloc_unsized_count() { return releases_unsized.load(std::memory_order_relaxed); }
long long alloc_system_by_class(int k) { return k >= 0 && k < 72 ? system_by_class[k].load(std::memory_order_relaxed) : 0; }
std::size_t alloc_class_size(int k) {
#if !defined(RC_NO_ALLOC_CACHE)
	return k >= 0 && std::size_t(k) < CLASSES ? class_size(std::size_t(k)) : 0;
#else
	(void)k;
	return 0;
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
void operator delete(void *p, std::size_t n) noexcept { release_sized(p, n); }
void operator delete[](void *p, std::size_t n) noexcept { release_sized(p, n); }
void operator delete(void *p, const std::nothrow_t &) noexcept { release(p); }
void operator delete[](void *p, const std::nothrow_t &) noexcept { release(p); }
