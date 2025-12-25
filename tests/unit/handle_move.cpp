#include "MemOx/object_pool.hpp"
#include <cassert>
#include <iostream>

struct MoveTracker
{
    static inline int ctor = 0;
    static inline int dtor = 0;

    int value = 0;

    explicit MoveTracker(int v) : value(v)
    {
        ++ctor;
    }

    ~MoveTracker()
    {
        ++dtor;
    }

    static void reset()
    {
        ctor = 0;
        dtor = 0;
    }
};

void test_move_ctor_transfers_ownership()
{
    MoveTracker::reset();

    ObjectPool<MoveTracker> pool(2);

    auto h1 = pool.emplace(42);
    auto* addr = h1.get();

    PoolHandle<MoveTracker> h2(std::move(h1));

    assert(!h1);
    assert(h2);
    assert(h2.get() == addr);
    assert(h2->value == 42);
    assert(pool.size() == 1);
    assert(MoveTracker::ctor == 1);
    assert(MoveTracker::dtor == 0);
}

void test_move_assignment_releases_previous_object()
{
    MoveTracker::reset();

    ObjectPool<MoveTracker> pool(2);

    auto h1 = pool.emplace(1);
    auto h2 = pool.emplace(2);

    assert(pool.size() == 2);

    auto* addr1 = h1.get();
    auto* addr2 = h2.get();

    h2 = std::move(h1);

    // old h2 object must be destroyed
    assert(MoveTracker::dtor == 1);
    assert(pool.size() == 1);

    assert(!h1);
    assert(h2);
    assert(h2.get() == addr1);
    assert(h2->value == 1);
}

void test_move_chain()
{
    MoveTracker::reset();

    ObjectPool<MoveTracker> pool(1);

    auto h1 = pool.emplace(7);
    auto* addr = h1.get();

    PoolHandle<MoveTracker> h2(std::move(h1));
    PoolHandle<MoveTracker> h3(std::move(h2));

    assert(!h1);
    assert(!h2);
    assert(h3);
    assert(h3.get() == addr);
    assert(pool.size() == 1);
}

void test_self_move_assignment_is_safe()
{
    MoveTracker::reset();

    ObjectPool<MoveTracker> pool(1);

    auto h = pool.emplace(99);
    auto* addr = h.get();

    // self-move must not destroy object
    h = std::move(h);

    assert(h);
    assert(h.get() == addr);
    assert(h->value == 99);
    assert(pool.size() == 1);
    assert(MoveTracker::dtor == 0);
}

void test_destruction_after_move()
{
    MoveTracker::reset();

    ObjectPool<MoveTracker> pool(1);

    {
        auto h1 = pool.emplace(5);
        PoolHandle<MoveTracker> h2(std::move(h1));

        assert(pool.size() == 1);
    } // h2 destroyed

    assert(pool.size() == 0);
    assert(MoveTracker::ctor == 1);
    assert(MoveTracker::dtor == 1);
}

int main()
{
    test_move_ctor_transfers_ownership();
    test_move_assignment_releases_previous_object();
    test_move_chain();
    test_self_move_assignment_is_safe();
    test_destruction_after_move();

    std::cout << "[OK] handle_move tests passed\n";
    return 0;
}