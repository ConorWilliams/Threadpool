# `riften::Thiefpool`

A Light, fast, work-stealing thread-pool for C++20. Built on the [`riften::Thiefpool`](https://github.com/ConorWilliams/ConcurrentDeque).

## Usage

```C++

#include "riften/thiefpool.hpp"

// Create thread pool with 4 worker threads.
riften::Thiefpool pool(4);

// Enqueue and store future.
auto result = pool.enqueue([](int x) { return x; }, 42);

// Get result from future.
std::cout << result.get() << std::endl;

```

## Installation
