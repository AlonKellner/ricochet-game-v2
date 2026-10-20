// Stage 120: a persistent pool of worker threads (the visibility region's
// cast batches; the game's background jobs build on it). parallel_for runs
// body(i) for i in [0, n) on the calling thread and up to `workers` pool
// threads, handing out indices one at a time (a slow item does not hold up the
// rest) and returning when all are done. The work done is the same as a serial
// loop's; only the thread an index runs on differs.
#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace rc {

class ThreadPool {
public:
	explicit ThreadPool(size_t threads) {
		for (size_t k = 0; k < threads; k++) {
			threads_.emplace_back([this, k] { loop(k); });
		}
	}
	~ThreadPool() {
		{
			std::lock_guard<std::mutex> lock(m_);
			stop_ = true;
		}
		cv_.notify_all();
		for (std::thread &t : threads_) {
			t.join();
		}
	}
	ThreadPool(const ThreadPool &) = delete;
	ThreadPool &operator=(const ThreadPool &) = delete;

	size_t size() const { return threads_.size(); }

	// body(worker, i): worker 0 is the caller, 1..workers the pool's threads
	// (each worker index runs on one thread at a time: per-worker state is safe).
	// One parallel_for at a time per pool (callers serialize on the pool).
	void parallel_for(size_t n, size_t workers, const std::function<void(size_t, size_t)> &body) {
		std::lock_guard<std::mutex> one(run_);
		workers = std::min(workers, threads_.size());
		std::atomic<size_t> next{0};
		auto drain = [&](size_t worker) {
			for (size_t i = next++; i < n; i = next++) {
				body(worker, i);
			}
		};
		{
			std::lock_guard<std::mutex> lock(m_);
			job_ = drain;
			active_ = workers;
			pending_ = workers;
			gen_++;
		}
		cv_.notify_all();
		drain(0);
		std::unique_lock<std::mutex> lock(m_);
		done_.wait(lock, [&] { return pending_ == 0; });
		job_ = nullptr;
	}

	// the process's pool for compute (the hardware threads but one, at least one)
	static ThreadPool &shared() {
		static ThreadPool pool(std::max<size_t>(1, std::thread::hardware_concurrency() > 1 ? std::thread::hardware_concurrency() - 1 : 1));
		return pool;
	}

private:
	void loop(size_t k) {
		size_t seen = 0;
		for (;;) {
			std::function<void(size_t)> job;
			{
				std::unique_lock<std::mutex> lock(m_);
				cv_.wait(lock, [&] { return stop_ || (gen_ != seen && k < active_); });
				if (stop_) {
					return;
				}
				seen = gen_;
				job = job_;
			}
			job(k + 1);
			{
				std::lock_guard<std::mutex> lock(m_);
				if (--pending_ == 0) {
					done_.notify_one();
				}
			}
		}
	}

	std::vector<std::thread> threads_;
	std::mutex m_, run_;
	std::condition_variable cv_, done_;
	std::function<void(size_t)> job_;
	size_t active_ = 0, pending_ = 0, gen_ = 0;
	bool stop_ = false;
};

} // namespace rc
