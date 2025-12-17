#pragma once

//=====================================================//
//                                                     //
//   ██████╗ ██╗  ██╗ ██╗███╗   ███╗███████╗██████╗    //
//   ██╔═██╗╚██╗██╔╝ ██║████╗ ████║██╔════╝██╔══██╗    //
//   ██████╔╝ ╚███╔╝  ██║██╔████╔██║█████╗  ██████╔╝   //
//   ██╔═██╗   ██╔██╗  ██║██║╚██╔╝██║██╔══╝  ██╔═██╗   //
//   ██████╔╝ ██╔╝ ██╗ ██║██║ ╚═╝ ██║███████╗██║  ██╗  //
//   ╚═════╝  ╚═╝  ╚═╝ ╚═╝╚═╝     ╚═╝╚══════╝╚═╝  ╚═╝  //
//                                                     //
//                    0x1mer                           //
//=====================================================//

// library source: https://github.com/0x1mer/oxi-memory-pool
// MIT license

// Logging support.
//
// Defining OxiMemPool_InfoLog enables verbose informational logging
// of memory pool operations.
#ifdef OxiMemPool_InfoLog
#include <iostream>
#endif

// Thread-safety support.
//
// Defining OxiMemPool_ThreadSafe enables basic thread-safety using
// a single internal mutex to protect pool operations.
#ifdef OxiMemPool_ThreadSafe
#include <mutex>
#endif

// Error handling support.
//
// Defining OxiMemPool_ErrCallback enables support for a user-defined
// error callback with the following signature:
//
//     void(const char* message, size_t errorCode)
//
// If no callback is registered, or if this macro is not defined,
// errors are reported via standard runtime exceptions.
#ifdef OxiMemPool_ErrCallback
using ErrorCallback = void (*)(const char *, size_t);
#endif

#include <cstddef>     // std::byte
#include <new>         // operator new, std::align_val_t
#include <cassert>     // assert
#include <utility>     // std::forward
#include <type_traits> // std::destructible
#include <stdexcept>   //
#include <string>      // std::string

// Forward declaration
/**
 * @brief MemoryPool forward declaration
 *
 * The MemoryPool is a fixed-size pool of storage slots capable of
 * holding objects of type T. This forward declaration is constrained
 * with std::destructible to ensure T can be destroyed.
 *
 * @tparam T Type of object stored in the pool. Must be destructible.
 */
template <typename T>
    requires std::destructible<T>
class MemoryPool;

/**
 * @brief RAII wrapper for an object allocated from MemoryPool.
 *
 * MemoryPoolObject provides exclusive ownership of a single object
 * allocated from a MemoryPool<T>. When the MemoryPoolObject is destroyed
 * or Reset() is called, the encapsulated object is destroyed and its slot
 * is returned to the originating pool.
 *
 * This class is moveable but not copyable.
 *
 * @tparam T Type of object borrowed from the pool.
 */
template <typename T>
    requires std::destructible<T>
class MemoryPoolObject
{
private:
    MemoryPool<T> *m_pool = nullptr; /**< Pointer to the originating pool. */
    T *m_object = nullptr;           /**< Pointer to the managed object. */

private:
    friend class MemoryPool<T>;

    /**
     * @brief Default construct an empty MemoryPoolObject.
     *
     * Creates a "null" wrapper that does not own any object. Used by the pool
     * when constructing return values.
     */
    MemoryPoolObject() noexcept = default;

    /**
     * @brief Construct wrapper for an object from a pool.
     *
     * This constructor is private and called by MemoryPool when returning
     * a freshly created object.
     *
     * @param pool Reference to the MemoryPool that created the object.
     * @param object Pointer to the object in pool storage.
     */
    MemoryPoolObject(MemoryPool<T> &pool, T *object) noexcept
        : m_pool(&pool), m_object(object) {}

    /**
     * @brief Destroy the managed object and return its slot to the pool.
     *
     * Safe to call on a null wrapper; after call the wrapper becomes empty.
     */
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
    MemoryPoolObject(const MemoryPoolObject &) = delete;            /**< Non-copyable. */
    MemoryPoolObject &operator=(const MemoryPoolObject &) = delete; /**< Non-copyable. */

    /**
     * @brief Move constructor.
     *
     * Transfers ownership from \p other to this wrapper. \p other becomes empty.
     *
     * @param other Source wrapper to move from.
     */
    MemoryPoolObject(MemoryPoolObject &&other) noexcept
        : m_pool(other.m_pool), m_object(other.m_object)
    {
        other.m_pool = nullptr;
        other.m_object = nullptr;
    }

    /**
     * @brief Move assignment operator.
     *
     * Releases any currently owned object, then takes ownership from \p other.
     *
     * @param other Source wrapper to move from.
     * @return Reference to *this.
     */
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

    /**
     * @brief Destructor.
     *
     * Ensures the managed object is destroyed and its slot returned to the pool.
     * noexcept because Destroy() is noexcept and pool destruction handles invariants.
     */
    ~MemoryPoolObject() noexcept
    {
        Destroy();
    }

