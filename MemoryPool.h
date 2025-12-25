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

/**
 * @file memory_pool.h
 * @brief Lightweight fixed-size object memory pool and RAII wrapper.
 *
 * This header provides a simple memory pool implementation suitable for
 * allocating many small objects of the same type T with minimal runtime
 * overhead. It also contains a small RAII wrapper (MemoryPoolObject) which
 * returns objects to the pool automatically when it goes out of scope.
 *
 * Features:
 * - Optional informational logging via OxiMemPool_InfoLog macro.
 * - Optional basic thread-safety via OxiMemPool_ThreadSafe macro (single
 *   mutex protecting pool operations).
 * - Optional user-defined error callback via OxiMemPool_ErrCallback macro.
 * - Proper alignment for stored objects and storage reuse via an internal
 *   singly-linked free list.
 *
 * @note The pool stores raw memory and constructs/destructs T in-place using
 * placement new and explicit destructor calls. T must be Destructible.
 *
 * @author 0x1mer
 * @license MIT
 */

// library source: https://github.com/0x1mer/oxi-memory-pool

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
#include <stdexcept>
#include <string>

// Forward declaration
template <typename T>
    requires std::destructible<T>
class MemoryPool;

/**
 * @brief RAII handle that holds an object allocated from MemoryPool<T>.
 *
 * MemoryPoolObject is a move-only type that owns a pointer to an object
 * allocated from a MemoryPool. When the MemoryPoolObject is destroyed (or
 * Reset() is called), the contained object is destroyed and the memory slot
 * is returned to the pool.
 *
 * The class intentionally deletes copy operations to avoid double-freeing
 * pool slots; move semantics are provided for efficient ownership transfer.
 *
 * @tparam T Type of the object stored in the pool. Must be destructible.
 */
template <typename T>
    requires std::destructible<T>
class MemoryPoolObject
{
private:
    MemoryPool<T> *m_pool = nullptr; /**< Pointer to originating pool. */
    T *m_object = nullptr;           /**< Pointer to the object in-place. */

private:
    friend class MemoryPool<T>;

    /**
     * @brief Default constructor creates an empty (null) handle.
     */
    MemoryPoolObject() noexcept = default;

    /**
     * @brief Construct a handle bound to a pool and an object pointer.
     *
     * @param pool Reference to the MemoryPool that owns the storage.
     * @param object Pointer to the constructed object inside the pool.
     */
    MemoryPoolObject(MemoryPool<T> &pool, T *object) noexcept
        : m_pool(&pool), m_object(object) {}

    /**
     * @brief Destroy the owned object and return its slot to the pool.
     *
     * Called from the destructor and when resetting/moving to release the
     * resource back to the pool. Safe to call when the handle is empty.
     */
    void Destroy()
    {
        if (m_pool && m_object)
        {
#ifdef OxiMemPool_InfoLog
            std::cout << "[PoolObject][DESTROY] object=" << m_object << "\n";
#endif
            m_pool->Destroy(m_object);
            m_pool = nullptr;
            m_object = nullptr;
        }
    }

public:
    MemoryPoolObject(const MemoryPoolObject &) = delete;            /**< Non-copyable. */
    MemoryPoolObject &operator=(const MemoryPoolObject &) = delete; /**< Non-copy-assignable. */

    /**
     * @brief Move constructor transfers ownership from other to this.
     *
     * After the move, the source handle is left empty (no pool, no object).
     */
    MemoryPoolObject(MemoryPoolObject &&other) noexcept
        : m_pool(other.m_pool), m_object(other.m_object)
    {
#ifdef OxiMemPool_InfoLog
        std::cout << "[PoolObject][MOVE] from=" << other.m_object << "\n";
#endif
        other.m_pool = nullptr;
        other.m_object = nullptr;
    }

    /**
     * @brief Move-assignment operator. Releases any currently owned object
     * and takes ownership from other.
     *
     * Strong exception-safety is provided since Destroy() is noexcept.
     */
    MemoryPoolObject &operator=(MemoryPoolObject &&other) noexcept
    {
        if (this != &other)
        {
            Destroy();
            m_pool = other.m_pool;
            m_object = other.m_object;
#ifdef OxiMemPool_InfoLog
            std::cout << "[PoolObject][MOVE_ASSIGN] object=" << m_object << "\n";
#endif
            other.m_pool = nullptr;
            other.m_object = nullptr;
        }
        return *this;
    }

