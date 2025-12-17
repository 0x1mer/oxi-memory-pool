# C++ MemoryPool

[![License](https://img.shields.io/badge/license-MIT-green)](LICENSE)
[![C++](https://img.shields.io/badge/C%2B%2B-17%2B-blue)](https://en.cppreference.com/w/cpp/20)
![Header Only](https://img.shields.io/badge/header--only-yes-orange)
![Platform](https://img.shields.io/badge/platform-Windows%20%7C%20Linux%20%7C%20macOS-lightgrey)
![Status](https://img.shields.io/badge/status-active-success)

A lightweight fixed-size memory pool for C++ with RAII-based object management.

Designed for performance-critical code where dynamic allocations must be minimized or completely avoided.

---


## License

This project is licensed under the [MIT License](LICENSE).

## API Reference

### Basic Usage

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

### Compile-Time Configuration

#### Logging
You can enable internal pool logging by defining InfoLog
before including the header.
```cpp
#define InfoLog
#include "MemoryPool.h"
```
| Macro   | Values | Description                                |
| ------- | ------ | ------------------------------------------ |
| InfoLog | 0 / 1  | Enables verbose logging of pool operations |
## Appendix

### Object Lifetime Rules

- `MemoryPool` must outlive all `MemoryPoolObject<T>` instances.
- Destroying a pool while objects are still alive results in undefined behavior.
- `Release()` transfers ownership to the user and requires manual destruction.

---

### Thread Safety

- This memory pool is **not thread-safe**.
- Concurrent access requires external synchronization.

---

### Design Limitations

- Pool size is fixed at construction time.
- Objects are not zero-initialized.
- No bounds checking in release builds.

---

### Intended Use Cases

- Game engines
- ECS / component storage
- Embedded and real-time systems
- Custom allocators and low-level utilities


## Authors

- [@0x1mer](https://www.github.com/0x1mer)