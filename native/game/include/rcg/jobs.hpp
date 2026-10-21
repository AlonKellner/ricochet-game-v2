// Stage 121: background work for a live session (the visibility region, the
// preview's exact climb). A LatestJob is one persistent worker thread that
// computes fn(input, cancel) for the newest input it was given: a newer submit
// replaces any waiting input -- the frame never queues stale work -- and, with
// cancel_running, also cancels the running computation (fn polls `cancel`);
// without it the running one finishes and its result is kept (a region a few
// frames old is better than none while the player keeps moving). A finished result is taken by the
// session on its own thread (take), so the game's state is touched by one
// thread only. The worker is its own thread, not a pool thread: fn may use the
// shared pool (rc::ThreadPool) without nesting in it.
#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>

namespace rcg {

template <class In, class Out>
class LatestJob {
public:
	using Fn = std::function<Out(const In &, const std::atomic<bool> &cancel)>;

	explicit LatestJob(Fn fn, bool cancel_running = true)
		: fn_(std::move(fn)), cancel_running_(cancel_running), worker_([this] { loop(); }) {}
	~LatestJob() {
		{
			std::lock_guard<std::mutex> lock(m_);
			stop_ = true;
			cancel_ = true;
		}
		cv_.notify_all();
		worker_.join();
	}
	LatestJob(const LatestJob &) = delete;
	LatestJob &operator=(const LatestJob &) = delete;

	// compute fn(in) in the background; `ticket` comes back with the result
	void submit(In in, long long ticket) {
		{
			std::lock_guard<std::mutex> lock(m_);
			pending_ = std::make_pair(std::move(in), ticket);
			if (cancel_running_) {
				cancel_ = running_; // the running computation is stale now
				result_.reset();    // so is a finished, untaken one
			}
		}
		cv_.notify_all();
	}

	// drop the waiting input and a finished result; cancel the running computation
	void cancel() {
		std::lock_guard<std::mutex> lock(m_);
		pending_.reset();
		result_.reset();
		cancel_ = running_;
	}

	// the newest finished result (once), if any (its ticket says which input)
	std::optional<std::pair<long long, Out>> take() {
		std::lock_guard<std::mutex> lock(m_);
		std::optional<std::pair<long long, Out>> r = std::move(result_);
		result_.reset();
		return r;
	}

	// a finished result not taken yet
	bool ready() const {
		std::lock_guard<std::mutex> lock(m_);
		return result_.has_value();
	}

	// a computation running or waiting
	bool busy() const {
		std::lock_guard<std::mutex> lock(m_);
		return running_ || pending_.has_value();
	}

	// block until nothing runs or waits (tests, a fire that needs the result)
	void wait_idle() {
		std::unique_lock<std::mutex> lock(m_);
		idle_.wait(lock, [&] { return !running_ && !pending_; });
	}

private:
	void loop() {
		for (;;) {
			std::pair<In, long long> job;
			{
				std::unique_lock<std::mutex> lock(m_);
				cv_.wait(lock, [&] { return stop_ || pending_.has_value(); });
				if (stop_) {
					return;
				}
				job = std::move(*pending_);
				pending_.reset();
				running_ = true;
				cancel_ = false;
			}
			Out out = fn_(job.first, cancel_);
			{
				std::lock_guard<std::mutex> lock(m_);
				running_ = false;
				if (!cancel_ && (!cancel_running_ || !pending_)) {
					result_ = std::make_pair(job.second, std::move(out));
				}
			}
			idle_.notify_all();
		}
	}

	Fn fn_;
	bool cancel_running_ = true;
	mutable std::mutex m_;
	std::condition_variable cv_, idle_;
	std::optional<std::pair<In, long long>> pending_;
	std::optional<std::pair<long long, Out>> result_;
	std::atomic<bool> cancel_{false};
	bool running_ = false;
	bool stop_ = false;
	std::thread worker_; // last: started once the rest is constructed
};

} // namespace rcg
