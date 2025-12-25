# A fixed-size object memory pool for C++

[![License](https://img.shields.io/badge/license-MIT-green)](LICENSE)
[![C++](https://img.shields.io/badge/C%2B%2B-20%2B-blue)](https://en.cppreference.com/w/cpp/20)
![Header Only](https://img.shields.io/badge/header--only-yes-orange)
![Platform](https://img.shields.io/badge/platform-Windows%20%7C%20Linux%20%7C%20macOS-lightgrey)
![Status](https://img.shields.io/badge/status-active-success)

A lightweight **fixed-size memory pool** for C++ with **RAII-based object lifetime management**.

Designed for performance-critical and low-level code where dynamic allocations must be minimized or fully avoided.

---

## Features

- Header-only
- Fixed-capacity pool (no heap allocations after construction)
- Strong RAII ownership model (`PoolHandle<T>`)
- Free-list reuse (O(1) allocation / deallocation)
- Correct alignment handling (supports over-aligned types)
- Strong exception safety for object construction
- Optional compile-time thread safety (mutex-based)
- Optional user-defined error callback
- C++20 constraints (`std::destructible`)

---

## Requirements

- C++20 or newer
- No external dependencies

---

## Basic Usage

```cpp
#include "MemOx/object_pool.hpp"

struct Foo {
    int a, b;
    Foo(int x, int y) : a(x), b(y) {}
};

int main() {
    ObjectPool<Foo> pool(128);

    auto obj = pool.emplace(1, 2);
    obj->a = 42;

    // Object is automatically destroyed
    // and returned to the pool when leaving scope
}
```

---

## API Overview

### ObjectPool<T>

```cpp
using LogFunction = void (*)(const std::string&);

explicit ObjectPool(size_t capacity, LogFunction log = nullptr);
```

Creates a pool capable of holding up to `capacity` objects of type `T`.
log — optional callback used for internal diagnostics and debugging.

#### Allocation

```cpp
auto obj = pool.emplace(args...);
```
 
- Constructs `T` in-place inside the pool
- Returns a `PoolHandle<T>` with unique ownership
- The object is automatically destroyed and returned to the pool when the handle goes out of scope
- Strong exception guarantee:
    - If `T`'s constructor throws, the slot is returned to the pool
    - No leaks, the pool remains usable
- If the pool is exhausted, an error is reported
  (exception or user-defined error callback)

#### Copy and move semantics

```cpp
// Non-copyable, non-movable
ObjectPool(const ObjectPool&) = delete;
ObjectPool& operator=(const ObjectPool&) = delete;
ObjectPool(ObjectPool&&) = delete;
ObjectPool& operator=(ObjectPool&&) = delete;
```

ObjectPool is neither copyable nor movable.

- This guarantees:
    - a single owner of the underlying memory block
    - stable object addresses for the entire lifetime of the pool
    - no accidental duplication or transfer of pool ownership

#### Statistics

```cpp
size_t size() const noexcept;
size_t capacity() const noexcept;
```

- `size()` — current number of live objects
- `capacity()` — maximum pool capacity

---

### PoolHandle<T>

RAII wrapper representing **unique ownership** of an object inside the pool.

#### Properties

- Move-only
- Automatically destroys the object on scope exit
- Returns memory back to the pool
- Safe to reset explicitly

#### Interface

```cpp
T* get() const noexcept;
T& operator*() const;
T* operator->() const;
explicit operator bool() const;
void reset() noexcept;
```

---

## Object Lifetime Rules

#### Pool destruction

```cpp
~ObjectPool() noexcept
    {
#ifndef NDEBUG
        assert(used_count_.load(std::memory_order_acquire) == 0 &&
               "ObjectPool destroyed with live objects");
#endif
        ::operator delete(pool_memory_, std::align_val_t{kSlotAlign});
    }
```

The pool must outlive all objects allocated from it.

In debug builds, destroying an ObjectPool while it still owns
live objects triggers an assertion failure, helping detect
lifetime and ownership bugs early.

In release builds, the pool releases its internal memory without
additional checks.

---

## Exception Safety

- Strong exception safety guarantee for `emplace()`
- If a constructor throws:
  - Slot is returned to the free-list
  - `size()` counter is rolled back
  - No memory leaks
- Destructors of `T` **must not throw**

---

## Thread Safety

### Default

- Not thread-safe

### Thread-safe mode

```cpp
#define OxiMemPool_ThreadSafe
#include "MemOx/object_pool.hpp"
```

- All public pool operations are protected by a single `std::mutex`
- Safe for concurrent `emplace()` and object destruction
- Object construction and destruction occur **outside** the mutex
- Slight performance overhead compared to single-threaded mode
- Not lock-free

---

## Error Handling

### Default behaviour

- Errors are reported via `std::runtime_error` exceptions

### Error callback support

```cpp
#define OxiMemPool_ErrCallback
#include "MemOx/object_pool.hpp"
```

Callback signature:

```cpp
void(const char* message, size_t errorCode)
```

Usage:

```cpp
void MyErrorHandler(const char* msg, size_t code) {
    // custom logging, abort, etc.
}

int main() {
    ObjectPool<int> pool(10);
    pool.set_error_callback(&MyErrorHandler);
}
```

- If a callback is provided, it is invoked instead of throwing
- Typical error cases:
  - Pool exhausted
  - Pool constructed with zero capacity

---

## Compile-Time Configuration

| Macro                    | Values | Description                                      |
|--------------------------|--------|--------------------------------------------------|
| OxiMemPool_ThreadSafe    | 0 / 1  | Enables mutex-based thread safety                |
| OxiMemPool_ErrCallback   | 0 / 1  | Enables user-defined error callback support      |

---

## Design Limitations

- Pool size is fixed at construction time
- No automatic growth
- Objects are not zero-initialized
- No bounds checking in release builds
- Not lock-free (even in thread-safe mode)
- Pool destruction must be externally synchronized in multithreaded code

---

## Intended Use Cases

- Game engines
- ECS / component storage
- Custom allocators
- Embedded and real-time systems
- Low-level infrastructure
- Performance-critical subsystems

---

## License

This project is licensed under the **MIT License** — see [LICENSE](LICENSE).

---

## Author

**@0x1mer**  
https://github.com/0x1mer
