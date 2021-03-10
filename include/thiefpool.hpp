

#include <atomic>

#include "deque.hpp"
#include "function2/function2.hpp"
#include "semaphore.hpp"
#include "shared.hpp"

class Thiefpool {
  public:
    explicit Thiefpool(std::size_t threads = std::thread::hardware_concurrency()) : _deques(threads) {
        for (std::size_t i = 0; i < threads; ++i) {
            _threads.emplace_back([&, id = i](std::stop_token tok) {
                while (!tok.stop_requested()) {
                    // Wait for work/stop-signal to be sent
                    _deques[id].sem.acquire();

                    // Spin until we have done (one) job or stop is requested
                    while (!tok.stop_requested()) {
                        if (std::optional one_shot = _deques[id].tasks.steal()) {
                            std::invoke(std::move(*one_shot));
                            break;
                        }
                    }
                }
            });
        }
    }

    ~Thiefpool() {
        for (auto &t : _threads) {
            t.request_stop();
        }
        for (auto &d : _deques) {
            d.sem.release();
        }
    }

    template <typename F, typename... Args> auto execute(F &&f, Args &&...args) {
        //
        auto task = NullaryOneShot(bind(std::forward<F>(f), std::forward<Args>(args)...));

        auto future = task.get_future();

        _deques[count % _deques.size()].tasks.emplace(std::move(task));

        _deques[count % _deques.size()].sem.release();

        count++;

        return future;
    }

  private:
    std::size_t count = 0;

    struct SemDeque {
        Semaphore sem{0};
        cj::Deque<fu2::unique_function<void() &&>> tasks;
    };

    std::vector<SemDeque> _deques;
    std::vector<std::jthread> _threads;
};
