#define OxiMemPool_ErrCallback
#include "MemOx/object_pool.hpp"

#include <cassert>
#include <cstring>

// ---------- Test state ----------
static bool callback_called = false;
static const char* last_msg = nullptr;
static size_t last_code = 0;

// ---------- Callback ----------
void error_callback(const char* msg, size_t code)
{
    callback_called = true;
    last_msg = msg;
    last_code = code;
}

// ---------- Test ----------
void test_error_callback_called()
{
    callback_called = false;
    last_msg = nullptr;
    last_code = 0;

    ObjectPool<int> pool(1);
    pool.set_error_callback(error_callback);

    auto h1 = pool.emplace(1);
    assert(h1);

    auto h2 = pool.emplace(2);

    assert(!h2);
    assert(callback_called);
    assert(last_msg != nullptr);
    assert(std::strcmp(last_msg, "ObjectPool exhausted") == 0);
    assert(last_code == 1);
    assert(pool.size() == 1);
}

// ---------- main ----------
int main()
{
    test_error_callback_called();
    return 0;
}