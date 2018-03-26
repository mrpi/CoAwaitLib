#pragma once

#include <atomic>
#include <stdexcept>
#include <thread>
#include <cassert>
#include <future>

#include <boost/asio.hpp>
#include <boost/thread.hpp>
#include <boost/date_time.hpp>
#include <boost/variant.hpp>

namespace co
{
namespace impl
{
struct Unset{};

template<typename T>
struct ValueHandling
{
    using PlaceholderType = T;
    using Type = boost::variant<Unset, PlaceholderType, std::exception_ptr>;
   
    template<typename Callable>
    static void setByResult(Type& value, Callable&& f)
    {
        try
        {
            value = f();
        }
        catch(...)
        {
            value = std::current_exception();
        }
    }
    
    static T get(Type& value)
    {
        if (value.which() == 2)
           std::rethrow_exception(boost::get<std::exception_ptr>(value));
        return boost::get<T>(value);
    }
};

struct StatelessT
{
};   

template<>
struct ValueHandling<void>
{
    using PlaceholderType = StatelessT;
    using Type = boost::variant<Unset, PlaceholderType, std::exception_ptr>;
   
    template<typename Callable>
    static void setByResult(Type& value, Callable&& f)
    {
        try
        {
            f();
            value = StatelessT{};
        }
        catch(...)
        {
            value = std::current_exception();
        }
    }
    
    static void get(Type& value)
    {
        if (value.which() == 2)
           std::rethrow_exception(boost::get<std::exception_ptr>(value));
    }
};

template<>
struct ValueHandling<StatelessT>
{
    using PlaceholderType = StatelessT;
    using Type = boost::variant<Unset, PlaceholderType, std::exception_ptr>;
   
    template<typename Callable>
    static void setByResult(Type& value, Callable&& f)
    {
        try
        {
            value = f();
        }
        catch(...)
        {
            value = std::current_exception();
        }
    }
    
    static void get(Type& value)
    {
        if (value.which() == 2)
           std::rethrow_exception(boost::get<std::exception_ptr>(value));
    }
};

template<typename T>
using Value = typename ValueHandling<T>::Type;

template<typename T>
using PlaceholderType = typename ValueHandling<T>::PlaceholderType;

template<typename T>
inline bool isSet(const boost::variant<Unset, T, std::exception_ptr>& val)
{
    return val.which() != 0;
}

template<typename T>
inline bool isException(const boost::variant<Unset, T, std::exception_ptr>& val)
{
    return val.which() == 2;
}

template<typename Callable>
using ResultType = decltype(std::declval<Callable>()());

template<typename Callable>
using HandlerForResult = ValueHandling<ResultType<Callable>>;
   
struct ContinuationTask
{
   virtual void operator()() = 0;
};

class ConditionVariableTask : public ContinuationTask
{
 private:
   boost::condition_variable mCond;
   boost::mutex mMut;
   bool mDataReady{false};

 public:
   void operator()() override
   {
      {
         boost::unique_lock<boost::mutex> lock(mMut);
         mDataReady = true;
      }
      mCond.notify_one();
   }

   void wait()
   {
      boost::unique_lock<boost::mutex> lock(mMut);
      mCond.wait(lock, [this]
                {
                   return mDataReady;
                });
   }

   template <class Rep, class Period>
   bool wait_for(const std::chrono::duration<Rep, Period>& timeoutDuration)
   {
      boost::unique_lock<boost::mutex> lock(mMut);
      return mCond.wait_for(lock, timeoutDuration, [this]
                           {
                              return mDataReady;
                           });
   }
};

constexpr ContinuationTask* EmptyHandle{nullptr};
constexpr uintptr_t InvalidHandle{0x1};

template <typename T>
class LightFutureData
{
public:
   LightFutureData() {}

   template<typename T1>
   LightFutureData(T1&& t)
    : continuationPtr{reinterpret_cast<impl::ContinuationTask*>(InvalidHandle)}, value{std::forward<T1>(t)}
   {
   }

   inline bool is_ready() const
   {
      return continuationPtr.load(std::memory_order_acquire) == reinterpret_cast<impl::ContinuationTask*>(impl::InvalidHandle);
   }

   inline bool is_ready_weak() const
   {
      static_assert(sizeof(continuationPtr) == sizeof(impl::InvalidHandle), "std::atomic<> should not add overhead");
      return !memcmp(&continuationPtr, &impl::InvalidHandle, sizeof(void*));
   }
   
   inline bool await_ready()
   {
       return is_ready_weak();
   }
   
   inline bool await_suspend(ContinuationTask& func)
   {
      return suspend(func);
   }
   
   inline auto await_resume()
   {
      return get_unchecked();
   }
   
   inline auto get_unchecked()
   {
      assert(is_ready_weak());
      return ValueHandling<T>::get(value);
   }

   auto get_blocking()
   {
      if (!is_ready_weak())
         wait();
      return get_unchecked();
   }

   bool suspend(impl::ContinuationTask& func)
   {
      auto expected = impl::EmptyHandle;
      bool res = continuationPtr.compare_exchange_strong(expected, &func, std::memory_order_acq_rel, std::memory_order_acq_rel);
      
      // Suspended for the second time?
      assert(res || expected == reinterpret_cast<impl::ContinuationTask*>(impl::InvalidHandle));
      
      return res;
   }
   
