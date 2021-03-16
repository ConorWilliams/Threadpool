

#include <atomic>
#include <chrono>
#include <future>
#include <iostream>
#include <string_view>
#include <thread>
#include <vector>

#include "riften/thiefpool.hpp"

struct clock_tick {
    std::string name;
    typename std::chrono::high_resolution_clock::time_point start;
};

inline clock_tick tick(std::string const &name, bool print = false) {
    if (print) {
        std::cout << "Timing: " << name << '\n';
    }
    return {name, std::chrono::high_resolution_clock::now()};
}

template <typename... Args> int tock(clock_tick &x, Args &&...args) {
    using namespace std::chrono;

    auto const stop = high_resolution_clock::now();

    auto const time = duration_cast<milliseconds>(stop - x.start).count();

    std::cout << x.name << ": " << time << "/ms";

    (static_cast<void>(std::cout << ',' << ' ' << args), ...);

    std::cout << std::endl;

    return time;
}

template <typename T> void Benchmark(int threads, std::string_view str) {
    {
        auto fast = tick("fast");

        std::atomic<std::size_t> counter;

        {
            T pool(threads);

            for (std::size_t i = 0; i < 10'000'000; i++) {
                pool.enqueue([&]() { counter.fetch_add(1); });
            }
        }

        tock(fast, str, counter.load());
    }

    {
        auto het = tick(" het");

        std::atomic<std::size_t> counter;

        {
            T pool(threads);

            for (std::size_t i = 0; i < 10'000; i++) {
                pool.enqueue([&]() {
                    counter.fetch_add(1);
                    std::this_thread::sleep_for(std::chrono::microseconds(i));
                });
            }
        }

        tock(het, str, counter.load());
    }

    {
        auto slow = tick("slow");

        std::atomic<std::size_t> counter;

        {
            T pool(threads);

            for (std::size_t i = 0; i < 500; i++) {
                pool.enqueue([&]() {
                    counter.fetch_add(1);
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                });
            }
        }

        tock(slow, str, counter.load());
    }
}

int main() {
    Benchmark<riften::Thiefpool>(12, "riften");
    return 0;
}