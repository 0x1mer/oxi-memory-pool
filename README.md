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
- Strong RAII ownership model
- Free-list reuse (O(1) allocation / deallocation)
- Correct alignment handling
- Exception-safe object construction
- Optional compile-time logging
- Optional compile-time thread safety
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
- Returns a `MemoryPoolObject<T>`
- Strong exception guarantee

#### Statistics

```cpp
size_t Capacity() const;
size_t Used() const;
size_t Available() const;
size_t MaxAllocated() const;
```

---

### MemoryPoolObject<T>

RAII wrapper representing **unique ownership** of an object inside the pool.

#### Properties

- Move-only
- Automatically destroys the object on scope exit
- Returns memory back to the pool

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

- `MemoryPool` must outlive all `MemoryPoolObject<T>` instances
- Destroying the pool while objects are still alive triggers an `assert` in debug builds
- Each pool slot owns exactly one object at a time
- Objects are destroyed before their memory is returned to the pool

---

## Exception Safety

- Strong exception safety guarantee
- If a constructor throws:
  - Slot is returned to the free-list
  - No memory leaks
  - Pool remains usable

---

## Thread Safety

### Default

- Not thread-safe

### Thread-safe mode

```cpp
#define ThreadSafe
#include "MemoryPool.h"
```

- All pool operations are protected by a mutex
- Safe for concurrent `Make()` / destruction
- Slight performance overhead

---

## Compile-Time Configuration

### Logging

```cpp
#define InfoLog
#include "MemoryPool.h"
```

| Macro      | Values | Description                                  |
|------------|--------|----------------------------------------------|
| InfoLog    | 0 / 1  | Enables verbose logging of pool operations   |
| ThreadSafe | 0 / 1  | Enables mutex-based thread safety            |

---

## Design Limitations

- Pool size is fixed at construction time
- No automatic growth
- Objects are not zero-initialized
- No bounds checking in release builds
- Not lock-free (even in thread-safe mode)

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
