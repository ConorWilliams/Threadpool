

#include <chrono>
#include <future>
#include <iostream>
#include <ratio>
#include <thread>

#include "singlequeue.hpp"

int main() {
    ThreadPool pool(12);

    std::vector<std::future<int>> _futures;

    for (size_t i = 0; i < 10; i++) {
        pool.execute([time = 100]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(time));
            std::cout << "Waited :" << time << std::endl;
            return 3;
        });
    }

    std::cout << "Working\n";
    return 0;
}