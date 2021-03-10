

#include <atomic>
#include <cassert>
#include <chrono>
#include <function2/function2.hpp>
#include <future>
#include <iostream>
#include <iterator>
#include <memory>
#include <ratio>
#include <stop_token>
#include <thread>

#include "function2/function2.hpp"
#include "threadpool.hpp"

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

template <typename TP> void test() {
    std::atomic_int_fast64_t threads = 10;

    // {
    //     auto fast_jobs = tick("fast_jobs");

    //     std::atomic_int_fast64_t count;

    //     {
    //         TP pool(threads);

    //         for (int i = 0; i < threads * 100000; i++) {
    //             pool.enqueue([&]() { count++; });
    //         }
    //     }

    //     tock(fast_jobs, count);

    //     assert(count == threads * 100000);
    // }

    // {
    //     auto slow_jobs = tick("slow_jobs");

    //     std::atomic_int_fast64_t count;

    //     {
    //         TP pool(threads);

    //         for (int i = 0; i < threads * 10; i++) {
    //             pool.enqueue([&]() {
    //                 ++count;
    //                 std::this_thread::sleep_for(std::chrono::milliseconds(100));
    //             });
    //         }
    //     }

    //     tock(slow_jobs, count);

    //     assert(count == threads * 10);
    // }

    {
        auto het_jobs = tick("het_jobs");

        std::atomic_int_fast64_t count;

        {
            TP pool(threads);

            for (int i = 0; i < threads * 2; i++) {
                pool.enqueue([&, i]() {
                    ++count;
                    std::this_thread::sleep_for(std::chrono::milliseconds((i % threads) + 1));
                });
            }
        }

        tock(het_jobs, count);

        assert(count == threads * 2);
    }
}

std::atomic<int> count;

struct Talker {
    bool alive = true;

    Talker() : alive(true) {
        ++count;
        std::cout << "construct\n";
    }

    Talker(Talker const &other) : alive(other.alive) {}
    Talker(Talker &&other) : alive(std::exchange(other.alive, false)) {}

    ~Talker() {
        if (alive) {
            alive = false;
            --count;
            std::cout << "destruct\n";
        }
    }
};

int main() {
    test<riften::MonoPool>();  // //

    std::cout << "Done!" << std::endl;
}