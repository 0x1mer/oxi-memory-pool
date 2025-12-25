//=====================================================//
//                                                     //
//   ██████╗ ██╗  ██╗ ██╗███╗   ███╗███████╗██████╗    //
//   ██╔═██║  ██╗██╔╝ ██║████╗ ████║██╔════╝██╔══██╗   //
//   ██████║   ███╔╝  ██║██╔████╔██║█████╗  ██████╔╝   //
//   ██╔═██║  ██╔██╗  ██║██║╚██╔╝██║██╔══╝  ██╔═██╗    //
//   ██████║ ██╔╝ ██╗ ██║██║ ╚═╝ ██║███████╗██║  ██╗   //
//   ╚═════╝ ╚═╝  ╚═╝ ╚═╝╚═╝     ╚═╝╚══════╝╚═╝  ╚═╝   //
//                                                     //
//                    0x1mer                           //
//=====================================================//

/**
* @file ObjectPool.hpp
* @brief Simple fixed-capacity object pool with RAII handles.
*
* This header provides a lightweight, header-only object pool for allocating
* many objects of the same type T from a single preallocated memory block.
* Objects are constructed in-place and automatically returned to the pool
* via an RAII wrapper (PoolHandle).
*
* Features:
* - Predictable memory usage (single allocation at construction time)
* - Move-only RAII handle for automatic object lifetime management
* - Optional logging via user-provided LogFunction
* - Optional basic thread-safety via OxiMemPool_ThreadSafe (single mutex)
* - Optional user-defined error callback via OxiMemPool_ErrCallback
* - Proper alignment and efficient storage reuse via a singly-linked free list
*
* Notes:
* - The pool stores raw memory and explicitly constructs/destructs objects of T
* using std::construct_at and std::destroy_at.
* - T must satisfy std::destructible.
* - When thread-safety is enabled, pool operations are serialized by a single mutex.
*
* @author 0x1mer
* @license MIT
*/

// library source: https://github.com/0x1mer/oxi-memory-pool

#pragma once

#include <cstddef>    // size_t, std::byte
#include <cstdint>    // std::uintptr_t
#include <new>        // ::operator new/delete, std::align_val_t
#include <memory>     // std::construct_at, std::destroy_at, std::launder
#include <cassert>    // assert
#include <utility>    // std::forward
#include <stdexcept>  // std::runtime_error
#include <string>     // std::string, std::to_string
#include <atomic>     // std::atomic
#include <limits>     // std::numeric_limits




using LogFunction = void (*)(const std::string&);

#ifdef OxiMemPool_ThreadSafe
#include <mutex>      // std::mutex, std::lock_guard, std::unique_lock
#endif

#ifdef OxiMemPool_ErrCallback
using ErrorCallback = void (*)(const char*, size_t);
#endif

/**
 * ObjectPool manages a preallocated block of raw memory divided into fixed-size slots
 * and provides fast allocation/free for objects of type T.
 * Objects are constructed only on emplace() and memory is released only when the pool
 * itself is destroyed.
 *
 * Requirement: T must satisfy std::destructible (see requires clause).
 */
template <typename T>
    requires std::destructible<T>
class ObjectPool;

/**
 * PoolHandle is a lightweight RAII wrapper for an object allocated from ObjectPool.
 * When the handle is destroyed, the object's destructor is called and the slot is
 * returned back to the pool.
 * Copying is disabled; move semantics are supported (move-only type).
 */
template <typename T>
    requires std::destructible<T>
class PoolHandle
{
private:
    ObjectPool<T>* pool_ = nullptr; // owning pool
    T* object_ = nullptr;           // managed object

    friend class ObjectPool<T>;

    PoolHandle() noexcept = default;

    PoolHandle(ObjectPool<T>& pool, T* object) noexcept
        : pool_(&pool), object_(object) {}

    /**
     * Internal handle destruction routine.
     * Destroys the managed object and clears internal state.
     * noexcept is required since this is used from the destructor.
     */
    void destroy_handle()
    {
        if (!pool_ || !object_)
            return;

        if (pool_->log_function_)
        {
            pool_->log_function_(
                "[PoolHandle][DESTROY] object=" +
                std::to_string(reinterpret_cast<std::uintptr_t>(object_)) +
                "\n");
        }

        pool_->destroy_object(object_);
        pool_ = nullptr;
        object_ = nullptr;
    }

public:
    PoolHandle(const PoolHandle&) = delete;
    PoolHandle& operator=(const PoolHandle&) = delete;

