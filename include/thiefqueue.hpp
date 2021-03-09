#pragma once

#include <atomic>
#include <cassert>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

// Lock-free single-producer multiple-consumer queue. Only the queue owner can perform pop and push
// operations, while others can steal data from the queue. All threads must have finished using the Deque
// before it is destructed. This class implements the queue described in the paper, "Correct and Efficient
// Work-Stealing for Weak Memory Models," available at https://www.di.ens.fr/~zappa/readings/ppopp13.pdf.
template <typename T> class ThiefDeque {
    // Convinience aliases
    static constexpr std::memory_order relaxed = std::memory_order::relaxed;
    static constexpr std::memory_order consume = std::memory_order::consume;
    static constexpr std::memory_order acquire = std::memory_order::acquire;
    static constexpr std::memory_order release = std::memory_order::release;
    static constexpr std::memory_order seq_cst = std::memory_order::seq_cst;

    struct Array {
      public:
        explicit Array(std::int64_t cap) : _cap{cap}, _mask{cap - 1}, _data{new std::atomic<T*>[cap]} {}

        int64_t capacity() const noexcept { return _cap; }

        void push(std::int64_t i, T* x) noexcept { _data[i & _mask].store(x, relaxed); }

        T* pop(std::int64_t i) noexcept { return _data[i & _mask].load(relaxed); }

        // Allocates a new array, copies elements in range [b, t) into array, returns a pointer to
        // the new array.
        Array* resize(std::int64_t b, std::int64_t t) {
            Array* ptr = new Array{2 * _cap};
            for (std::int64_t i = t; i != b; ++i) {
                ptr->push(i, pop(i));
            }
            return ptr;
        }

        // Does not delete items in the Array
        ~Array() { delete[] _data; }

      private:
        std::int64_t _cap;
        std::int64_t _mask;
        std::atomic<T*>* _data;
    };

  public:
    // Constructs the queue with a given cap the cap of the queue (must be power of 2)
    explicit ThiefDeque(std::int64_t cap = 1024);

    // Destruct the queue, all threads must have finished using the queue.
    ~ThiefDeque();

    // Emplace an item to the queue Only the owner thread can insert an item to the queue. The operation can
    // trigger the queue to resize its cap if more space is required.
    template <typename... Args> void emplace(Args&&... args);

    // Pops out an item from the queue Only the owner thread can pop out an item from the queue. The return
    // can be a std::nullopt if this operation failed (empty queue).
    std::optional<T> pop();

    // Steals an item from the queue Any threads can try to steal an item from the queue. The return can be a
    // std::nullopt if this operation failed (not necessary empty).
    std::optional<T> steal();

  private:
    std::atomic<std::int64_t> _top;
    std::atomic<std::int64_t> _bottom;

    std::atomic<Array*> _array;
    std::vector<Array*> _garbage;
};

template <typename T> ThiefDeque<T>::ThiefDeque(std::int64_t cap) {
    assert(cap && (!(cap & (cap - 1))));
    _top.store(0, relaxed);
    _bottom.store(0, relaxed);
    _array.store(new Array{cap}, relaxed);
    _garbage.reserve(32);
}

template <typename T> ThiefDeque<T>::~ThiefDeque() {
    for (auto a : _garbage) {
        delete a;
    }
    // Cleans up all remaining items in queue.
    while (pop()) {
    }
    delete _array.load();
}

template <typename T> template <typename... Args> void ThiefDeque<T>::emplace(Args&&... args) {
    // Construct new object
    T* x = new T(std::forward<Args>(args)...);

    std::int64_t b = _bottom.load(relaxed);
    std::int64_t t = _top.load(acquire);
    Array* a = _array.load(relaxed);

    if (a->capacity() - 1 < (b - t)) {
        // Queue is full, build a new one
        _garbage.push_back(std::exchange(a, a->resize(b, t)));
        _array.store(a, relaxed);
    }

    a->push(b, x);

    std::atomic_thread_fence(release);
    _bottom.store(b + 1, relaxed);
}

template <typename T> std::optional<T> ThiefDeque<T>::pop() {
    std::int64_t b = _bottom.load(relaxed) - 1;
    Array* a = _array.load(relaxed);
    _bottom.store(b, relaxed);
    std::atomic_thread_fence(seq_cst);
    std::int64_t t = _top.load(relaxed);

    if (t <= b) {
        // Non-empty queue
        T* x = a->pop(b);

        if (t == b) {
            // The last item just got stolen
            if (!_top.compare_exchange_strong(t, t + 1, seq_cst, relaxed)) {
                // Failed race.
                _bottom.store(b + 1, relaxed);
                return std::nullopt;
            }
        }
        std::optional<T> tmp = std::move(*x);
        delete x;
        return tmp;
    } else {
        _bottom.store(b + 1, relaxed);
        return std::nullopt;
    }
}

template <typename T> std::optional<T> ThiefDeque<T>::steal() {
    std::int64_t t = _top.load(acquire);
    std::atomic_thread_fence(seq_cst);
    std::int64_t b = _bottom.load(acquire);

    if (t < b) {
        // Non-empty queue.
        T* x = _array.load(consume)->pop(t);

        if (!_top.compare_exchange_strong(t, t + 1, seq_cst, relaxed)) {
            // Failed race.
            return std::nullopt;
        }
        std::optional<T> tmp = std::move(*x);
        delete x;
        return tmp;
    } else {
        return std::nullopt;
    }
}
