#include <iostream>

#include "doctest/doctest.h"
#include "riften/thiefpool.hpp"

TEST_CASE("README example") {
    // Create thread pool with 4 worker threads.
    riften::Thiefpool pool(4);

    // Enqueue and store future.
    auto result = pool.enqueue([](int x) { return x; }, 42);

    // Get result from future.
    std::cout << result.get() << std::endl;
}
