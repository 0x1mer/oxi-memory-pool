#define OxiMemPool_ThreadSafe
#include "MemOx/object_pool.hpp"

#include <cassert>
#include <thread>
#include <vector>
#include <atomic>
#include <iostream>

struct ThreadObject
{
    int value;
    explicit ThreadObject(int v) : value(v) {}
};

void test_parallel_emplace_and_destroy()
{
    constexpr int kThreads = 8;
    constexpr int kIterations = 10'000;
    constexpr int kCapacity = 64;

    ObjectPool<ThreadObject> pool(kCapacity);

    std::atomic<bool> start{false};
    std::vector<std::thread> threads;

    for (int t = 0; t < kThreads; ++t)
    {
        threads.emplace_back([&, t] {
            while (!start.load(std::memory_order_acquire)) {}

            for (int i = 0; i < kIterations; ++i)
            {
                auto h = pool.emplace(t * 100000 + i);
                if (h)
                {
                    assert(h->value == t * 100000 + i);
                }
            }
        });
    }

    start.store(true, std::memory_order_release);

    for (auto& th : threads)
        th.join();

    // All handles destroyed → pool must be empty
    assert(pool.size() == 0);
}

void test_parallel_reuse_pressure()
{
    constexpr int kThreads = 6;
    constexpr int kCapacity = 8;
    constexpr int kIterations = 20'000;

    ObjectPool<ThreadObject> pool(kCapacity);

    std::atomic<bool> start{false};
    std::vector<std::thread> threads;

    for (int t = 0; t < kThreads; ++t)
    {
        threads.emplace_back([&, t] {
            while (!start.load(std::memory_order_acquire)) {}

            for (int i = 0; i < kIterations; ++i)
            {
                auto h = pool.emplace(t);
                if (!h)
                    continue;

                // small workload
                h->value += 1;
            }
        });
    }

    start.store(true, std::memory_order_release);

    for (auto& th : threads)
        th.join();

    assert(pool.size() == 0);
}

void test_capacity_never_exceeded()
{
    constexpr int kThreads = 4;
    constexpr int kCapacity = 4;
    constexpr int kIterations = 50'000;

    ObjectPool<ThreadObject> pool(kCapacity);

    std::atomic<bool> start{false};
    std::vector<std::thread> threads;

    for (int t = 0; t < kThreads; ++t)
    {
        threads.emplace_back([&] {
            while (!start.load(std::memory_order_acquire)) {}

            for (int i = 0; i < kIterations; ++i)
            {
                auto h = pool.emplace(i);
                if (h)
                {
                    size_t sz = pool.size();
                    assert(sz <= kCapacity);
                }
            }
        });
    }

    start.store(true, std::memory_order_release);

    for (auto& th : threads)
        th.join();

    assert(pool.size() == 0);
}

int main()
{
    test_parallel_emplace_and_destroy();
    test_parallel_reuse_pressure();
    test_capacity_never_exceeded();

    std::cout << "[OK] thread_safety tests passed\n";
    return 0;
}