    PoolHandle(PoolHandle&& other) noexcept
        : pool_(other.pool_), object_(other.object_)
    {
        other.pool_ = nullptr;
        other.object_ = nullptr;
    }

    PoolHandle& operator=(PoolHandle&& other) noexcept
    {
        if (this != &other)
        {
            destroy_handle();
            pool_ = other.pool_;
            object_ = other.object_;
            other.pool_ = nullptr;
            other.object_ = nullptr;
        }
        return *this;
    }

    ~PoolHandle() noexcept
    {
        destroy_handle();
    }

    void reset() noexcept { destroy_handle(); }

    T* get() const noexcept { return object_; }
    T& operator*() const noexcept { return *object_; }
    T* operator->() const noexcept { return object_; }
    explicit operator bool() const noexcept { return object_ != nullptr; }
};

/**
 * ObjectPool implementation.
 * Manages raw memory storage and a free-list of available slots.
 * Uses a simple LIFO free-list allocator with optional logging and error callbacks.
 */
template <typename T>
    requires std::destructible<T>
class ObjectPool
{
private:
    struct FreeSlot
    {
        FreeSlot* next = nullptr; // singly-linked free list node
    };

    size_t capacity_;               // maximum number of objects
    std::byte* pool_memory_ = nullptr; // raw memory block

#ifdef OxiMemPool_ErrCallback
    ErrorCallback err_callback_ = nullptr; // optional error callback
#endif

#ifdef OxiMemPool_ThreadSafe
    mutable std::mutex mutex_; // protects free list and related state
#endif

    FreeSlot* free_head_ = nullptr;        // head of free-list
    std::atomic<size_t> used_count_{0};    // number of live objects
    size_t max_allocated_index_ = 0;       // number of slots ever handed out

    LogFunction log_function_ = nullptr;   // optional logging function

    // Slot size and alignment calculation.
    // Each slot must be able to store either T or FreeSlot and satisfy alignment.
    static constexpr size_t kRawSlotSize =
        sizeof(T) > sizeof(FreeSlot) ? sizeof(T) : sizeof(FreeSlot);
    static constexpr size_t kSlotAlign =
        alignof(T) > alignof(FreeSlot) ? alignof(T) : alignof(FreeSlot);
    static constexpr size_t kSlotSize =
        (kRawSlotSize + kSlotAlign - 1) / kSlotAlign * kSlotAlign;

    friend class PoolHandle<T>;

    // Allocate a raw slot without locking.
    // Caller must hold the mutex if thread-safety is enabled.
    // Never throws; returns nullptr if the pool is exhausted.
    T* allocate_no_lock() noexcept
    {
        if (free_head_)
        {
            auto* node = free_head_;
            free_head_ = node->next;

            if (log_function_)
                log_function_("[Pool][ALLOC][REUSE] slot=" +
                              std::to_string(reinterpret_cast<std::uintptr_t>(node)) +
                              "\n");

            return reinterpret_cast<T*>(node);
        }

        if (max_allocated_index_ >= capacity_)
        {
            return nullptr; // pool exhausted
        }

        const size_t idx = max_allocated_index_++;
        std::byte* raw = pool_memory_ + kSlotSize * idx;
        auto* ptr = std::launder(reinterpret_cast<T*>(raw));

        if (log_function_)
            log_function_("[Pool][ALLOC][NEW] slot=" +
                          std::to_string(reinterpret_cast<std::uintptr_t>(ptr)) +
                          " index=" +
                          std::to_string(idx) +
                          "\n");

        return ptr;
    }

    // Return a slot to the free-list without locking.
    // Caller must hold the mutex if thread-safety is enabled.
    void free_no_lock(T* obj) noexcept
    {
        auto* node = reinterpret_cast<FreeSlot*>(obj);
        node->next = free_head_;
        free_head_ = node;

        if (log_function_)
            log_function_("[Pool][FREE] slot=" +
                          std::to_string(reinterpret_cast<std::uintptr_t>(obj)) +
                          "\n");
    }

