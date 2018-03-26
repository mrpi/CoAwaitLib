#pragma once

#include "future.hpp"
#include "await.hpp"

#include <atomic>
#include <vector>

#include <boost/thread/synchronized_value.hpp>
#include <boost/container/small_vector.hpp>

namespace co
{
namespace impl
{
struct HighThroughputPolicy {
    using FutureValueT = impl::StatelessT;
    using FuturePromiseT = impl::LightFutureData<FutureValueT>;
    using Waiter = std::vector<FuturePromiseT*>;

    template<typename ContainerT, typename T>
    static void addToWaiter(ContainerT&& c, T&& t)
    {
        c->push_back(std::forward<T>(t));
    }

    template<typename ContainerT>
    static auto getNextWaiter(ContainerT&& c)
    {
        auto next = std::move(c->back());
        c->pop_back();
        return next;
    }
};

struct FairPolicy {
    using FutureValueT = impl::StatelessT;
    using FuturePromiseT = impl::LightFutureData<FutureValueT>;
    using Waiter = std::deque<FuturePromiseT*>;

    template<typename ContainerT, typename T>
    static void addToWaiter(ContainerT&& c, T&& t)
    {
        c->push_back(std::forward<T>(t));
    }

    template<typename ContainerT>
    static auto getNextWaiter(ContainerT&& c)
    {
        auto next = std::move(c->front());
        c->pop_front();
        return next;
    }
};
}

template<typename PolicyT>
class BaseMutex
{
private:
    std::atomic<int> mCountOfWaiter {0};

    boost::synchronized_value<typename PolicyT::Waiter> mWaiter;

    static constexpr auto MemoryOrderLock = std::memory_order_acquire;
    static constexpr auto MemoryOrderUnlock = std::memory_order_release;
    static constexpr auto MemoryOrderFailedLocking = std::memory_order_relaxed;

public:
    BaseMutex() = default;

    ~BaseMutex()
    {
        assert(mCountOfWaiter == 0);
    }

    void lock()
    {
        for (int i = 0; i < 8; i++) {
            int expected = 0;
            bool res = mCountOfWaiter.compare_exchange_weak(expected, 1, MemoryOrderLock, MemoryOrderFailedLocking);
            assert(expected >=  0);

            if (res)
                return;

            if (expected > 1)
                break;
        }

        std::this_thread::yield();

        auto waitingCount = mCountOfWaiter.fetch_add(1, MemoryOrderLock) + 1;
        if (waitingCount  == 1)
            return;

        // TODO: remove allocations (atleast for the comman case
        typename PolicyT::FuturePromiseT p;
        PolicyT::addToWaiter(mWaiter, &p);
        await(p);
    }

    bool try_lock()
    {
        int expected = 0;
        bool res = mCountOfWaiter.compare_exchange_weak(expected, 1, MemoryOrderLock, MemoryOrderFailedLocking);
        assert(expected >=  0);
        return res;
    }

    void unlock()
    {
        auto waitingCount = mCountOfWaiter.fetch_sub(1, MemoryOrderUnlock) - 1;
        assert(waitingCount >= 0); // count of unlock() calls is larger than the count of lock() + count of succesfull try_lock() calls
        if (waitingCount == 0)
            return;

        typename PolicyT::FuturePromiseT* next = nullptr;
        while (true) {
            {
                auto lockedWaiters = mWaiter.synchronize();
                if (!lockedWaiters->empty()) {
                    next = PolicyT::getNextWaiter(lockedWaiters);
                    break;
                }
            }

            std::this_thread::yield();
        }

        next->set_value(typename PolicyT::FutureValueT{});
    }
};

//using Mutex = BaseMutex<impl::HighThroughputPolicy>;
using Mutex = BaseMutex<impl::FairPolicy>;
}
