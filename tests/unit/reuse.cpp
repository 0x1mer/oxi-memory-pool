#include "MemOx/object_pool.hpp"
#include <cassert>
#include <iostream>

struct ReuseTracker
{
    int id = 0;
    explicit ReuseTracker(int i) : id(i) {}
};

void test_single_slot_reuse()
{
    ObjectPool<ReuseTracker> pool(1);

    auto h1 = pool.emplace(1);
    auto* addr1 = h1.get();

    h1.reset();

    assert(pool.size() == 0);

    auto h2 = pool.emplace(2);
    auto* addr2 = h2.get();

    assert(addr1 == addr2);
    assert(h2->id == 2);
}

void test_reuse_before_new_allocation()
{
    ObjectPool<ReuseTracker> pool(3);

    auto h1 = pool.emplace(1);
    auto h2 = pool.emplace(2);

    auto* addr1 = h1.get();
    auto* addr2 = h2.get();

    h1.reset();

    auto h3 = pool.emplace(3);
    auto* addr3 = h3.get();

    // must reuse freed slot, not allocate new one
    assert(addr3 == addr1);
    assert(h3->id == 3);
}

void test_lifo_free_list_order()
{
    ObjectPool<ReuseTracker> pool(3);

    auto h1 = pool.emplace(1);
    auto h2 = pool.emplace(2);
    auto h3 = pool.emplace(3);

    auto* addr1 = h1.get();
    auto* addr2 = h2.get();
    auto* addr3 = h3.get();

    h1.reset();
    h2.reset();
    h3.reset();

    auto a = pool.emplace(10);
    auto b = pool.emplace(20);
    auto c = pool.emplace(30);

    // free list is LIFO
    assert(a.get() == addr3);
    assert(b.get() == addr2);
    assert(c.get() == addr1);
}

void test_partial_reuse_and_growth()
{
    ObjectPool<ReuseTracker> pool(3);

    auto h1 = pool.emplace(1);
    auto h2 = pool.emplace(2);

    auto* addr1 = h1.get();
    auto* addr2 = h2.get();

    h1.reset();

    auto h3 = pool.emplace(3);
    auto h4 = pool.emplace(4);

    // h3 must reuse h1 slot
    assert(h3.get() == addr1);

    // h4 must allocate a new slot
    assert(h4.get() != addr1);
    assert(h4.get() != addr2);

    assert(pool.size() == 3);
}

void test_reuse_after_scope_exit()
{
    ObjectPool<ReuseTracker> pool(2);

    void* addr = nullptr;

    {
        auto h = pool.emplace(42);
        addr = h.get();
    }

    assert(pool.size() == 0);

    auto h2 = pool.emplace(99);
    assert(h2.get() == addr);
    assert(h2->id == 99);
}

int main()
{
    test_single_slot_reuse();
    test_reuse_before_new_allocation();
    test_lifo_free_list_order();
    test_partial_reuse_and_growth();
    test_reuse_after_scope_exit();

    std::cout << "[OK] reuse tests passed\n";
    return 0;
}