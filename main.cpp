#include <iostream>
#include <vector>
#include <thread>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <chrono>
#include <algorithm>
#include <random>

#define ThreadSafe
#include "MemoryPool.h"

// --------------------- Вспомогательные типы для тестов ---------------------

struct Trackable {
    static std::atomic<int> alive;
    int value = 0;

    Trackable() : value(0) { ++alive; }
    explicit Trackable(int v) : value(v) { ++alive; }
    ~Trackable() { --alive; }

    Trackable(const Trackable &o) : value(o.value) { ++alive; }
    Trackable(Trackable &&o) noexcept : value(o.value) { ++alive; o.value = 0; }
    Trackable &operator=(const Trackable &o) { value = o.value; return *this; }
    Trackable &operator=(Trackable &&o) noexcept { value = o.value; o.value = 0; return *this; }
};
std::atomic<int> Trackable::alive{0};

struct ThrowOnValue {
    // бросает, если ctor аргумент == magic
    static constexpr int magic = 42;
    int v;
    static std::atomic<int> constructions;

    ThrowOnValue(int x) {
        if (x == magic) throw std::runtime_error("ctor: boom");
        v = x;
        ++constructions;
    }
    ~ThrowOnValue() { --constructions; }
};
std::atomic<int> ThrowOnValue::constructions{0};

// --------------------- Утилиты assert/print ---------------------
#define TEST_ASSERT(expr) do { if (!(expr)) { \
    std::cerr << "ASSERT FAILED: " #expr << " at " << __FILE__ << ":" << __LINE__ << "\n"; \
    std::terminate(); } } while(0)

void info(const char *s) { std::cout << "[TEST] " << s << "\n"; }

// --------------------- Тесты ---------------------

// Проверка базовых allocate/free, Used/Available/MaxAllocated/Capacity
void test_basic_alloc_dealloc() {
    info("test_basic_alloc_dealloc");
    MemoryPool<Trackable> pool(4);
    TEST_ASSERT(pool.Capacity() == 4);
    TEST_ASSERT(pool.Used() == 0);

    auto a = pool.Make(1);
    TEST_ASSERT(a);
    TEST_ASSERT(pool.Used() == 1);
    TEST_ASSERT(pool.Available() + pool.Used() == pool.Capacity());

    {
        auto b = pool.Make(2);
        auto c = pool.Make(3);
        TEST_ASSERT(pool.Used() == 3);
        TEST_ASSERT(Trackable::alive.load() == 3);
        // проверяем, что объекты корректно доступны
        TEST_ASSERT(a->value == 1);
        TEST_ASSERT(b->value == 2);
        TEST_ASSERT(c->value == 3);
    } // b и c должны быть разрушены здесь

    // a всё ещё live
    TEST_ASSERT(pool.Used() == 1);
    TEST_ASSERT(Trackable::alive.load() == 1);

    a.Reset(); // вернуть слот
    TEST_ASSERT(pool.Used() == 0);
    TEST_ASSERT(Trackable::alive.load() == 0);
}

// Проверка переиспользования freed слота
void test_reuse_free_slot() {
    info("test_reuse_free_slot");
    MemoryPool<Trackable> pool(3);
    auto p1 = pool.Make(10);
    auto p2 = pool.Make(20);

    void *addr1 = static_cast<void *>(p1.Get());
    void *addr2 = static_cast<void *>(p2.Get());
    TEST_ASSERT(addr1 != addr2);

    p1.Reset(); // освободили первый
    TEST_ASSERT(pool.Used() == 1);

    auto p3 = pool.Make(30);
    void *addr3 = static_cast<void *>(p3.Get());

    // ожидание: p3 должен переиспользовать freed slot p1 (LIFO free-list)
    TEST_ASSERT(addr3 == addr1);
    p2.Reset();
    p3.Reset();
    TEST_ASSERT(pool.Used() == 0);
}

// Проверка выравнивания адресов
void test_alignment() {
    info("test_alignment");
    MemoryPool<Trackable> pool(8);
    std::vector<void*> addrs;
    for (int i=0;i<5;i++){
        auto o = pool.Make(i);
        addrs.push_back(o.Get());
    }
    for (void* a : addrs) {
        uintptr_t up = reinterpret_cast<uintptr_t>(a);
        TEST_ASSERT((up % alignof(Trackable)) == 0);
    }
    // освобождаем всё
    // memory pool object RAII освободит при выходе scope
}