    /**
     * @brief Destructor — destroys owned object (if any) and returns its slot.
     */
    ~MemoryPoolObject() noexcept
    {
        Destroy();
    }

    /**
     * @brief Reset the handle, destroying the contained object (if any).
     */
    void Reset() noexcept { Destroy(); }

    /**
     * @brief Access the underlying pointer to the object.
     * @return Pointer to T, or nullptr if the handle is empty.
     */
    T *Get() const noexcept { return m_object; }

    /**
     * @brief Dereference operator to access the object.
     * @return Reference to the contained T. Behavior undefined if empty.
     */
    T &operator*() const noexcept { return *m_object; }

    /**
     * @brief Pointer-like access to the object.
     * @return Pointer to the contained T. Behavior undefined if empty.
     */
    T *operator->() const noexcept { return m_object; }

    /**
     * @brief Boolean conversion indicating whether the handle owns an object.
     * @return true if non-null, false otherwise.
     */
    explicit operator bool() const noexcept { return m_object != nullptr; }
};

/**
 * @brief Fixed-capacity memory pool for objects of type T.
 *
 * MemoryPool manages a preallocated block of memory capable of holding up to
 * `count` objects of type T. Objects are constructed in-place using
 * placement-new by calling Make(...). When a MemoryPoolObject returned by
 * Make goes out of scope (or Reset is called), the object is destroyed and
 * its slot is returned to the internal free list.
 *
 * The implementation prioritizes simplicity and low overhead. It is not
 * lock-free; optional locking via OxiMemPool_ThreadSafe uses a single
 * std::mutex to protect concurrent access.
 *
 * @tparam T Type stored in the pool; must be Destructible.
 */
template <typename T>
    requires std::destructible<T>
class MemoryPool
{
private:
    /**
     * @brief Internal node used for the free list.
     *
     * FreeNode overlays freed object storage and contains a pointer to the
     * next free slot.
     */
    struct FreeNode
    {
        FreeNode *next = nullptr;
    };

    size_t m_count;        /**< Total number of slots (capacity). */
    std::byte *m_pool = nullptr; /**< Pointer to raw storage block. */

#ifdef OxiMemPool_ErrCallback
    ErrorCallback m_errCallback{}; /**< Optional user error callback. */
#endif

#ifdef OxiMemPool_ThreadSafe
    mutable std::mutex m; /**< Mutex protecting mutable state when enabled. */
#endif

    FreeNode *m_free_head = nullptr; /**< Head of the singly-linked free list. */
    size_t m_used = 0;               /**< Number of currently constructed objects. */
    size_t m_max_allocated = 0;      /**< Number of slots that have ever been allocated (high-water index). */

    /**
     * @brief Size and alignment calculations for each slot.
     *
     * RawSlotSize — the storage size required to hold either T or FreeNode.
     * SlotAlign — alignment chosen to satisfy both T and FreeNode.
     * SlotSize — final aligned slot size used to index into the storage.
     */
    static constexpr size_t RawSlotSize =
        sizeof(T) > sizeof(FreeNode) ? sizeof(T) : sizeof(FreeNode);
    static constexpr size_t SlotAlign =
        alignof(T) > alignof(FreeNode) ? alignof(T) : alignof(FreeNode);
    static constexpr size_t SlotSize =
        (RawSlotSize + SlotAlign - 1) / SlotAlign * SlotAlign;

private:
    friend class MemoryPoolObject<T>;

    /**
     * @brief Allocate a raw slot without taking the external lock.
     *
     * This returns a pointer to memory suitable for placement-new'ing a T.
     * It prefers reusing a slot from the free list; if none are available it
     * returns the next unused region from the raw storage. If the pool is
     * exhausted (max_allocated >= m_count) it reports an error and returns
     * nullptr.
     *
     * @return Pointer to raw memory for T, or nullptr on exhaustion.
     */
    T *Allocate_NoLock()
    {
        if (m_free_head)
        {
            FreeNode *node = m_free_head;
            m_free_head = node->next;
#ifdef OxiMemPool_InfoLog
            std::cout << "[Pool][ALLOC][REUSE] slot=" << node << "\n";
#endif
            return reinterpret_cast<T *>(node);
        }

        if (m_max_allocated >= m_count)
        {
#ifdef OxiMemPool_InfoLog
            std::cout << "[Pool][ALLOC][FAILED] exhausted\n";
#endif
            ReportError("MemoryPool exhausted", 1);
            return nullptr;
        }

        auto *ptr = reinterpret_cast<T *>(m_pool + SlotSize * m_max_allocated++);
#ifdef OxiMemPool_InfoLog
        std::cout << "[Pool][ALLOC][NEW] slot=" << ptr
                  << " index=" << (m_max_allocated - 1) << "\n";
#endif
        return ptr;
    }

