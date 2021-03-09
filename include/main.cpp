

#include <bits/c++config.h>

#include <chrono>
#include <function2/function2.hpp>
#include <future>
#include <iostream>
#include <iterator>
#include <ratio>
#include <stop_token>
#include <thread>

#include "function2/function2.hpp"
#include "multiqueue.hpp"
#include "singlequeue.hpp"
#include "thiefqueue.hpp"

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

    std::cout << x.name << ": " << time << "/us";

    (static_cast<void>(std::cout << ',' << ' ' << args), ...);

    std::cout << std::endl;

    return time;
}

std::size_t threads = 12;

template <typename TP> void test() {
    TP pool(threads);

    std::size_t jobs = 1000000;

    std::vector<std::future<decltype(std::chrono::system_clock::now())>> _futures;

    _futures.reserve(jobs);

    auto fast_jobs = tick("fast_jobs");

    for (size_t i = 0; i < jobs; i++) {
        _futures.push_back(pool.execute([&]() { return std::chrono::system_clock::now(); }));
    }

    //

    for (auto &f : _futures) {
        f.get();
    }

    tock(fast_jobs);

    _futures.clear();

    auto slow_jobs = tick("slow_jobs");

    for (size_t i = 0; i < 100; i++) {
        _futures.push_back(pool.execute([&]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            return std::chrono::system_clock::now();
        }));
    }

    //

    for (auto &f : _futures) {
        f.get();
    }

    tock(slow_jobs);
}

int main() {
    std::cout << "working" << std::endl;

    ThiefQueue<std::vector<int>> q{2};

    std::jthread t1([&](std::stop_token tok) {
        while (!tok.stop_requested()) {
            if (q.steal()) {
                std::cout << "1, got it\n";
            }
        }
    });

    std::jthread t2([&](std::stop_token tok) {
        while (!tok.stop_requested()) {
            if (q.steal()) {
                std::cout << "2, got it\n";
            }
        }
    });

    for (size_t i = 0; i < 15; i++) {
        q.emplace(0, 0);
    }

    std::this_thread::sleep_for(std::chrono::nanoseconds(100));

    if (q.steal()) {
        std::cout << "0, got it\n";
    }

    std::this_thread::sleep_for(std::chrono::seconds(1));

    t1.request_stop();
    t2.request_stop();

    return 0;
}