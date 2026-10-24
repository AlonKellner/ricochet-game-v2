// F22 (Stage 135): the compute threads' allocation cache. A region's casts
// allocate small blocks by the million, and the system allocator contends
// when several threads do (about 30% of a cast's time at six threads). The
// core replaces the global operator new / delete: on a thread marked as a
// compute thread (the thread pool's workers) freed small blocks go to that
// thread's free lists and come back from them; everywhere else, and for large
// blocks, it is malloc / free as before. Every block is a malloc block (a
// freed one is classed by malloc_size), so a block allocated anywhere may be
// freed anywhere. Off under the sanitizers (they must see every free) and
// where the platform has no malloc_size.
#pragma once

#include <cstddef>

namespace rc {

// Marks the calling thread as a compute thread (its frees are cached) or not;
// returns what it was.
bool set_compute_thread(bool on);

// The allocation counter (every operator new in the process while on; a flag
// read per allocation, an atomic increment while counting): rc_tool regions
// --allocs reports allocations per region. Returns what it was.
bool alloc_counting(bool on);
long long alloc_count();
long long alloc_system_count();  // ... of which went to malloc (the thread's free list empty, or no cache)
long long alloc_unsized_count(); // frees without a size (classed by malloc_size) on compute threads
long long alloc_system_by_class(int k); // the system mallocs by size class (71: over the largest class, or no cache)
std::size_t alloc_class_size(int k);     // a class's block size (0: none)

} // namespace rc
