#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN

#include "riften/thiefpool.hpp"

#include <future>
#include <iostream>
#include <thread>
#include <vector>

#include "doctest/doctest.h"

TEST_CASE("Construct-Destruct") {
    for (std::size_t i = 0; i < 10000; i++) {
        riften::Thiefpool pool;
    }
}

void null_jobs(std::size_t threads) {
    std::vector<std::future<void>> future;

    {
        riften::Thiefpool pool(threads);

        for (std::size_t i = 0; i < (1ul << 21); i++) {
            future.push_back(pool.enqueue([]() {}));
        }
    }

    for (auto&& f : future) {
        REQUIRE(f.valid());
        f.wait();
    }
}

TEST_CASE("Null jobs - 1 thread" * doctest::timeout(25)) { null_jobs(1); }
TEST_CASE("Null jobs - 2 thread" * doctest::timeout(25)) { null_jobs(2); }
TEST_CASE("Null jobs - 3 thread" * doctest::timeout(25)) { null_jobs(3); }
TEST_CASE("Null jobs - 4 thread" * doctest::timeout(25)) { null_jobs(4); }
TEST_CASE("Null jobs - 12 thread" * doctest::timeout(25)) { null_jobs(12); }

void detach_job(std::size_t threads) {
    std::atomic<std::size_t> counter;

    {
        riften::Thiefpool pool(threads);

        for (std::size_t i = 0; i < (1ul << 21); i++) {
            pool.enqueue_detach([&]() { counter.fetch_add(1); });
        }
    }

    REQUIRE(counter == (1ul << 21));
}

TEST_CASE("Detach jobs - 1 thread" * doctest::timeout(25)) { detach_job(1); }
TEST_CASE("Detach jobs - 2 thread" * doctest::timeout(25)) { detach_job(2); }
TEST_CASE("Detach jobs - 3 thread" * doctest::timeout(25)) { detach_job(3); }
TEST_CASE("Detach jobs - 4 thread" * doctest::timeout(25)) { detach_job(4); }
TEST_CASE("Detach jobs - 12 thread" * doctest::timeout(25)) { detach_job(12); }

void fast_jobs(std::size_t threads) {
    std::vector<std::future<int>> future;

    {
        riften::Thiefpool pool(threads);

        for (std::size_t i = 0; i < (1ul << 21); i++) {
            future.push_back(pool.enqueue([](int x) { return x; }, (int)i));
        }
    }

    for (std::size_t i = 0; i < future.size(); i++) {
        REQUIRE(future[i].valid());
        REQUIRE(future[i].get() == i);
    }
}

TEST_CASE("Fast jobs - 1 thread" * doctest::timeout(25)) { fast_jobs(1); }
TEST_CASE("Fast jobs - 2 thread" * doctest::timeout(25)) { fast_jobs(2); }
TEST_CASE("Fast jobs - 3 thread" * doctest::timeout(25)) { fast_jobs(3); }
TEST_CASE("Fast jobs - 4 thread" * doctest::timeout(25)) { fast_jobs(4); }
TEST_CASE("Fast jobs - 12 thread" * doctest::timeout(25)) { fast_jobs(12); }

void waiting_jobs(std::size_t threads) {
    std::vector<std::future<int>> future;

    {
        riften::Thiefpool pool(threads);

        for (std::size_t i = 0; i < 100; i++) {
            future.push_back(pool.enqueue(
                [](int x) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    return x;
                },
                (int)i));
        }
    }

    for (std::size_t i = 0; i < future.size(); i++) {
        REQUIRE(future[i].valid());
        REQUIRE(future[i].get() == i);
    }
}

TEST_CASE("Waiting jobs - 1 thread" * doctest::timeout(25)) { waiting_jobs(1); }
TEST_CASE("Waiting jobs - 2 thread" * doctest::timeout(25)) { waiting_jobs(2); }
TEST_CASE("Waiting jobs - 3 thread" * doctest::timeout(25)) { waiting_jobs(3); }
TEST_CASE("Waiting jobs - 4 thread" * doctest::timeout(25)) { waiting_jobs(4); }
TEST_CASE("Waiting jobs - 12 thread" * doctest::timeout(25)) { waiting_jobs(12); }

void heterogenous_wait(std::size_t threads) {
    std::vector<std::future<void>> future;

    {
        riften::Thiefpool pool(threads);

        for (std::size_t i = 0; i < 10 * threads; i++) {
            future.push_back(
                pool.enqueue([i]() { std::this_thread::sleep_for(std::chrono::milliseconds(i * 10)); }));
        }
    }

    for (std::size_t i = 0; i < future.size(); i++) {
        REQUIRE(future[i].valid());
        future[i].get();
    }
}

TEST_CASE("Heterogenous waiting jobs - 1 thread" * doctest::timeout(25)) { heterogenous_wait(1); }
TEST_CASE("Heterogenous waiting jobs - 2 thread" * doctest::timeout(25)) { heterogenous_wait(2); }
TEST_CASE("Heterogenous waiting jobs - 3 thread" * doctest::timeout(25)) { heterogenous_wait(3); }
TEST_CASE("Heterogenous waiting jobs - 4 thread" * doctest::timeout(25)) { heterogenous_wait(4); }
TEST_CASE("Heterogenous waiting jobs - 12 thread" * doctest::timeout(25)) { heterogenous_wait(12); }

void heavy_jobs(std::size_t threads) {
    std::vector<std::future<bool>> future;

    {
        riften::Thiefpool pool(threads);

        for (std::size_t i = 0; i < 100; i++) {
            future.push_back(pool.enqueue([]() {
                int big_prime = 50'000'719;
                for (int i = 2; i < big_prime; i++) {
                    if (big_prime % i == 0) {
                        return false;
                    }
                }
                return true;
            }));
        }
    }

    for (std::size_t i = 0; i < future.size(); i++) {
        REQUIRE(future[i].valid());
        REQUIRE(future[i].get());
    }
}

TEST_CASE("Heavy jobs - 1 thread" * doctest::timeout(25)) { heavy_jobs(1); }
TEST_CASE("Heavy jobs - 2 thread" * doctest::timeout(25)) { heavy_jobs(2); }
TEST_CASE("Heavy jobs - 3 thread" * doctest::timeout(25)) { heavy_jobs(3); }
TEST_CASE("Heavy jobs - 4 thread" * doctest::timeout(25)) { heavy_jobs(4); }
TEST_CASE("Heavy jobs - 12 thread" * doctest::timeout(25)) { heavy_jobs(12); }