    /**
     * @brief Return a slot into the free list without taking the external lock.
     *
     * The pointer must point to storage previously returned by Allocate_NoLock
     * and must not be used after calling this function (until re-allocated).
     */
    void Free_NoLock(T *obj) noexcept
    {
        auto *node = reinterpret_cast<FreeNode *>(obj);
        node->next = m_free_head;
        m_free_head = node;
#ifdef OxiMemPool_InfoLog
        std::cout << "[Pool][FREE] slot=" << obj << "\n";
#endif
    }

public:
    /**
     * @brief Create and construct an object of type T inside the pool.
     *
     * This function constructs a T in-place using perfect forwarding of the
     * provided arguments. It returns a MemoryPoolObject<T> that will destroy
     * the object and return the slot to the pool when the handle is destroyed.
     *
     * On allocation failure (pool exhausted) a default-empty MemoryPoolObject
     * is returned. If construction of T throws, the slot is returned and the
     * exception is propagated.
     *
     * Thread-safety: if OxiMemPool_ThreadSafe is defined this function will
     * lock the internal mutex while allocating and updating counters.
     *
     * @tparam Args Parameter pack forwarded to T's constructor.
     * @param args Constructor arguments for T.
     * @return MemoryPoolObject<T> owning the constructed object or empty on
     *         failure.
     */
    template <typename... Args>
    [[nodiscard]] MemoryPoolObject<T> Make(Args &&...args)
    {
#ifdef OxiMemPool_ThreadSafe
        T *slot = nullptr;
        {
            std::lock_guard<std::mutex> guard(m);
            slot = Allocate_NoLock();
            if (!slot)
                return MemoryPoolObject<T>();
            ++m_used;
#ifdef OxiMemPool_InfoLog
            std::cout << "[Pool][USED] ++ -> " << m_used << "\n";
#endif
        }

        try
        {
            new (slot) T(std::forward<Args>(args)...);
        }
        catch (...)
        {
            std::lock_guard<std::mutex> guard(m);
            --m_used;
            Free_NoLock(slot);
            throw;
        }

        return MemoryPoolObject<T>(*this, std::launder(slot));
#else
        T *slot = Allocate_NoLock();
        if (!slot)
            return MemoryPoolObject<T>();

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
#ifdef OxiMemPool_InfoLog
        std::cout << "[Pool][USED] ++ -> " << m_used << "\n";
#endif
        return MemoryPoolObject<T>(*this, std::launder(slot));
#endif
    }

private:
    /**
     * @brief Destroy an object located in the pool and return its slot.
     *
     * This function calls the object's destructor and places the slot back on
     * the free list. It is noexcept so it is safe to call from destructors
     * and cleanup paths.
     *
     * @param obj Pointer to the object previously created inside the pool.
     */
    void Destroy(T *obj) noexcept
    {
#ifdef OxiMemPool_InfoLog
        std::cout << "[Pool][OBJ_DTOR] object=" << obj << "\n";
#endif
        obj->~T();

#ifdef OxiMemPool_ThreadSafe
        std::lock_guard<std::mutex> guard(m);
#endif
        Free_NoLock(obj);
        --m_used;
#ifdef OxiMemPool_InfoLog
        std::cout << "[Pool][USED] -- -> " << m_used << "\n";
#endif
    }

