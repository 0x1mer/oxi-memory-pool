// tests/unit/thread_stress.cpp
#include "MemOx/object_pool.hpp"

#include <atomic>
#include <cassert>
#include <chrono>
#include <iostream>
#include <random>
#include <thread>
#include <vector>

struct StressItem
{
    static std::atomic<int> constructed;
    static std::atomic<int> destroyed;

    StressItem() { constructed.fetch_add(1, std::memory_order_relaxed); }
    ~StressItem() { destroyed.fetch_add(1, std::memory_order_relaxed); }

    static void reset_counts() {
        constructed.store(0, std::memory_order_relaxed);
        destroyed.store(0, std::memory_order_relaxed);
    }
};

std::atomic<int> StressItem::constructed{0};
std::atomic<int> StressItem::destroyed{0};

void test_thread_stress()
{
    using namespace std::chrono_literals;

    StressItem::reset_counts();

    // Настройки — можно подправить:
    const size_t pool_capacity = 64;      // небольшой пул для усиления конкуренции
    const auto test_duration = 3s;        // время гонки
    const int max_local_handles = 64;     // сколько handle'ов локально держать в потоке

    ObjectPool<StressItem> pool(pool_capacity);

    const unsigned hw = std::max(2u, std::thread::hardware_concurrency());
    const int num_threads = static_cast<int>(hw);

    std::atomic<bool> stop{false};
    std::vector<std::thread> threads;
    threads.reserve(num_threads);

    for (int t = 0; t < num_threads; ++t)
    {
        threads.emplace_back([t, &pool, &stop, max_local_handles]() {
            // Каждый поток имеет свой RNG
            std::mt19937_64 rng(static_cast<uint64_t>(std::chrono::high_resolution_clock::now().time_since_epoch().count()) + (uint64_t)t);
            std::uniform_int_distribution<int> coin(0, 99);

            std::vector<PoolHandle<StressItem>> local;
            local.reserve(32);

            while (!stop.load(std::memory_order_relaxed))
            {
                // Попытка выделить; emplace может бросить при исчерпании
                try
                {
                    auto h = pool.emplace();
                    if (h) {
                        // иногда держим handle локально, иногда отпускаем сразу
                        if (coin(rng) < 30) {
                            local.push_back(std::move(h));
                            if (local.size() > static_cast<size_t>(max_local_handles)) {
                                // освободить случайный элемент (чтобы создавать разные паттерны)
                                size_t idx = rng() % local.size();
                                // переместим последний на место idx и pop_back
                                if (idx + 1 != local.size()) local[idx] = std::move(local.back());
                                local.pop_back();
                            }
                        }
                        // иначе h выйдет из области видимости и освободится
                    }
                }
                catch (...) {
                    // Игнорируем исключения от исчерпания (тест проверяет стабильность)
                }

                // иногда очищаем несколько локальных хендлов
                if (coin(rng) < 5 && !local.empty()) {
                    local.pop_back();
                }
            } // while

            // при выходе из потока локальный vector уничтожится и освободит оставшиеся handles
        });
    }

    // запускаем гонку
    std::this_thread::sleep_for(test_duration);
    stop.store(true, std::memory_order_relaxed);

    for (auto &th : threads) th.join();

    // После join все локальные handles уничтожены -> все объекты должны быть разрушены
    const int constructed = StressItem::constructed.load(std::memory_order_relaxed);
    const int destroyed = StressItem::destroyed.load(std::memory_order_relaxed);

    // Дополнительная проверка: размер пула должен вернуться к 0
    const size_t size = pool.size();

    if (constructed != destroyed) {
        std::cerr << "[ThreadStress] constructed=" << constructed
                  << " destroyed=" << destroyed << " pool.size=" << size << "\n";
    }

    assert(constructed == destroyed && "constructed != destroyed -> leak or double-destruction");
    assert(size == 0 && "pool.size() must be 0 after all handles destroyed");

    std::cout << "[ThreadStress] OK: threads=" << num_threads
              << " constructed=" << constructed << " destroyed=" << destroyed << "\n";
}

int main()
{
    test_thread_stress();
    return 0;
}