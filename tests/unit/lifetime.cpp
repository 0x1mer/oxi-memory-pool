#define OxiMemPool_ErrCallback
#include "MemOx/object_pool.hpp"

#include <cassert>
#include <iostream>

struct LifetimeTracker
{
    static inline int ctor_count = 0;
    static inline int dtor_count = 0;

    LifetimeTracker()
    {
        ++ctor_count;
    }

    ~LifetimeTracker()
    {
        ++dtor_count;
    }

    static void reset()
    {
        ctor_count = 0;
        dtor_count = 0;
    }
};

void test_destructor_called_on_reset()
{
    LifetimeTracker::reset();

    ObjectPool<LifetimeTracker> pool(4);

    {
        auto h = pool.emplace();
        assert(pool.size() == 1);
        assert(LifetimeTracker::ctor_count == 1);
        assert(LifetimeTracker::dtor_count == 0);

        h.reset();

        assert(pool.size() == 0);
        assert(LifetimeTracker::dtor_count == 1);
    }

    // handle already reset — destructor must NOT run again
    assert(LifetimeTracker::dtor_count == 1);
}

void test_destructor_called_on_handle_scope_exit()
{
    LifetimeTracker::reset();

    ObjectPool<LifetimeTracker> pool(4);

    {
        auto h = pool.emplace();
        assert(pool.size() == 1);
    } // ~PoolHandle

    assert(pool.size() == 0);
    assert(LifetimeTracker::ctor_count == 1);
    assert(LifetimeTracker::dtor_count == 1);
}

void test_multiple_objects_lifetime()
{
    LifetimeTracker::reset();

    ObjectPool<LifetimeTracker> pool(8);

    {
        auto h1 = pool.emplace();
        auto h2 = pool.emplace();
        auto h3 = pool.emplace();

        assert(pool.size() == 3);
        assert(LifetimeTracker::ctor_count == 3);
        assert(LifetimeTracker::dtor_count == 0);

        h2.reset();

        assert(pool.size() == 2);
        assert(LifetimeTracker::dtor_count == 1);
    }

    // h1, h3 destroyed here
    assert(pool.size() == 0);
    assert(LifetimeTracker::dtor_count == 3);
}

void test_pool_can_be_destroyed_after_all_handles()
{
    LifetimeTracker::reset();

    {
        ObjectPool<LifetimeTracker> pool(2);
        auto h1 = pool.emplace();
        auto h2 = pool.emplace();

        assert(pool.size() == 2);
    } // pool destroyed AFTER handles

    assert(LifetimeTracker::ctor_count == 2);
    assert(LifetimeTracker::dtor_count == 2);
}

int main()
{
    test_destructor_called_on_reset();
    test_destructor_called_on_handle_scope_exit();
    test_multiple_objects_lifetime();
    test_pool_can_be_destroyed_after_all_handles();

    std::cout << "[OK] lifetime tests passed\n";
    return 0;
}