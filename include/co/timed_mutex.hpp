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
struct TimedMutexWaiter
{
   enum class Result
   {
      Succeeded,
      TimedOut
   };

   TimedMutexWaiter()
   {}
   
   template<class Rep, class Period>
   TimedMutexWaiter(boost::asio::io_context& ioc, const std::chrono::duration<Rep, Period>& timeoutDuration)
    : timer{ioc, toBoostPosixTime(timeoutDuration)}
   {
      timer->async_wait([thisPtr = this](boost::system::error_code ec){ 
         if (ec)
            return;
         
         if (*thisPtr)
            thisPtr->result.set_value(Result::TimedOut);
      });
   }
   
   std::atomic<bool> alreadyFinalized{false};
   impl::LightFutureData<Result> result;
   boost::optional<boost::asio::deadline_timer> timer;
   
   explicit operator bool()
   {
      bool expected = false;
      return alreadyFinalized.compare_exchange_strong(expected, true);
   }
};
   
struct TimedHighThroughputPolicy {
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

struct TimedFairPolicy {
    using Waiter = std::deque<TimedMutexWaiter*>;

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

namespace experimental
{
template<typename PolicyT>
class BaseTimedMutex
{
private:
    std::atomic<int> mCountOfWaiter {0};

    boost::synchronized_value<typename PolicyT::Waiter> mWaiter;

    static constexpr auto MemoryOrderLock = std::memory_order_acquire;
    static constexpr auto MemoryOrderUnlock = std::memory_order_release;
    static constexpr auto MemoryOrderFailedLocking = std::memory_order_relaxed;

    bool spinLock()
    {
        for (int i = 0; i < 8; i++) {
            int expected = 0;
            bool res = mCountOfWaiter.compare_exchange_weak(expected, 1, MemoryOrderLock, MemoryOrderFailedLocking);
            assert(expected >=  0);

            if (res)
                return true;

            if (expected > 1)
                break;
        }
       
        std::this_thread::yield();

        auto waitingCount = mCountOfWaiter.fetch_add(1, MemoryOrderLock) + 1;
        if (waitingCount  == 1)
            return true;
        
        return false;
    }
    
public:
    BaseTimedMutex() = default;

    ~BaseTimedMutex()
    {
        assert(mCountOfWaiter == 0);
    }

    void lock()
    {
        if (spinLock())
           return;

        impl::TimedMutexWaiter p;
        PolicyT::addToWaiter(mWaiter, &p);
        auto res = await(p.result);
        assert(res ==  impl::TimedMutexWaiter::Result::Succeeded);
        return;
    }

    bool try_lock()
    {
        int expected = 0;
        bool res = mCountOfWaiter.compare_exchange_weak(expected, 1, MemoryOrderLock, MemoryOrderFailedLocking);
        assert(expected >=  0);
        return res;
    }
    
    template<class Rep, class Period>
    bool try_lock_for(const std::chrono::duration<Rep, Period>& timeoutDuration)
    {
        if (timeoutDuration <=  timeoutDuration.zero())
           return try_lock();
        
        impl::TimedMutexWaiter p{timeoutDuration};
        PolicyT::addToWaiter(mWaiter, &p);
        auto res = await(p.result);
        if (res == impl::TimedMutexWaiter::Result::TimedOut)
        {
           mCountOfWaiter.fetch_sub(1, MemoryOrderFailedLocking);
           return false;
        }
        
        return true;
    }

    /* TODO
    template<class Clock, class Duration>
    bool try_lock_until(const std::chrono::time_point<Clock, Duration>& timeoutTime)
    {
       // should be mapped to try_lock_for
    }
    */
    
    void unlock()
    {
        auto waitingCount = mCountOfWaiter.fetch_sub(1, MemoryOrderUnlock) - 1;
        assert(waitingCount >= 0); // count of unlock() calls is larger than the count of lock() + count of succesfull try_lock() calls
        if (waitingCount == 0)
            return;

        impl::TimedMutexWaiter* next = nullptr;
        while (true) {
            {
                auto lockedWaiters = mWaiter.synchronize();
                if (!lockedWaiters->empty())
                    next = PolicyT::getNextWaiter(lockedWaiters);
            }

            if (*next) // not timed out
               break;
            assert(!next); // the previous check should have reserved it
            
            if (mCountOfWaiter == 0)
               return;

            std::this_thread::yield();
        }

        if (next->timer)
           next->timer->cancel();
        next->result.set_value(impl::TimedMutexWaiter::Result::Succeeded);
    }
};

//using Mutex = BaseMutex<impl::TimedHighThroughputPolicy>;
using TimedMutex = BaseTimedMutex<impl::TimedFairPolicy>;
}
}