    /**
     * @brief Manually release/destroy the managed object and return the slot.
     *
     * After Reset(), the wrapper is empty and operator bool() will return false.
     */
    void Reset() noexcept { Destroy(); }

    /**
     * @brief Get raw pointer to the managed object.
     *
     * @return Pointer to T, or nullptr if wrapper is empty.
     */
    T *Get() const noexcept { return m_object; }

    /**
     * @brief Dereference to the managed object.
     *
     * Undefined behaviour if called when wrapper is empty. Intended to mirror
     * std::unique_ptr<T> semantics.
     *
     * @return Reference to the managed object.
     */
    T &operator*() const noexcept { return *m_object; }

    /**
     * @brief Member access to the managed object.
     *
     * Undefined behaviour if called when wrapper is empty.
     *
     * @return Pointer to the managed object.
     */
    T *operator->() const noexcept { return m_object; }

    /**
     * @brief Allow boolean checks for validity.
     *
     * @return true if wrapper owns an object, false otherwise.
     */
    explicit operator bool() const noexcept { return m_object != nullptr; }
};

/**
 * @brief Fixed-capacity MemoryPool for objects of type T.
 *
 * MemoryPool allocates a contiguous block of memory sized to hold a fixed number
 * of slots (specified at construction). Each slot can hold one T object.
 *
 * Allocation strategy:
 *  - If a freed slot is available, it is taken from a singly-linked free list.
 *  - Otherwise, slots are handed out linearly from the preallocated block until
 *    capacity is exhausted.
 *
 * Thread-safety model:
 *  - When OxiMemPool_ThreadSafe is defined, public operations (Make / Destroy)
 *    use a single std::mutex to protect free-list and accounting members.
 *  - Low-level helpers (Allocate_NoLock / Free_NoLock) are intentionally
 *    lock-free and must be called either while holding the mutex (thread-safe
 *    path) or from single-threaded context.
 *
 * Lifetime rules:
 *  - Construction and destruction of T are performed *outside* the pool mutex.
 *    The pool reserves/releases slots and adjusts counters under the mutex,
 *    but object constructors/destructors run without holding the mutex to
 *    avoid deadlocks and poor scalability.
 *
 * @tparam T Type of object stored in the pool. Must be Destructible.
 */
template <typename T>
    requires std::destructible<T>
class MemoryPool
{
private:
    struct FreeNode
    {
        FreeNode *next = nullptr;
    };

    size_t m_count;              /**< Total number of slots in the pool (capacity). */
    std::byte *m_pool = nullptr; /**< Pointer to the contiguous block of pool storage. */

#ifdef OxiMemPool_ErrCallback
    ErrorCallback m_errCallback{};
#endif

#ifdef OxiMemPool_ThreadSafe
    mutable std::mutex m; /**< Mutex protecting free-list and accounting in ThreadSafe mode. */
#endif

    // Protected by mutex in thread-safe mode (or only accessed from single-threaded code)
    FreeNode *m_free_head = nullptr; /**< Head of the singly-linked free-list. */
    size_t m_used = 0;               /**< Number of currently live objects. */
    size_t m_max_allocated = 0;      /**< Number of slots handed out linearly so far. */

    // Slot size/alignment calculations
    static constexpr size_t RawSlotSize =
        sizeof(T) > sizeof(FreeNode) ? sizeof(T) : sizeof(FreeNode);
    static constexpr size_t SlotAlign =
        alignof(T) > alignof(FreeNode) ? alignof(T) : alignof(FreeNode);
    static constexpr size_t SlotSize =
        (RawSlotSize + SlotAlign - 1) / SlotAlign * SlotAlign;

private:
    friend class MemoryPoolObject<T>;

    // ---------------------
    // Low-level, no-lock helpers
    // ---------------------
    // These helpers do NOT take the mutex. They must be used either while the
    // caller holds the mutex (thread-safe path) or from single-threaded code.

    /**
     * @brief Allocate a raw slot without taking a lock.
     *
     * Returns nullptr via ReportError if the pool is exhausted.
     */
    T *Allocate_NoLock()
    {
        if (m_free_head)
        {
            FreeNode *node = m_free_head;
            m_free_head = node->next;
            return reinterpret_cast<T *>(node);
        }

        if (m_max_allocated >= m_count)
        {
            ReportError("MemoryPool exhausted", 1);
            return nullptr;
        }

        return reinterpret_cast<T *>(m_pool + SlotSize * m_max_allocated++);
    }

