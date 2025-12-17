#include <iostream>
#include <vector>
#include <thread>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <chrono>
#include <algorithm>
#include <random>
#include <mutex>
#include <string>

// Включаем ThreadSafe и ErrCallback для тестирования соответствующих веток
#define OxiMemPool_ThreadSafe
#define OxiMemPool_ErrCallback
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

struct alignas(64) BigAligned {
    char data[64];
    BigAligned() { data[0] = 0; }
};

// --------------------- Утилиты assert/print ---------------------
#define TEST_ASSERT(expr) do { if (!(expr)) { \
    std::cerr << "ASSERT FAILED: " #expr << " at " << __FILE__ << ":" << __LINE__ << "\n"; \
    std::terminate(); } } while(0)

void info(const char *s) { std::cout << "[TEST] " << s << "\n"; }

// --------------------- Error callback capture ---------------------

static std::atomic<int> g_err_count{0};
static std::atomic<size_t> g_last_err_code{0};
static std::string g_last_err_msg;
static std::mutex g_err_mutex;

void TestErrorCallback(const char* msg, size_t code) {
    ++g_err_count;
    g_last_err_code.store(code);
    std::lock_guard<std::mutex> lk(g_err_mutex);
    g_last_err_msg = msg ? msg : "";
}

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

// Проверка выравнивания адресов на Trackable и BigAligned
void test_alignment() {
    info("test_alignment");
    {
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
    }
    {
        MemoryPool<BigAligned> pool(4);
        std::vector<void*> addrs;
        for (int i=0;i<3;i++) addrs.push_back(pool.Make().Get());
        for (void* a : addrs) {
            uintptr_t up = reinterpret_cast<uintptr_t>(a);
            TEST_ASSERT((up % alignof(BigAligned)) == 0);
        }
    }
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

// Проверка поведения при исключении конструктора — important
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
    try {
        auto bad = pool.Make(ThrowOnValue::magic); // должен бросить
        (void)bad;
        TEST_ASSERT(false && "Make should have thrown");
    } catch (const std::runtime_error &e) {
        // ожидаем, что количество живых объектов не изменилось
        TEST_ASSERT(pool.Used() == used_before);
        TEST_ASSERT(ThrowOnValue::constructions.load() == 0);

        // слот должен быть доступен для повторного использования
        auto ok = pool.Make(999);
        TEST_ASSERT(ok);
        ok.Reset();
    }
}

// Проверка MaxAllocated "монотонности" и повторного использования
void test_max_allocated_behavior() {
    info("test_max_allocated_behavior");
    MemoryPool<Trackable> pool(3);
    TEST_ASSERT(pool.MaxAllocated() == 0);

    auto a = pool.Make(1); // max_allocated -> 1
    auto b = pool.Make(2); // -> 2
    TEST_ASSERT(pool.MaxAllocated() >= 2);

    a.Reset(); // free first
    TEST_ASSERT(pool.Used() == 1);

    auto c = pool.Make(3); // should reuse a's slot, MaxAllocated should not decrease
    TEST_ASSERT(pool.MaxAllocated() >= 2);
    TEST_ASSERT(pool.Used() == 2);

    // allocate until capacity
    auto d = pool.Make(4);
    TEST_ASSERT(pool.Used() <= pool.Capacity());
    TEST_ASSERT(pool.MaxAllocated() <= pool.Capacity());
    d.Reset(); c.Reset(); b.Reset();
    TEST_ASSERT(pool.Used() == 0);
}

// Тестируем error callback: переполнение пула
void test_error_callback_exhaustion() {
    info("test_error_callback_exhaustion");
    g_err_count.store(0);
    {
        MemoryPool<Trackable> pool(2, &TestErrorCallback);
        auto a = pool.Make(1);
        auto b = pool.Make(2);
        TEST_ASSERT(a && b);
        auto c = pool.Make(3); // переполнение -> должен вызвать callback и вернуть empty wrapper
        TEST_ASSERT(!c); // объект не создан
        TEST_ASSERT(g_err_count.load() >= 1);
        TEST_ASSERT(g_last_err_code.load() != 0);
        TEST_ASSERT(pool.Used() == 2);
        a.Reset(); b.Reset();
    }
    // callback count at least 1 during test
    TEST_ASSERT(g_err_count.load() >= 1);
}

// Тестируем error callback в конструкторе пула с count==0 (конструктор вызывает ReportError)
void test_error_callback_constructor_zero() {
    info("test_error_callback_constructor_zero");
    g_err_count.store(0);
    // Конструктор с count==0 вызывает ReportError — наш callback должен быть вызван
    MemoryPool<Trackable> *poolPtr = nullptr;
    try {
        poolPtr = new MemoryPool<Trackable>(0, &TestErrorCallback);
    } catch (...) {
        // если реализация выбрасывает вместо вызова callback - это допустимо, поймаем
    }
    TEST_ASSERT(g_err_count.load() >= 1 || poolPtr == nullptr);
    delete poolPtr; // если nullptr - delete безопасен
}

// Многопоточный стресс-тест — выполняется только если пул собран с ThreadSafe
void test_multithreaded_stress() {
#ifdef OxiMemPool_ThreadSafe
    info("test_multithreaded_stress");
    constexpr int THREADS = 8;
    constexpr int OPS = 3000;
    MemoryPool<Trackable> pool(THREADS * 10);

    std::atomic<int> counter{0};
    auto worker = [&](int tid){
        std::mt19937 rng(tid + 1234);
        std::uniform_int_distribution<int> dist(1, 10000);
        std::uniform_int_distribution<int> sleepDist(0, 3);
        for (int i=0;i<OPS;i++){
            int v = dist(rng);
            auto o = pool.Make(v);
            // allocation might fail only if pool exhausted (shouldn't for our capacity),
            TEST_ASSERT(o);
            ++counter;
            // иногда переместим o или сбросим
            if ((v & 1) == 0) {
                auto tmp = std::move(o);
                if ((v & 3) == 0) std::this_thread::yield();
                tmp.Reset();
            } else {
                if ((v & 7) == 0) std::this_thread::sleep_for(std::chrono::microseconds(sleepDist(rng)));
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
    test_max_allocated_behavior();
    test_error_callback_exhaustion();
    test_error_callback_constructor_zero();
    test_multithreaded_stress();

    std::cout << "=== All tests PASSED ===\n";
    return 0;
}