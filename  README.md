# C++ MemoryPool

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
- Strong RAII ownership model (`MemoryPoolObject<T>`)
- Free-list reuse (O(1) allocation / deallocation)
- Correct alignment handling (supports over-aligned types)
- Strong exception safety for object construction
- Optional compile-time logging
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
#include "MemoryPool.h"

struct Foo {
    int a, b;
    Foo(int x, int y) : a(x), b(y) {}
};

int main() {
    MemoryPool<Foo> pool(128);

    auto obj = pool.Make(1, 2);
    obj->a = 42;

    // Object is automatically destroyed
    // and returned to the pool when leaving scope
}
```

---

## API Overview

### MemoryPool<T>

```cpp
explicit MemoryPool(size_t capacity);
```

Creates a pool capable of holding up to `capacity` objects of type `T`.

#### Allocation

```cpp
auto obj = pool.Make(args...);
```

- Constructs `T` in-place inside the pool
- Returns a `MemoryPoolObject<T>` (unique ownership)
- Strong exception guarantee:
  - If constructor throws, the slot is returned to the pool
  - No leaks, pool remains usable

#### Statistics

```cpp
size_t Capacity() const noexcept;
size_t Used() const noexcept;
size_t Available() const noexcept;
size_t MaxAllocated() const noexcept;
```

- `Capacity()` — total number of slots
- `Used()` — currently live objects
- `Available()` — free slots
- `MaxAllocated()` — highest linear allocation watermark

---

### MemoryPoolObject<T>

RAII wrapper representing **unique ownership** of an object inside the pool.

#### Properties

- Move-only
- Automatically destroys the object on scope exit
- Returns memory back to the pool
- Safe to reset explicitly

#### Interface

```cpp
T* Get() const;
T& operator*() const;
T* operator->() const;
explicit operator bool() const;
void Reset();
```

---

## Object Lifetime Rules

- `MemoryPool` **must outlive** all `MemoryPoolObject<T>` instances
- Destroying the pool while objects are still alive:
  - Triggers an `assert` in debug builds
  - Is undefined behaviour in release builds
- Each pool slot owns exactly one object at a time
- Objects are destroyed *before* their memory is returned to the pool

---

## Exception Safety

- Strong exception safety guarantee for `Make()`
- If a constructor throws:
  - Slot is returned to the free-list
  - `Used()` counter is rolled back
  - No memory leaks
- Destructors of `T` **must not throw**

---

## Thread Safety

### Default

- Not thread-safe

### Thread-safe mode

```cpp
#define OxiMemPool_ThreadSafe
#include "MemoryPool.h"
```

- All public pool operations are protected by a single `std::mutex`
- Safe for concurrent `Make()` and object destruction
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
#include "MemoryPool.h"
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

MemoryPool<Foo> pool(64, &MyErrorHandler);
```

- If a callback is provided, it is invoked instead of throwing
- Typical error cases:
  - Pool exhausted
  - Pool constructed with zero capacity

---

## Compile-Time Configuration

| Macro                    | Values | Description                                      |
|--------------------------|--------|--------------------------------------------------|
| OxiMemPool_InfoLog       | 0 / 1  | Enables verbose logging of pool operations       |
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