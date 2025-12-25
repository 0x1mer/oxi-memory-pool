#include "MemOx/object_pool.hpp"

#include <cassert>
#include <stdexcept>
#include <iostream>

struct Exploding
{
    static inline int constructed = 0;
    static inline int destroyed = 0;
    static inline int explode_on = -1;

    Exploding()
    {
        int current = ++constructed;
        if (current == explode_on)
            throw std::runtime_error("constructor exploded");
    }

    ~Exploding()
    {
        ++destroyed;
    }

    static void reset(int explode_on_n)
    {
        constructed = 0;
        destroyed = 0;
        explode_on = explode_on_n;
    }
};

void test_single_throw()
{
    Exploding::reset(1);

    ObjectPool<Exploding> pool(1);

    try
    {
        auto h = pool.emplace();
        (void)h;
        assert(false && "exception expected");
    }
    catch (const std::runtime_error&) {}

    assert(pool.size() == 0);
    assert(Exploding::constructed == 1);
    assert(Exploding::destroyed == 0);
}

void test_reuse_after_throw()
{
    Exploding::reset(1);

    ObjectPool<Exploding> pool(1);

    try
    {
        auto h = pool.emplace();
        (void)h;
    }
    catch (...) {}

    Exploding::reset(-1);

    auto h = pool.emplace();
    assert(h);
    assert(pool.size() == 1);
}

void test_multiple_throws_do_not_exhaust_pool()
{
    ObjectPool<Exploding> pool(1);

    for (int i = 0; i < 5; ++i)
    {
        Exploding::reset(1);

        try
        {
            auto h = pool.emplace();
            (void)h;
        }
        catch (...) {}

        assert(pool.size() == 0);
    }

    Exploding::reset(-1);

    auto h = pool.emplace();
    assert(h);
}

void test_throw_does_not_leak_capacity()
{
    Exploding::reset(1);

    ObjectPool<Exploding> pool(2);

    try {
        auto h1 = pool.emplace();
        (void)h1;
        assert(false && "exception expected");
    }
    catch (const std::runtime_error&) {}

    assert(pool.size() == 0);

    Exploding::reset(-1);

    auto h2 = pool.emplace();
    assert(h2);
    assert(pool.size() == 1);

    auto h3 = pool.emplace();
    assert(h3);
    assert(pool.size() == 2);
}


int main()
{
    test_single_throw();
    test_reuse_after_throw();
    test_multiple_throws_do_not_exhaust_pool();
    test_throw_does_not_leak_capacity();

    std::cout << "[OK] exception_safety tests passed\n";
    return 0;
}