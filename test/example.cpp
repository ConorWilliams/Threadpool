#include <iostream>
#include <riften/thiefpool.hpp>

int main() {
    // Create thread pool with 4 worker threads
    riften::Thiefpool pool(4);

    // Enqueue and store future
    auto result = pool.enqueue([](int answer) { return answer; }, 42);

    // Get result from future
    std::cout << result.get() << std::endl;

    return 0;
}
