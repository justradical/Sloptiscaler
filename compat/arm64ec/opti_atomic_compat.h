/*
 * std::atomic<std::shared_ptr<T>> shim.
 *
 * C++20 (P0718) added a std::atomic specialisation for std::shared_ptr, which
 * MSVC's STL implements. libc++ does NOT -- it still falls through to the
 * primary template, which static_asserts that T is trivially copyable. This is
 * a libc++ gap, unrelated to ARM64EC.
 *
 * OptiScaler only ever calls load(), store() and exchange() on these, so a
 * small mutex-backed stand-in is behaviourally equivalent. It is slower than a
 * real lock-free atomic, but these are control-plane objects touched on
 * latency-tech init/teardown and once per frame, not in a hot inner loop.
 */
#ifndef OPTISCALER_COMPAT_ATOMIC_H
#define OPTISCALER_COMPAT_ATOMIC_H

#ifdef __cplusplus

#include <atomic>
#include <memory>
#include <mutex>

#ifdef _MSC_VER

template <class T> using OptiAtomicSharedPtr = std::atomic<std::shared_ptr<T>>;

#else

template <class T> class OptiAtomicSharedPtr
{
    mutable std::mutex m_;
    std::shared_ptr<T> p_;

  public:
    OptiAtomicSharedPtr() = default;
    OptiAtomicSharedPtr(std::shared_ptr<T> p) : p_(std::move(p)) {}

    OptiAtomicSharedPtr(const OptiAtomicSharedPtr&) = delete;
    OptiAtomicSharedPtr& operator=(const OptiAtomicSharedPtr&) = delete;

    std::shared_ptr<T> load(std::memory_order = std::memory_order_seq_cst) const
    {
        std::lock_guard<std::mutex> lock(m_);
        return p_;
    }

    void store(std::shared_ptr<T> v, std::memory_order = std::memory_order_seq_cst)
    {
        std::lock_guard<std::mutex> lock(m_);
        p_ = std::move(v);
    }

    std::shared_ptr<T> exchange(std::shared_ptr<T> v, std::memory_order = std::memory_order_seq_cst)
    {
        std::lock_guard<std::mutex> lock(m_);
        p_.swap(v);
        return v;
    }

    OptiAtomicSharedPtr& operator=(std::shared_ptr<T> v)
    {
        store(std::move(v));
        return *this;
    }

    operator std::shared_ptr<T>() const { return load(); }
};

#endif /* _MSC_VER */

#endif /* __cplusplus */

#endif /* OPTISCALER_COMPAT_ATOMIC_H */
