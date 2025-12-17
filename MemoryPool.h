#pragma once

#include <cstddef>     // std::byte
#include <new>         // operator new, std::align_val_t
#include <cassert>     // assert
#include <utility>     //std::forward
#include <type_traits> //std::destructible

// For now this pool is not thread-safe. But it will be later.

// Compilation flag for logging
// Use #define InfoLog to logging on
#ifdef InfoLog
#include <iostream>
#endif

// Compilation flag for thread safe
// Use #define ThreadSafe to thread-safe on
#ifdef ThreadSafe
#include <mutex>
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

    void Destroy()
    {
        if (m_pool && m_object)
        {
            m_pool->Destroy(m_object);
            m_pool = nullptr;
            m_object = nullptr;
        }
    }

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

    void Reset() noexcept { Destroy(); }

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
    std::byte *m_pool;

#ifdef ThreadSafe
    mutable std::mutex m;
#endif

    // All of this only under mutex!!! (in thread safe version)
    FreeNode *m_free_head = nullptr;
    size_t m_used = 0;
    size_t m_max_allocated = 0;

    static constexpr size_t RawSlotSize =
        sizeof(T) > sizeof(FreeNode) ? sizeof(T) : sizeof(FreeNode);
    static constexpr size_t SlotAlign =
        alignof(T) > alignof(FreeNode) ? alignof(T) : alignof(FreeNode);
    static constexpr size_t SlotSize =
        (RawSlotSize + SlotAlign - 1) / SlotAlign * SlotAlign;

private:
    friend class MemoryPoolObject<T>;
    void Destroy(T *obj)
    {
        obj->~T();
        auto *node = reinterpret_cast<FreeNode *>(obj);

#ifdef ThreadSafe
        std::lock_guard<std::mutex> guard(m);
        FreeNode *old_head = m_free_head;
        node->next = old_head;
        m_free_head = node;
        m_used--;
#else
        node->next = m_free_head;
        m_free_head = node;
        --m_used;
#endif
    }

private:
    T *Allocate()
    {
#ifdef ThreadSafe
        // 1. Try free-list
        std::lock_guard<std::mutex> guard(m);
        if (m_free_head)
        {
            FreeNode *node = m_free_head;
            m_free_head = node->next;
            return reinterpret_cast<T *>(node);
        }

        // 2. Linear allocation
        assert(m_max_allocated < m_count);

        return reinterpret_cast<T *>(m_pool + SlotSize * m_max_allocated++);
#else
        if (m_free_head)
        {
            FreeNode *node = m_free_head;
            m_free_head = node->next;
            return reinterpret_cast<T *>(node);
        }

        assert(m_max_allocated < m_count);
        return reinterpret_cast<T *>(m_pool + SlotSize * m_max_allocated++);
#endif
    }

    template <typename... Args>
    T *Create(Args &&...args)
    {
        T *ptr = Allocate();
        try
        {
#ifdef InfoLog
            std::cout << "[Pool][CREATE] construct object at "
                      << static_cast<void *>(ptr) << "\n";
#endif
            new (ptr) T(std::forward<Args>(args)...);
        }
        catch (...)
        {
            auto *node = reinterpret_cast<FreeNode *>(ptr);
#ifdef ThreadSafe
            {
                std::lock_guard<std::mutex> guard(m);
                node->next = m_free_head;
                m_free_head = node;
            }
#else
            node->next = m_free_head;
            m_free_head = node;
#endif
            throw;
        }
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

    MemoryPool(const MemoryPool &) = delete;
    MemoryPool &operator=(const MemoryPool &) = delete;

    ~MemoryPool() noexcept
    {
#ifdef InfoLog
        std::cout << "[Pool][DESTROY] max allocated=" << m_max_allocated << "\n";
#endif
#ifndef NDEBUG
        assert(Used() == 0 && "MemoryPool destroyed with live objects");
#endif
        ::operator delete(m_pool, std::align_val_t{SlotAlign});
    }

    template <typename... Args>
    [[nodiscard]] MemoryPoolObject<T> Make(Args &&...args)
    {
        T *ptr = Create(std::forward<Args>(args)...);
#ifdef ThreadSafe
        std::lock_guard<std::mutex> guard(m);
        m_used++;
#else
        m_used++;
#endif
        return MemoryPoolObject<T>(*this, ptr);
    }

    size_t Capacity() const noexcept { return m_count; }
    size_t MaxAllocated() const noexcept
    {
#ifdef ThreadSafe
        std::lock_guard<std::mutex> guard(m);
        return m_max_allocated;
#else
        return m_max_allocated;
#endif
    }
    size_t Used() const noexcept
    {
#ifdef ThreadSafe
        std::lock_guard<std::mutex> guard(m);
        return m_used;
#else
        return m_used;
#endif
    }
    size_t Available() const noexcept
    {
#ifdef ThreadSafe
        std::lock_guard<std::mutex> guard(m);
        return m_count - m_used;
#else
        return m_count - m_used;
#endif
    }
};