#pragma once
#include <thread>
#include <vector>
#include <queue>
#include <functional>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <cassert>

namespace cleainput {

class ThreadPool {
public:
    explicit ThreadPool(size_t num_threads = 0) {
        size_t n = num_threads == 0
            ? std::thread::hardware_concurrency()
            : num_threads;
        assert(n > 0);
        stop_.store(false);
        workers_.reserve(n);
        for (size_t i = 0; i < n; i++) {
            workers_.emplace_back([this] {
                while (true) {
                    std::function<void()> task;
                    {
                        std::unique_lock<std::mutex>
                            lock(mutex_);
                        cv_.wait(lock, [this] {
                            return stop_.load()
                                || !tasks_.empty();
                        });
                        if (stop_.load()
                            && tasks_.empty())
                            return;
                        task = std::move(tasks_.front());
                        tasks_.pop();
                    }
                    task();
                }
            });
        }
    }

    ~ThreadPool() {
        stop_.store(true);
        cv_.notify_all();
        for (auto& w : workers_)
            w.join();
    }

    void enqueue(std::function<void()> task) {
        {
            std::lock_guard<std::mutex>
                lock(mutex_);
            tasks_.push(std::move(task));
        }
        cv_.notify_one();
    }

    size_t size() const {
        return workers_.size();
    }

private:
    std::vector<std::thread>          workers_;
    std::queue<std::function<void()>> tasks_;
    std::mutex                        mutex_;
    std::condition_variable           cv_;
    std::atomic<bool>                 stop_;
};

} // namespace cleainput