    /**
     * Destroys an object and returns its slot to the free-list.
     * The destructor of T is executed before acquiring the pool lock
     * (if enabled) to reduce the risk of re-entrancy issues.
     *
     * Requirement: T::~T() must not re-enter this pool in an unsafe manner.
     */
    void destroy_object(T* obj) noexcept
    {
        if (log_function_)
            log_function_("[Pool][OBJ_DTOR] object=" +
                          std::to_string(reinterpret_cast<std::uintptr_t>(obj)) +
                          "\n");

        std::destroy_at(obj);

#ifdef OxiMemPool_ThreadSafe
        std::lock_guard<std::mutex> guard(mutex_);
#endif
        free_no_lock(obj);
        used_count_.fetch_sub(1, std::memory_order_acq_rel);
    }

    void report_error(const char* msg, size_t code)
    {
        if (log_function_)
            log_function_("[Pool][ERROR] " + std::string(msg) +
                          " code=" + std::to_string(code) +
                          "\n");

#ifdef OxiMemPool_ErrCallback
        if (err_callback_)
        {
            err_callback_(msg, code);
            return;
        }
#endif
        throw std::runtime_error(msg);
    }

    void initialize_pool_memory()
    {
        // Overflow check: kSlotSize * capacity_
        if (capacity_ != 0 &&
            kSlotSize > std::numeric_limits<size_t>::max() / capacity_)
        {
            report_error("ObjectPool size overflow", 2);
        }

        const size_t total_bytes = kSlotSize * capacity_;

        pool_memory_ = static_cast<std::byte*>(
            ::operator new(total_bytes, std::align_val_t{kSlotAlign}));

        if (log_function_)
            log_function_("[Pool][INIT] capacity=" +
                        std::to_string(capacity_) +
                        " bytes=" +
                        std::to_string(total_bytes) +
                        "\n");
    }

public:
    explicit ObjectPool(size_t capacity, LogFunction log = nullptr)
        : capacity_(capacity), log_function_(log)
    {
        if (capacity == 0)
            report_error("Pool size cannot be 0", 0);
        initialize_pool_memory();
    }

    ~ObjectPool() noexcept
    {
#ifndef NDEBUG
        assert(used_count_.load(std::memory_order_acquire) == 0 &&
               "ObjectPool destroyed with live objects");
#endif
        ::operator delete(pool_memory_, std::align_val_t{kSlotAlign});
    }

    // Non-copyable, non-movable
    ObjectPool(const ObjectPool&) = delete;
    ObjectPool& operator=(const ObjectPool&) = delete;
    ObjectPool(ObjectPool&&) = delete;
    ObjectPool& operator=(ObjectPool&&) = delete;

#ifdef OxiMemPool_ErrCallback
    void set_error_callback(ErrorCallback cb) noexcept {
        err_callback_ = cb;
    }
#endif

    /**
     * Constructs an object of type T in a free slot and returns a PoolHandle.
     * Strong exception safety: if T's constructor throws, the slot is returned
     * back to the free-list and the exception is propagated.
     */
    template <typename... Args>
    PoolHandle<T> emplace(Args&&... args)
    {
        T* slot = nullptr;

#ifdef OxiMemPool_ThreadSafe
        {
            std::lock_guard<std::mutex> g(mutex_);
            slot = allocate_no_lock();
        }
#else
        slot = allocate_no_lock();
#endif

        if (!slot) {
#ifdef OxiMemPool_ErrCallback
            // If an error callback is set, invoke it and return an empty handle
            if (err_callback_) {
                if (log_function_)
                    log_function_("[Pool][ERROR] exhausted -> calling err_callback\n");
                err_callback_("ObjectPool exhausted", 1);
                return PoolHandle<T>{};
            }
#endif
            report_error("ObjectPool exhausted", 1);
        }

        try {
            std::construct_at(slot, std::forward<Args>(args)...);
        }
        catch (...) {
#ifdef OxiMemPool_ThreadSafe
            std::lock_guard<std::mutex> g(mutex_);
#endif
            free_no_lock(slot);
            throw;
        }

        used_count_.fetch_add(1, std::memory_order_acq_rel);

        return PoolHandle<T>(*this, slot);
    }

    // Current number of live objects
    size_t size() const noexcept { return used_count_.load(std::memory_order_acquire); }

    // Maximum pool capacity
    size_t capacity() const noexcept { return capacity_; }
};