

#include <atomic>

#include "deque.hpp"
#include "function2/function2.hpp"
#include "semaphore.hpp"
#include "shared.hpp"

class Threadpool {
  public:
    explicit Threadpool(std::size_t threads = std::thread::hardware_concurrency()) : _sem(0) {
        for (std::size_t i = 0; i < threads; ++i) {
            _threads.emplace_back([&](std::stop_token tok) {
                while (true) {
                    // Wait to be signalled
                    _sem.acquire();

                    if (tok.stop_requested() && !_in_flight.load(std::memory_order::acquire)) {
                        return;
                    }

                    while (true) {
                        if (std::optional one_shot = _tasks.steal()) {
                            _in_flight.fetch_sub(1, std::memory_order::release);
                            std::invoke(std::move(*one_shot));
                            break;
                        }
                    }
                }
            });
        }
    }

    ~Threadpool() {
        for (auto &thread : _threads) {
            thread.request_stop();
        }
        _sem.release(_threads.size());
    }

    template <typename F, typename... Args> auto execute(F &&f, Args &&...args) {
        //
        auto task = NullaryOneShot(bind(std::forward<F>(f), std::forward<Args>(args)...));

        auto future = task.get_future();

        _tasks.emplace(std::move(task));

        _in_flight.fetch_add(1, std::memory_order::release);

        _sem.release();

        return future;
    }

  private:
    std::atomic<std::int64_t> _in_flight;
    Semaphore _sem;
    cj::Deque<fu2::unique_function<void() &&>> _tasks;
    std::vector<std::jthread> _threads;
};