// Проверка move семантики MemoryPoolObject
void test_move_semantics() {
    info("test_move_semantics");
    MemoryPool<Trackable> pool(2);
    auto a = pool.Make(100);
    TEST_ASSERT(a);
    TEST_ASSERT(static_cast<bool>(a));
    void *addr = a.Get();

    // move ctor
    MemoryPoolObject<Trackable> moved(std::move(a));
    TEST_ASSERT(!static_cast<bool>(a));
    TEST_ASSERT(static_cast<bool>(moved));
    TEST_ASSERT(moved.Get() == addr);

    // move assignment
    auto b = pool.Make(200);
    TEST_ASSERT(b);
    moved = std::move(b);
    TEST_ASSERT(!static_cast<bool>(b));
    TEST_ASSERT(moved->value == 200);
    moved.Reset();
    TEST_ASSERT(!static_cast<bool>(moved));
    TEST_ASSERT(pool.Used() == 0);
}

// Проверка корректного вызова деструкторов
void test_destructor_called() {
    info("test_destructor_called");
    Trackable::alive = 0;
    {
        MemoryPool<Trackable> pool(5);
        {
            auto a = pool.Make(1);
            auto b = pool.Make(2);
            TEST_ASSERT(Trackable::alive.load() == 2);
            // выход из scope для a,b произойдёт до разрушения pool
        }
        TEST_ASSERT(Trackable::alive.load() == 0);
    }
    TEST_ASSERT(Trackable::alive.load() == 0);
}

// Проверка поведения при исключении конструктора — важный тест на exception-safety
void test_exception_safety() {
    info("test_exception_safety");
    MemoryPool<ThrowOnValue> pool(3);

    // успешная конструкция
    {
        auto t = pool.Make(1);
        TEST_ASSERT(t);
        TEST_ASSERT(ThrowOnValue::constructions.load() == 1);
        t.Reset();
        TEST_ASSERT(ThrowOnValue::constructions.load() == 0);
    }

    // попытка создать объект, конструктор бросает
    size_t used_before = pool.Used();
    size_t max_before = pool.MaxAllocated();
    try {
        auto bad = pool.Make(ThrowOnValue::magic); // должен бросить
        (void)bad;
        TEST_ASSERT(false && "Make should have thrown");
    } catch (const std::runtime_error &e) {
        // ожидаем, что количество живых объектов не изменилось
        TEST_ASSERT(pool.Used() == used_before);
        TEST_ASSERT(ThrowOnValue::constructions.load() == 0);
        // Поведение m_max_allocated: реализация увеличивает max_allocated при линейном выделении
        // и при падении конструктора возвращает слот в free-list (но max_allocated не уменьшается).
        // Проверим, что пул не "утёк" живыми объектами и слот доступен для повторного использования.
        auto ok = pool.Make(999);
        TEST_ASSERT(ok);
        ok.Reset();
    }
}

// Многопоточный стресс-тест — выполняется только если пул собран с ThreadSafe
void test_multithreaded_stress() {
#ifdef ThreadSafe
    info("test_multithreaded_stress");
    constexpr int THREADS = 8;
    constexpr int OPS = 3000;
    MemoryPool<Trackable> pool(THREADS * 10);

    std::atomic<int> counter{0};
    auto worker = [&](int tid){
        std::mt19937 rng(tid + 123);
        std::uniform_int_distribution<int> dist(1, 10000);
        for (int i=0;i<OPS;i++){
            int v = dist(rng);
            auto o = pool.Make(v);
            TEST_ASSERT(o);
            ++counter;
            // случайно переместим o или сбросим
            if ((v & 1) == 0) {
                auto tmp = std::move(o);
                tmp.Reset();
            } else {
                o.Reset();
            }
        }
    };

    std::vector<std::thread> threads;
    for (int t=0;t<THREADS;t++) threads.emplace_back(worker, t);
    for (auto &th : threads) th.join();

    // после всех операций никого не должно быть live
    TEST_ASSERT(pool.Used() == 0);
    TEST_ASSERT(Trackable::alive.load() == 0);
    info("multithread stress finished");
#else
    info("test_multithreaded_stress skipped (compile with -DThreadSafe to enable)");
#endif
}

int main() {
    std::cout << "=== MemoryPool tests start ===\n";

    test_basic_alloc_dealloc();
    test_reuse_free_slot();
    test_alignment();
    test_move_semantics();
    test_destructor_called();
    test_exception_safety();
    test_multithreaded_stress();

    std::cout << "=== All tests PASSED ===\n";
    return 0;
}