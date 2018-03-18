#include "coroutine.hpp"

#include <boost/optional.hpp>

namespace co
{
   template <typename T>
   class Awaiter
   {
   private:
      T& mValue;

   public:
      inline Awaiter(T& value) : mValue(value)
      {
      }

      inline bool await_ready()
      {
         return mValue.await_ready();
      }

      inline auto await_resume()
      {
         return mValue.await_resume();
      }

      template <typename HANDLE>
      inline bool await_suspend(HANDLE&& cb)
      {
         return mValue.await_suspend(std::forward<HANDLE>(cb));
      }
   };

   template <>
   class Awaiter<boost::asio::io_service>
   {
   private:
      boost::asio::io_service& mIos;

   public:
      Awaiter(boost::asio::io_service& ios) : mIos{ios}
      {
      }

      bool await_ready()
      {
         return false;
      }

      void await_resume()
      {
      }

      template <typename Handle>
      bool await_suspend(Handle&& cb)
      {
         mIos.post(std::ref(cb));
         return true;
      }
   };
   
   template <class Rep, class Period>
   struct AsioSleep
   {
      boost::asio::io_service& ioService;
      std::chrono::duration<Rep, Period> sleepFor;
   };
   
   template <class Rep, class Period>
   auto asioSleep(boost::asio::io_service& ioService, std::chrono::duration<Rep, Period> value)
   {
       return AsioSleep<Rep, Period>{ioService, std::move(value)};
   }
   
   namespace impl
   {
   template <class Rep, class Period>
   boost::posix_time::time_duration toBoostPosixTime(const std::chrono::duration<Rep, Period>& dur)
   {
      return boost::posix_time::microseconds(std::chrono::duration_cast<std::chrono::microseconds>(dur).count());
   }
   }

   template <class Rep, class Period>
   class Awaiter<AsioSleep<Rep, Period>>
   {
   private:
      std::chrono::duration<Rep, Period> mValue;
      boost::asio::deadline_timer mTimer;

   public:
      Awaiter(AsioSleep<Rep, Period>& value) : mValue{std::move(value.sleepFor)}, mTimer{value.ioService, impl::toBoostPosixTime(mValue)}
      {
      }

      bool await_ready()
      {
         return mValue < std::chrono::duration<Rep, Period>{};
      }

      constexpr void await_resume()
      {
      }

      template <typename Handle>
      bool await_suspend(Handle&& cb)
      {
         mTimer.async_wait([&cb](boost::system::error_code /*ec*/) { cb(); });
         return true;
      }
   };

   template <typename T>
   auto await(T&& awaitable) // -> decltype(std::declval_t<Awaiter<std::decay_t<decltype(awaitable)>>>().await_resume())
   {
      using AwaiterT = Awaiter<std::decay_t<decltype(awaitable)>>;

      AwaiterT awaiter{awaitable};
      if (awaiter.await_ready())
         return awaiter.await_resume();

      auto current = Routine::current().load();
      if (!current)
         return awaiter.await_resume();
      
      struct PostLeave : public Routine::PostLeaveFunction
      {
         AwaiterT& mAw;
         Routine::Runner mRunner;

         PostLeave(AwaiterT& aw, Routine& caller) : mAw(aw), mRunner(&caller)
         {
         }

         bool operator()() override
         {
            return mAw.await_suspend(mRunner);
         }
      };

      PostLeave pl{awaiter, const_cast<Routine&>(*current)};
      current->leave(&pl);

      return awaiter.await_resume();
   }
}
