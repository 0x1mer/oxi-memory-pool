#pragma once

// Compilation flag for logging
// Use #define InfoLog to logging on
#ifdef InfoLog
#include <iostream>
#endif

// Forward declaration
template <typename T>
requires std::destructible<T>
class MemoryPool;

/**
 * @brief MemoryPool object
 *
 * @tparam T type of object
 *
 * RAII wraps over an object from the pool.
 * Automatically returns a memory region as a free slot to the pool when leaving scope
 *
 */
template <typename T>
requires std::destructible<T>
class MemoryPoolObject
{
private:
    MemoryPool<T> *m_pool = nullptr;
    T *m_object = nullptr;

private:
    friend class MemoryPool<T>;
    MemoryPoolObject() noexcept = default;

    MemoryPoolObject(MemoryPool<T> &pool, T *object) noexcept
        : m_pool(&pool), m_object(object) {}

public:
    MemoryPoolObject(const MemoryPoolObject &) = delete;
    MemoryPoolObject &operator=(const MemoryPoolObject &) = delete;

    MemoryPoolObject(MemoryPoolObject &&other) noexcept
        : m_pool(other.m_pool), m_object(other.m_object)
    {
        other.m_pool = nullptr;
        other.m_object = nullptr;
    }

    MemoryPoolObject &operator=(MemoryPoolObject &&other) noexcept
    {
        if (this != &other)
        {
            Destroy();
            m_pool = other.m_pool;
            m_object = other.m_object;
            other.m_pool = nullptr;
            other.m_object = nullptr;
        }
        return *this;
    }

    ~MemoryPoolObject() noexcept
    {
        Destroy();
    }

    void Destroy()
    {
        if (m_pool && m_object)
        {
            m_pool->Destroy(m_object);
            m_pool = nullptr;
            m_object = nullptr;
        }
    }

    [[nodiscard("Release() transfers ownership; returned pointer must be manually destroyed")]]
    T *Release() noexcept
    {
        T *tmp = m_object;
        m_object = nullptr;
        m_pool = nullptr;
        return tmp;
    }

    T *Get() const noexcept { return m_object; }
    T &operator*() const noexcept { return *m_object; }
    T *operator->() const noexcept { return m_object; }
    explicit operator bool() const noexcept { return m_object != nullptr; }
};

template <typename T>
requires std::destructible<T>
class MemoryPool
{
private:
    struct FreeNode
    {
        FreeNode *next = nullptr;
    };

    size_t m_count;
    size_t m_max_allocated = 0;
    std::byte *m_pool;

    FreeNode *m_free_head = nullptr;

    static constexpr size_t SlotSize =
        sizeof(T) > sizeof(FreeNode) ? sizeof(T) : sizeof(FreeNode);
    static constexpr size_t SlotAlign =
        alignof(T) > alignof(FreeNode) ? alignof(T) : alignof(FreeNode);

private:
    friend class MemoryPoolObject<T>;
    void Destroy(T *obj)
    {
#ifdef InfoLog
        std::cout << "[Pool][DESTROY] destruct object at "
                  << static_cast<void *>(obj) << "\n";
#endif
        obj->~T();

        auto *node = reinterpret_cast<FreeNode *>(obj);
        node->next = m_free_head;
        m_free_head = node;

#ifdef InfoLog
        std::cout << "[Pool][FREE-LIST] slot returned at "
                  << static_cast<void *>(node) << "\n";
#endif
    }

private:
    T *Allocate()
    {
        if (m_free_head)
        {
            FreeNode *node = m_free_head;
            m_free_head = node->next;

#ifdef InfoLog
            std::cout << "[Pool][ALLOC][FREE-LIST] reuse slot at "
                      << static_cast<void *>(node) << "\n";
#endif
            return reinterpret_cast<T *>(node);
        }

        assert(m_max_allocated < m_count);

        size_t index = m_max_allocated++;
        std::byte *addr = m_pool + SlotSize * index;

#ifdef InfoLog
        std::cout << "[Pool][ALLOC][LINEAR] index=" << index
                  << ", addr=" << static_cast<void *>(addr) << "\n";
#endif
        return reinterpret_cast<T *>(addr);
    }

    template <typename... Args>
    T *Create(Args &&...args)
    {
        T *ptr = Allocate();

#ifdef InfoLog
        std::cout << "[Pool][CREATE] construct object at "
                  << static_cast<void *>(ptr) << "\n";
#endif
        new (ptr) T(std::forward<Args>(args)...);
        return std::launder(ptr);
    }

public:
    explicit MemoryPool(size_t count)
        : m_count(count)
    {
        m_pool = static_cast<std::byte *>(
            ::operator new(SlotSize * count, std::align_val_t{SlotAlign}));

#ifdef InfoLog
        std::cout << "[Pool][INIT] pool=" << static_cast<void *>(m_pool)
                  << ", slots=" << m_count
                  << ", slotSize=" << SlotSize
                  << ", slotAlign=" << SlotAlign << "\n";
#endif
    }

    ~MemoryPool() noexcept
    {
#ifdef InfoLog
        std::cout << "[Pool][DESTROY] max allocated=" << m_max_allocated << "\n";
#endif
        ::operator delete(m_pool, std::align_val_t{SlotAlign});
    }

    template <typename... Args>
    [[nodiscard]] MemoryPoolObject<T> Make(Args &&...args)
    {
#ifdef InfoLog
        std::cout << "[Pool][MAKE] create RAII object\n";
#endif
        return MemoryPoolObject<T>(*this, Create(std::forward<Args>(args)...));
    }

    size_t Capacity() const noexcept { return m_count; }
    size_t MaxAllocated() const noexcept { return m_max_allocated; }
};