    /**
     * @brief Internal error reporting helper.
     *
     * If OxiMemPool_ErrCallback is defined and an ErrorCallback was provided
     * during construction it will be invoked. Otherwise this function throws
     * std::runtime_error with a formatted message.
     *
     * @param errorMessage Human-readable description of the error.
     * @param errorCode Numeric error code (user-defined semantics).
     */
    void ReportError(const char *errorMessage, size_t errorCode)
    {
#ifdef OxiMemPool_InfoLog
        std::cout << "[Pool][ERROR] " << errorMessage
                  << " code=" << errorCode << "\n";
#endif
#ifdef OxiMemPool_ErrCallback
        if (m_errCallback)
        {
            m_errCallback(errorMessage, errorCode);
            return;
        }
#endif
        throw std::runtime_error(
            std::string(errorMessage) +
            "\nError code: " +
            std::to_string(errorCode));
    }

    /**
     * @brief Allocate and initialize the raw storage block used by the pool.
     *
     * Uses ::operator new with alignment to allocate SlotSize * m_count bytes.
     * This method is called from constructors and assumes m_count has been set.
     */
    void Init()
    {
        m_pool = static_cast<std::byte *>(
            ::operator new(SlotSize * m_count,
                           std::align_val_t{SlotAlign}));
#ifdef OxiMemPool_InfoLog
        std::cout << "[Pool][INIT] capacity=" << m_count
                  << " slot_size=" << SlotSize
                  << " align=" << SlotAlign << "\n";
#endif
    }

public:
    /**
     * @brief Construct a MemoryPool with the given capacity.
     *
     * @param count Maximum number of objects the pool can hold (must be > 0).
     * @throws std::runtime_error or invokes error callback if count == 0.
     */
    explicit MemoryPool(size_t count)
        : m_count(count)
    {
        if (count == 0)
            ReportError("Size of pool cannot be 0", 0);
        Init();
    }

#ifdef OxiMemPool_ErrCallback
    /**
     * @brief Construct a MemoryPool and register an error callback.
     *
     * @param count Capacity of the pool.
     * @param errCallback User-provided callback invoked on errors.
     */
    explicit MemoryPool(size_t count, ErrorCallback errCallback)
        : m_count(count), m_errCallback(errCallback)
    {
        if (count == 0)
            ReportError("Size of pool cannot be 0", 0);
        Init();
    }
#endif

    MemoryPool(const MemoryPool &) = delete;            /**< Non-copyable. */
    MemoryPool &operator=(const MemoryPool &) = delete; /**< Non-copy-assignable. */

    /**
     * @brief Destructor — frees the raw storage and asserts there are no live objects
     * in debug builds.
     *
     * The destructor will call ::operator delete with alignment. In debug builds
     * an assertion checks that Used() == 0 to help catch leaks where objects
     * were not returned to the pool.
     */
    ~MemoryPool() noexcept
    {
#ifdef OxiMemPool_InfoLog
        std::cout << "[Pool][DESTROY] used=" << m_used
                  << " max_allocated=" << m_max_allocated << "\n";
#endif
#ifndef NDEBUG
        assert(Used() == 0 && "MemoryPool destroyed with live objects");
#endif
        ::operator delete(m_pool, std::align_val_t{SlotAlign});
    }

    /**
     * @brief Get the configured capacity of the pool.
     * @return Number of slots configured for this pool.
     */
    size_t Capacity() const noexcept { return m_count; }

    /**
     * @brief Get the number of slots that have ever been allocated from raw
     * storage (high-water index).
     *
     * When thread-safety is enabled, this getter takes the internal mutex to
     * provide a stable value.
     *
     * @return The maximum index + 1 that has been allocated so far.
     */
    size_t MaxAllocated() const noexcept
    {
#ifdef OxiMemPool_ThreadSafe
        std::lock_guard<std::mutex> guard(m);
#endif
        return m_max_allocated;
    }

    /**
     * @brief Number of currently constructed/checked-out objects.
     * @return Count of objects that must be returned before pool destruction
     *         (or Available() increases accordingly).
     */
    size_t Used() const noexcept
    {
#ifdef OxiMemPool_ThreadSafe
        std::lock_guard<std::mutex> guard(m);
#endif
        return m_used;
    }

    /**
     * @brief Number of available slots (capacity - used).
     *
     * Note: this value may change immediately after returning from this call
     * in multi-threaded environments.
     *
     * @return Number of slots that are not currently in use.
     */
    size_t Available() const noexcept
    {
#ifdef OxiMemPool_ThreadSafe 
        std::lock_guard<std::mutex> guard(m); 
#endif 
        return m_count - m_used; 
    } 
}; 