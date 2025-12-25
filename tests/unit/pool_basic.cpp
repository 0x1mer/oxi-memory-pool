#include "MemOx/object_pool.hpp"
#include <cassert>

struct Dummy {
    int value;
    Dummy(int v) : value(v) {}
};

void test_basic_emplace() {
    ObjectPool<Dummy> pool(4);

    auto h = pool.emplace(42);

    assert(h);
    assert(pool.size() == 1);
    assert(h->value == 42);
}

void test_raii_destroy() {
    ObjectPool<Dummy> pool(2);

    {
        auto h = pool.emplace(1);
        assert(pool.size() == 1);
    }

    assert(pool.size() == 0);
}

void test_slot_reuse() {
    ObjectPool<Dummy> pool(1);

    auto h1 = pool.emplace(1);
    auto addr1 = h1.get();

    h1.reset();

    auto h2 = pool.emplace(2);
    auto addr2 = h2.get();

    assert(addr1 == addr2);
    assert(h2->value == 2);
}

void test_exhaustion() {
    ObjectPool<Dummy> pool(1);

    auto h1 = pool.emplace(1);
    assert(h1);

    bool thrown = false;
    try {
        auto h2 = pool.emplace(2);
    }
    catch (const std::runtime_error&) {
        thrown = true;
    }

    assert(thrown);
    assert(pool.size() == 1);
}

void test_handle_move() {
    ObjectPool<Dummy> pool(1);

    auto h1 = pool.emplace(10);
    auto addr = h1.get();

    PoolHandle<Dummy> h2 = std::move(h1);

    assert(!h1);
    assert(h2);
    assert(h2.get() == addr);
    assert(pool.size() == 1);
}

void test_handle_reset() {
    ObjectPool<Dummy> pool(1);

    auto h = pool.emplace(5);
    assert(pool.size() == 1);

    h.reset();

    assert(!h);
    assert(pool.size() == 0);
}

int main() {
    test_basic_emplace();
    test_raii_destroy();
    test_slot_reuse();
    test_exhaustion();
    test_handle_move();
    test_handle_reset();
    return 0;
}