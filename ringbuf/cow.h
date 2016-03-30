#pragma once

#include <memory>
#include <atomic>

template <class T>
struct cow
{
    std::shared_ptr<T const> load()
    {
        return std::atomic_load_explicit(&ptr_, std::memory_order_acquire);
    }

    template <class X>
    void store(std::shared_ptr<X> ptr)
    {
        std::atomic_store_explicit(&ptr_, std::static_pointer_cast<T const>(std::move(ptr)), std::memory_order_release);
    }

    template <class X>
    std::shared_ptr<T const> exchange(std::shared_ptr<X> ptr)
    {
        return std::atomic_exchange_explicit(&ptr_, std::static_pointer_cast<T const>(std::move(ptr)), std::memory_order_acq_rel);
    }

    template <class... Args>
    void store(Args&&... args)
    {
        store(std::make_shared<T>(std::forward<Args>(args)...));
    }

    template <class... Args>
    std::shared_ptr<T const> exchange(Args&&... args)
    {
        return exchange(std::make_shared<T>(std::forward<Args>(args)...));
    }

private:
    std::shared_ptr<T const> ptr_;
}; // cow

} // namespace ufw
