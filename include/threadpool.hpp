

#include <atomic>

#include "semaphore.hpp"
#include "shared.hpp"
#include "thiefdeque.hpp"

class Threadpool {
  public:
    explicit Threadpool(std::size_t threads = std::thread::hardware_concurrency()) {
        for (std::size_t i = 0; i < threads; ++i) {
            _threads.emplace_back([&](std::stop_token tok) {
                //
                std::unique_lock<std::mutex> lock(_mutex, std::defer_lock);

                while (true) {
                    lock.lock();

                    _cv.wait(lock, [&]() { return !_tasks.empty() || tok.stop_requested(); });

                    if (tok.stop_requested() && _tasks.empty()) {
                        return;
                    }

                    auto one_shot = std::move(_tasks.front());

                    _tasks.pop();

                    lock.unlock();

                    std::invoke(std::move(one_shot));
                }
            });
        }
    }

    ~ThreadPool() {
        for (auto &thread : _threads) {
            thread.request_stop();
        }
        _cv.notify_all();
    }

    template <typename F, typename... Args> auto execute(F &&f, Args &&...args) {
        //
        auto task = NullaryOneShot(bind(std::forward<F>(f), std::forward<Args>(args)...));

        auto future = task.get_future();

        std::unique_lock<std::mutex> lock(_mutex);

        _tasks.emplace(std::move(task));

        lock.unlock();

        _cv.notify_one();

        return future;
    }

  private:
    LightweightSemaphore _sem;
    std::atomic<std::uint64_t> _in_flight;
};
