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

#include <cstddef>     // std::byte
#include <new>         // operator new, std::align_val_t
#include <cassert>     // assert
#include <utility>     //std::forward
#include <type_traits> //std::destructible

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
 * The pool supports optional compile-time logging (InfoLog) and an optional
 * simple mutex-based thread-safety mode (ThreadSafe). In ThreadSafe mode,
 * a single std::mutex protects the free list and accounting members.
 *
 * @tparam T Type of object stored in the pool. Must be Destructible.
 */
template <typename T>
    requires std::destructible<T>
class MemoryPool
{
private:
    /**
     * @brief Node used for free-list linking.
     *
     * When a slot is free its storage is reinterpreted as a FreeNode and linked
     * into m_free_head. The node contains only a pointer to the next free node.
     */
    struct FreeNode
    {
        FreeNode *next = nullptr; /**< Next free node in the free-list. */
    };

    size_t m_count;     /**< Total number of slots in the pool (capacity). */
    std::byte *m_pool;  /**< Pointer to the contiguous block of pool storage. */

#ifdef ThreadSafe
    mutable std::mutex m; /**< Mutex protecting free-list and accounting in ThreadSafe mode. */
#endif

    // All of this only under mutex!!! (in thread safe version)
    FreeNode *m_free_head = nullptr; /**< Head of the singly-linked free-list. */
    size_t m_used = 0;               /**< Number of currently live objects. */
    size_t m_max_allocated = 0;      /**< Number of slots handed out linearly so far. */

    /**
     * @brief Size calculations for each slot.
     *
     * RawSlotSize is the maximum of sizeof(T) and sizeof(FreeNode) to guarantee
     * the slot can hold either the object or the free-list node. SlotAlign is
     * the maximum alignment required by T or FreeNode. SlotSize is RawSlotSize
     * rounded up to the SlotAlign boundary.
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
     * @brief Destroy object located at \p obj and push its slot to free-list.
     *
     * Performs explicit destructor call for T, then converts the storage into
     * a FreeNode and pushes it onto the free-list. Decrements m_used.
     *
     * This method is called by MemoryPoolObject::Destroy() when the wrapper
     * leaves scope or is reset.
     *
     * @param obj Pointer to object to destroy (must be a pointer into pool storage).
     */
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
    /**
     * @brief Allocate raw storage for a new object (does NOT construct it).
     *
     * First attempts to pop a slot from the free-list. If free-list is empty,
     * assigns the next slot from the linear region of m_pool. Asserts if the
     * pool capacity is exhausted (debug build).
     *
     * @return Pointer to raw storage suitable for placement-new of T.
     */
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

    /**
     * @brief Construct an object of type T in a freshly allocated slot.
     *
     * Allocates a slot via Allocate(), performs placement-new with forwarded
     * arguments, and returns a pointer to the constructed object. If construction
     * throws, the allocated slot is returned to the free-list and the exception
     * is propagated.
     *
     * @tparam Args Parameter pack for T constructor.
     * @param args Arguments forwarded to T constructor.
     * @return Pointer to the constructed T (std::launder applied).
     * @throws Rethrows exceptions thrown by T's constructor.
     */
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
    /**
     * @brief Construct a MemoryPool with fixed number of slots.
     *
     * Allocates an aligned block of memory able to store \p count objects
     * (each slot sized to SlotSize and aligned to SlotAlign).
     *
     * @param count Number of slots (capacity) in the pool.
     */
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

    MemoryPool(const MemoryPool &) = delete;            /**< Non-copyable. */
    MemoryPool &operator=(const MemoryPool &) = delete; /**< Non-copyable. */

    /**
     * @brief Destructor.
     *
     * In debug builds asserts that no objects remain live (Used() == 0).
     * Frees the underlying storage block with the appropriate alignment.
     */
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

    /**
     * @brief Create a new object in the pool and return an owning wrapper.
     *
     * Constructs a T in-place in the pool using forwarded arguments and
     * increments the used counter. Returns a MemoryPoolObject<T> which will
     * automatically destroy the object and return the slot to the pool when
     * it goes out of scope or is reset.
     *
     * @tparam Args Parameter pack for T constructor.
     * @param args Arguments forwarded to T constructor.
     * @return MemoryPoolObject<T> RAII wrapper owning the new object.
     */
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

    /**
     * @brief Get pool capacity (number of slots).
     *
     * @return The capacity specified at construction.
     */
    size_t Capacity() const noexcept { return m_count; }

    /**
     * @brief Get number of slots that were ever allocated linearly.
     *
     * This number is non-decreasing and indicates how many unique slots have
     * been handed out from the pool's contiguous region (not counting reused slots).
     *
     * @return m_max_allocated (thread-safe under ThreadSafe).
     */
    size_t MaxAllocated() const noexcept
    {
#ifdef ThreadSafe
        std::lock_guard<std::mutex> guard(m);
        return m_max_allocated;
#else
        return m_max_allocated;
#endif
    }

    /**
     * @brief Get number of currently live (constructed) objects.
     *
     * @return Number of live objects (thread-safe under ThreadSafe).
     */
    size_t Used() const noexcept
    {
#ifdef ThreadSafe
        std::lock_guard<std::mutex> guard(m);
        return m_used;
#else
        return m_used;
#endif
    }

    /**
     * @brief Compute number of available slots remaining.
     *
     * Available = Capacity() - Used().
     *
     * @return Number of slots available for new objects.
     */
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