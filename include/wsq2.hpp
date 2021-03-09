#pragma once

#include <atomic>
#include <cassert>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

// Lock-free unbounded single-producer multiple-consumer queue.  This class implements the work
// stealing queue described in the paper, "Correct and Efficient Work-Stealing for Weak Memory
// Models," available at https://www.di.ens.fr/~zappa/readings/ppopp13.pdf. Only the queue owner can
// perform pop and push operations, while others can steal data from the queue.
template <typename T> class ThiefQueue {
  private:
    struct Array {
      public:
        explicit Array(std::int64_t capacity)
            : _capacity{capacity}, _mask{capacity - 1}, _data{new std::atomic<T>[capacity]} {
            // Check is power of 2
            assert(capacity && (!(capacity & (capacity - 1))));
        }

        int64_t capacity() const noexcept { return _capacity; }

        void push(std::int64_t i, T* x) noexcept {
            _data[i & _mask].store(x, std::memory_order_relaxed);
        }

        T* pop(std::int64_t i) noexcept { return _data[i & _mask].load(std::memory_order_relaxed); }

        // Allocates a new array, copies elements in range [b, t) into array, returns a pointer to
        // the new array.
        Array* resize(std::int64_t b, std::int64_t t) {
            Array* ptr = new Array{2 * _capacity};
            for (std::int64_t i = t; i != b; ++i) {
                ptr->push(i, pop(i));
            }
            return ptr;
        }

        // Does not delete items in the Array
        ~Array() { delete[] _data; }

      private:
        std::int64_t _capacity;
        std::int64_t _mask;
        std::atomic<T*>* _data;
    };

  public:
    // Constructs the queue with a given capacity the capacity of the queue (must be power of 2)
    explicit ThiefQueue(std::int64_t capacity = 1024);

    ~ThiefQueue();

    // Emplace an item to the queue Only the owner thread can insert an item to the queue. The
    // operation can trigger the queue to resize its capacity if more space is required.
    template <typename... Args> void emplace(Args&&... args);

    // Pops out an item from the queue Only the owner thread can pop out an item from the queue. The
    // return can be a std::nullopt if this operation failed (empty queue).
    std::optional<T> pop();

    // Steals an item from the queue Any threads can try to steal an item from the queue. The return
    // can be a @std_nullopt if this operation failed (not necessary empty).
    std::optional<T> steal();

  private:
    std::atomic<std::int64_t> _top;
    std::atomic<std::int64_t> _bottom;

    std::atomic<Array*> _array;
    std::vector<Array*> _garbage;
};

template <typename T> ThiefQueue<T>::ThiefQueue(std::int64_t capacity) {
    _top.store(0, std::memory_order_relaxed);
    _bottom.store(0, std::memory_order_relaxed);
    _array.store(new Array{capacity}, std::memory_order_relaxed);
    _garbage.reserve(32);
}

template <typename T> ThiefQueue<T>::~ThiefQueue() {
    for (auto a : _garbage) {
        delete a;
    }
    delete _array.load();
}

template <typename T> template <typename... Args> void ThiefQueue<T>::emplace(Args&&... args) {
    // Construct new object
    T* x = new T{std::forward<Args>(args)...};

    std::int64_t b = _bottom.load(std::memory_order_relaxed);
    std::int64_t t = _top.load(std::memory_order_acquire);
    Array* a = _array.load(std::memory_order_relaxed);

    if (a->capacity() - 1 < (b - t)) {
        // Queue is full, build a new one
        _garbage.push_back(std::exchange(a, a->resize(b, t)));
        _array.store(a, std::memory_order_relaxed);
    }

    a->push(b, x);

    std::atomic_thread_fence(std::memory_order_release);
    _bottom.store(b + 1, std::memory_order_relaxed);
}

template <typename T> std::optional<T> ThiefQueue<T>::pop() {
    std::int64_t b = _bottom.load(std::memory_order_relaxed) - 1;
    Array* a = _array.load(std::memory_order_relaxed);
    _bottom.store(b, std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_seq_cst);
    std::int64_t t = _top.load(std::memory_order_relaxed);

    T* x = nullptr;

    if (t <= b) {
        // Non-empty queue
        x = a->pop(b);
        if (t == b) {
            // The last item just got stolen
            if (!_top.compare_exchange_strong(
                    t, t + 1, std::memory_order_seq_cst, std::memory_order_relaxed)) {
                // Failed race.
                x = nullptr;
            }
            _bottom.store(b + 1, std::memory_order_relaxed);
        }
    } else {
        // Empty queue
        _bottom.store(b + 1, std::memory_order_relaxed);
    }

    if (x) {
        std::optional<T> tmp = std::move(*x);
        delete x;
        return tmp;
    } else {
        return std::nullopt;
    }
}

// Function: steal
template <typename T> std::optional<T> ThiefQueue<T>::steal() {
    std::int64_t t = _top.load(std::memory_order_acquire);
    std::atomic_thread_fence(std::memory_order_seq_cst);
    std::int64_t b = _bottom.load(std::memory_order_acquire);

    T* x = nullptr;

    if (t < b) {
        // Non-empty queue.
        Array* a = _array.load(std::memory_order_consume);
        x = a->pop(t);
        if (!_top.compare_exchange_strong(
                t, t + 1, std::memory_order_seq_cst, std::memory_order_relaxed)) {
            return std::nullopt;
        }
    }

    if (x) {
        std::optional<T> tmp = std::move(*x);
        delete x;
        return tmp;
    } else {
        return std::nullopt;
    }
}