    /**
     * @brief Return a slot to the free list without taking a lock.
     */
    void Free_NoLock(T *obj) noexcept
    {
        auto *node = reinterpret_cast<FreeNode *>(obj);
        node->next = m_free_head;
        m_free_head = node;
    }

public:
    /**
     * @brief Create a new object in the pool and return an owning RAII wrapper.
     *
     * Thread-safe behaviour (when OxiMemPool_ThreadSafe defined):
     *  - Reserve a slot and increment m_used under the mutex.
     *  - Construct the object outside the mutex.
     *  - If construction throws, rollback the reservation under the mutex.
     *
     * Single-threaded behaviour:
     *  - Allocate, construct, adjust counters without locking.
     *
     * @tparam Args Parameter pack for T constructor.
     * @param args Arguments forwarded to T constructor.
     * @return MemoryPoolObject<T> RAII wrapper owning the new object,
     *         or an empty wrapper if allocation failed.
     */
    template <typename... Args>
    [[nodiscard]] MemoryPoolObject<T> Make(Args &&...args)
    {
#ifdef OxiMemPool_ThreadSafe
        T *slot = nullptr;

        {
            std::lock_guard<std::mutex> guard(m);
            slot = Allocate_NoLock();
            if (!slot) [[unlikely]]
            {
                return MemoryPoolObject<T>();
            }
            ++m_used;
        }

        try
        {
            new (slot) T(std::forward<Args>(args)...);
        }
        catch (...)
        {
            // Rollback reservation if construction failed.
            std::lock_guard<std::mutex> guard(m);
            --m_used;
            Free_NoLock(slot);
            throw;
        }

        return MemoryPoolObject<T>(*this, std::launder(slot));
#else
        // Single-threaded path: simple allocate -> construct -> accounting.
        T *slot = Allocate_NoLock();
        if (!slot) [[unlikely]]
        {
            return MemoryPoolObject<T>();
        }

        try
        {
            new (slot) T(std::forward<Args>(args)...);
        }
        catch (...)
        {
            Free_NoLock(slot);
            throw;
        }

        ++m_used;
        return MemoryPoolObject<T>(*this, std::launder(slot));
#endif
    }

private:
    /**
     * @brief Destroy object located at \p obj and push its slot to free-list.
     *
     * Destructor for T is invoked outside of the pool mutex. Only after the
     * destructor returns do we take the mutex to push the slot back into the
     * free-list and decrement m_used (in thread-safe mode).
     *
     * This method is noexcept and intended to be called from
     * MemoryPoolObject::Destroy().
     */
    void Destroy(T *obj) noexcept
    {
        obj->~T();

#ifdef OxiMemPool_ThreadSafe
        std::lock_guard<std::mutex> guard(m);
        Free_NoLock(obj);
        --m_used;
#else
        Free_NoLock(obj);
        --m_used;
#endif
    }

    // ---------------------
    // Misc helpers
    // ---------------------
    void ReportError(const char *errorMessage, size_t errorCode)
    {
#ifdef OxiMemPool_ErrCallback
        if (m_errCallback)
        {
            m_errCallback(errorMessage, errorCode);
            return;
        }
        else
        {
            throw std::runtime_error(
                std::string(errorMessage) +
                "\nError code: " +
                std::to_string(errorCode));
        }
#endif

        throw std::runtime_error(
            std::string(errorMessage) +
            "\nError code: " +
            std::to_string(errorCode));
    }

    void Init()
    {
        m_pool = static_cast<std::byte *>(
            ::operator new(SlotSize * m_count,
                           std::align_val_t{SlotAlign}));
    }

public:
    explicit MemoryPool(size_t count)
        : m_count(count)
    {
        if (count == 0)
        {
            ReportError("Size of pool cannot be 0", 0);
        }
        Init();
    }

#ifdef OxiMemPool_ErrCallback
    explicit MemoryPool(
        size_t count,
        ErrorCallback errCallback)
        : m_count(count), m_errCallback(errCallback)
    {
        if (count == 0)
        {
            ReportError("Size of pool cannot be 0", 0);
        }
        Init();
    }
#endif

    MemoryPool(const MemoryPool &) = delete;
    MemoryPool &operator=(const MemoryPool &) = delete;

    ~MemoryPool() noexcept
    {
#ifdef OxiMemPool_InfoLog
        std::cout << "[Pool][DESTROY] max allocated=" << m_max_allocated << "\n";
#endif
#ifndef NDEBUG
        assert(Used() == 0 && "MemoryPool destroyed with live objects");
#endif
        ::operator delete(m_pool, std::align_val_t{SlotAlign});
    }

    size_t Capacity() const noexcept { return m_count; }

    size_t MaxAllocated() const noexcept
    {
#ifdef OxiMemPool_ThreadSafe
        std::lock_guard<std::mutex> guard(m);
        return m_max_allocated;
#else
        return m_max_allocated;
#endif
    }

    size_t Used() const noexcept
    {
#ifdef OxiMemPool_ThreadSafe
        std::lock_guard<std::mutex> guard(m);
        return m_used;
#else
        return m_used;
#endif
    }

    size_t Available() const noexcept
    {
#ifdef OxiMemPool_ThreadSafe
        std::lock_guard<std::mutex> guard(m);
        return m_count - m_used;
#else
        return m_count - m_used;
#endif
    }
};