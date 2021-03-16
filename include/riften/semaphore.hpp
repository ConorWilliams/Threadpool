#pragma once

// Code in the file below is an adaptation (by C. J. Williams) of Jeff Preshing's portable + lightweight
// semaphore implementations, see:
//
// https://github.com/preshing/cpp11-on-multicore
// https://preshing.com/20150316/semaphores-are-surprisingly-versatile/

// LICENSE:
//
// Copyright (c) 2015 Jeff Preshing
//
// This software is provided 'as-is', without any express or implied
// warranty. In no event will the authors be held liable for any damages
// arising from the use of this software.
//
// Permission is granted to anyone to use this software for any purpose,
// including commercial applications, and to alter it and redistribute it
// freely, subject to the following restrictions:
//
// 1. The origin of this software must not be misrepresented; you must not
//	claim that you wrote the original software. If you use this software
//	in a product, an acknowledgement in the product documentation would be
//	appreciated but is not required.
// 2. Altered source versions must be plainly marked as such, and must not be
//	misrepresented as being the original software.
// 3. This notice may not be removed or altered from any source distribution.

#include <atomic>
#include <cassert>

namespace riften {

namespace detail {

#if defined(_WIN32)
//---------------------------------------------------------
// Semaphore (Windows)
//---------------------------------------------------------

#    include <windows.h>
#    undef min
#    undef max

class Semaphore {
  private:
    HANDLE m_hSema;

    Semaphore(const Semaphore& other) = delete;
    Semaphore& operator=(const Semaphore& other) = delete;

  public:
    Semaphore(int initialCount = 0) {
        assert(initialCount >= 0);
        m_hSema = CreateSemaphore(NULL, initialCount, MAXLONG, NULL);
    }

    ~Semaphore() { CloseHandle(m_hSema); }

    void wait() { WaitForSingleObject(m_hSema, INFINITE); }

    void signal(int count = 1) { ReleaseSemaphore(m_hSema, count, NULL); }
};

#elif defined(__MACH__)
//---------------------------------------------------------
// Semaphore (Apple iOS and OSX)
// Can't use POSIX semaphores due to http://lists.apple.com/archives/darwin-kernel/2009/Apr/msg00010.html
//---------------------------------------------------------

#    include <mach/mach.h>

class Semaphore {
  private:
    semaphore_t m_sema;

    Semaphore(const Semaphore& other) = delete;
    Semaphore& operator=(const Semaphore& other) = delete;

  public:
    Semaphore(int initialCount = 0) {
        assert(initialCount >= 0);
        semaphore_create(mach_task_self(), &m_sema, SYNC_POLICY_FIFO, initialCount);
    }

    ~Semaphore() { semaphore_destroy(mach_task_self(), m_sema); }

    void wait() { semaphore_wait(m_sema); }

    void signal() { semaphore_signal(m_sema); }

    void signal(int count) {
        while (count-- > 0) {
            semaphore_signal(m_sema);
        }
    }
};

#elif defined(__unix__)
//---------------------------------------------------------
// Semaphore (POSIX, Linux)
//---------------------------------------------------------

#    include <semaphore.h>

class Semaphore {
  private:
    sem_t m_sema;

    Semaphore(const Semaphore& other) = delete;
    Semaphore& operator=(const Semaphore& other) = delete;

  public:
    Semaphore(int initialCount = 0) {
        assert(initialCount >= 0);
        sem_init(&m_sema, 0, initialCount);
    }

    ~Semaphore() { sem_destroy(&m_sema); }

    void wait() {
        // http://stackoverflow.com/questions/2013181/gdb-causes-sem-wait-to-fail-with-eintr-error
        int rc;
        do {
            rc = sem_wait(&m_sema);
        } while (rc == -1 && errno == EINTR);
    }

    void signal() { sem_post(&m_sema); }

    void signal(int count) {
        while (count-- > 0) {
            sem_post(&m_sema);
        }
    }
};

#else

#    error Unsupported platform!

#endif

}  // namespace detail

class Semaphore {
  public:
    explicit Semaphore(std::ptrdiff_t desired) : m_count(desired) { assert(desired >= 0); }

    void release(std::ptrdiff_t update = 1) {
        std::ptrdiff_t oldCount = m_count.fetch_add(update, std::memory_order_release);
        std::ptrdiff_t toRelease = -oldCount < update ? -oldCount : update;
        if (toRelease > 0) {
            m_sema.signal(toRelease);
        }
    }

    void acquire() {
        // Is there a better way to set the initial spin count? If we lower it to 1000, testBenaphore
        // becomes 15x slower on my Core i7-5930K Windows PC, as threads start hitting the kernel
        // semaphore.

        for (std::ptrdiff_t spin = 0; spin < 10'000; ++spin) {
            std::ptrdiff_t count = m_count.load(std::memory_order_relaxed);
            if (count > 0 && m_count.compare_exchange_strong(count, count - 1, std::memory_order_acquire)) {
                return;
            }
            // Prevent the compiler from collapsing the loop.
            std::atomic_signal_fence(std::memory_order_acquire);
        }
        if (m_count.fetch_sub(1, std::memory_order_acquire) <= 0) {
            m_sema.wait();
        }
    }

    void acquire_all() {
        for (std::ptrdiff_t spin = 0; spin < 10'000; ++spin) {
            std::ptrdiff_t old = m_count.load(std::memory_order_relaxed);
            if (old > 0 && m_count.compare_exchange_strong(old, 0, std::memory_order_acquire)) {
                return;
            }
            // Prevent the compiler from collapsing the loop.
            std::atomic_signal_fence(std::memory_order_acquire);
        }
        if (m_count.fetch_sub(1, std::memory_order_acquire) <= 0) {
            m_sema.wait();
        }
        try_aquire_all();
    }

    bool try_aquire() {
        std::ptrdiff_t old = m_count.load(std::memory_order_relaxed);
        return (old > 0 && m_count.compare_exchange_strong(old, old - 1, std::memory_order_acquire));
    }

    bool try_aquire_all() {
        std::ptrdiff_t old = m_count.load(std::memory_order_relaxed);
        return (old > 0 && m_count.compare_exchange_strong(old, 0, std::memory_order_acquire));
    }

  private:
    std::atomic<std::ptrdiff_t> m_count;
    detail::Semaphore m_sema;
};

}  // namespace riften