   void wait()
   {
      int cnt{0};

      do
      {
         if (is_ready())
            return;

         std::this_thread::yield();
      } while (cnt++ < 4);

      impl::ConditionVariableTask task;
      if (suspend(task))
         task.wait();
   }

   template <class Rep, class Period>
   std::future_status wait_for(const std::chrono::duration<Rep, Period>& timeout_duration)
   {
      if (is_ready())
         return std::future_status::ready;

      impl::ConditionVariableTask task;
      if (suspend(task))
      {
         if (!task.wait_for(timeout_duration))
         {
            auto state = continuationPtr.exchange(impl::EmptyHandle);
            if (state == &task)
               return std::future_status::timeout;
            else
               // Suspended for the second time
               assert(state == reinterpret_cast<impl::ContinuationTask*>(impl::InvalidHandle));
         }
      }

      return std::future_status::ready;
   }

   void set_value(T val)
   {
      value = std::move(val);
      on_ready();
   }

   void set_exception(std::exception_ptr except)
   {
      value = std::move(except);
      on_ready();
   }

private:
   inline void on_ready()
   {
      auto continuation = reinterpret_cast<impl::ContinuationTask*>(impl::InvalidHandle);
      continuation = continuationPtr.exchange(continuation, std::memory_order_acq_rel);
      if (continuation)
      {
         // check if set two times
         assert(continuation != reinterpret_cast<impl::ContinuationTask*>(impl::InvalidHandle));
         (*continuation)();
      }
   }

   std::atomic<ContinuationTask*> continuationPtr{nullptr};
   Value<T> value{};
};
}

template <typename T>
class future
{
 private:
   std::shared_ptr<impl::LightFutureData<impl::PlaceholderType<T>>> mData;

   future(std::shared_ptr<impl::LightFutureData<impl::PlaceholderType<T>>>&& data) : mData(std::move(data)) {}
   
 public:
   future() = default;
   
   static future fromData_(std::shared_ptr<impl::LightFutureData<impl::PlaceholderType<T>>> data)
   {
      return future{std::move(data)};
   }

   bool is_ready() const
   {
      return mData->is_ready();
   }

   constexpr bool await_ready()
   {
      return false; // handled by return value of await_suspend()
   }

   inline T await_resume()
   {
      return get_unchecked();
   }
   
   inline T await_synchron()
   {
      return get();
   }

   inline T get_unchecked()
   {
      return mData->get_unchecked();
   }

   inline T get()
   {
      return mData->get_blocking();
   }

   inline bool valid() const
   {
      return mData != nullptr;      
   }

   void wait() const
   {
      return mData->wait();
   }

#if false
   template <typename FUNC>
   auto then(FUNC&& func) -> decltype(func(future<T>&&))
   {
      auto funcPtr =
          std::make_shared<std::decay_t<FUNC>>(std::forward<FUNC>(func));
      struct ThenContinuation : public impl::ContinuationTask
      {
         FUNC func;
         std::shared_ptr<ThenContinuation> self;

         void operator()() override
         {
            func();
            self.reset();
         }
      };
   }
#endif

   template <class Rep, class Period>
   std::future_status wait_for(const std::chrono::duration<Rep, Period>& timeout_duration) const
   {
      return mData->wait_for(timeout_duration);
   }

   bool await_suspend(impl::ContinuationTask& func) const
   {
      return mData->suspend(func);
   }
};

template <typename T>
class promise
{
 private:
   std::shared_ptr<impl::LightFutureData<T>> mData;

 public:
   inline promise() : mData{std::make_shared<impl::LightFutureData<T>>()} {}

   future<T> get_future() { return future<T>::fromData_(mData); }

   void set_value(T value)
   {      
      mData->set_value(std::move(value));
   }

   void set_exception(std::exception_ptr except)
   {
      mData->set_exception(std::move(except));
   }
};

template <>
class promise<void>
{
 private:
   std::shared_ptr<impl::LightFutureData<impl::StatelessT>> mData;

 public:
   inline promise() : mData{std::make_shared<impl::LightFutureData<impl::StatelessT>>()} {}

   future<void> get_future() { return future<void>::fromData_(mData); }

   void set_value()
   {
      mData->set_value(impl::StatelessT{});
   }

   void set_exception(std::exception_ptr except)
   {
      mData->set_exception(std::move(except));
   }
};

template <typename FUNC>
auto async(boost::asio::io_service& ioService, FUNC&& func)
{
   promise<decltype(func())> p;
   auto f = p.get_future();

   ioService.post([ p = std::move(p), func = std::forward<FUNC>(func) ]() mutable
                  {
                     p.set_value(func());
                  });

   return f;
}

template <typename T>
auto make_ready_future(T&& val)
{
   auto data = std::make_shared<impl::LightFutureData<impl::PlaceholderType<T>>>(std::forward<T>(val));
   return future<T>::fromData_(std::move(data));
}

inline future<void> make_ready_future()
{
   auto data = std::make_shared<impl::LightFutureData<impl::PlaceholderType<void>>>(impl::PlaceholderType<void>{});
   return future<void>::fromData_(std::move(data));
}

template <typename T>
auto make_exceptional_future(std::exception_ptr ex)
{
   auto data = std::make_shared<impl::LightFutureData<impl::PlaceholderType<T>>>();
   data->set_exception(std::move(ex));
   return future<T>::fromData_(std::move(data));
}

template <typename T, typename E>
auto make_exceptional_future(E ex)
{
   return make_exceptional_future<T>(std::make_exception_ptr(ex));
}
}
