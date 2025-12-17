#define InfoLog
#include <cassert>
#include <iostream>

#include "MemoryPool.h"

struct Test
{
    int x;
    int y;

    Test(int a, int b) : x(a), y(b)
    {
        std::cout << "[Test][CTOR] (" << x << ", " << y << ")\n";
    }

    ~Test()
    {
        std::cout << "[Test][DTOR] (" << x << ", " << y << ")\n";
    }

    void Print() const
    {
        std::cout << "[Test] x=" << x << " y=" << y << "\n";
    }
};

int main()
{
    std::cout << "=== CREATE POOL ===\n";
    MemoryPool<Test> pool(3);

    std::cout << "\n=== LINEAR ALLOCATION ===\n";
    auto a = pool.Make(1, 2);
    auto b = pool.Make(3, 4);

    a->Print();
    b->Print();

    std::cout << "\n=== MOVE SEMANTICS ===\n";
    MemoryPoolObject<Test> c = std::move(a);
    assert(!a);
    c->Print();

    std::cout << "\n=== SCOPE EXIT (RETURN TO FREE LIST) ===\n";
    {
        auto tmp = pool.Make(5, 6);
        tmp->Print();
    } // tmp destroyed → slot returned

    std::cout << "\n=== FREE LIST REUSE ===\n";
    auto d = pool.Make(7, 8);
    d->Print();

    std::cout << "\n=== RELEASE OWNERSHIP ===\n";
    Test* raw = d.Release();
    assert(raw != nullptr);
    raw->Print();

    std::cout << "\n(manual destroy after Release)\n";
    d.Destroy();

    std::cout << "\n=== POOL DESTRUCTION ===\n";
    std::cout << "Leaving main()\n";
}
