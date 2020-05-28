#pragma once

#include "routine.hpp"

#include <boost/optional.hpp>

namespace co
{
   namespace impl
   {
      template <typename T>
      class DefaultAwaiter
      {
      protected:
         T& mValue;

      public:
         inline DefaultAwaiter(T& value) : mValue(value)
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
      
      template<typename T, typename = void>
      struct HasAwaitSynchronMethod : std::false_type
      {
      };

      template<typename T>
      struct HasAwaitSynchronMethod<T, std::void_t<decltype(std::declval<T>().await_synchron())>>  : std::true_type
      {
      };
   }
      
   template <typename T, typename = void>
   class Awaiter : public impl::DefaultAwaiter<T>
   {
   public:
      using impl::DefaultAwaiter<T>::DefaultAwaiter;
   };
   
   template <typename T>
   class Awaiter<T, std::enable_if_t<impl::HasAwaitSynchronMethod<T>::value>> : public impl::DefaultAwaiter<T>
   {
   public:
      using impl::DefaultAwaiter<T>::DefaultAwaiter;
      
      inline auto await_synchron()
      {
          return this->mValue.await_synchron();
      }
   };

   template <>
   class Awaiter<boost::asio::io_context>
   {
   private:
      boost::asio::io_context& mIoc;

   public:
      Awaiter(boost::asio::io_context& ioc) : mIoc{ioc}
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
         mIoc.post(std::ref(cb));
         return true;
      }
   };

   namespace impl
   {
      template <class Rep, class Period>
      boost::posix_time::time_duration toBoostPosixTime(const std::chrono::duration<Rep, Period>& dur)
      {
         return boost::posix_time::microseconds{ std::chrono::duration_cast<std::chrono::microseconds>(dur).count() };
      }
   }

   template <class Rep, class Period>
   boost::asio::deadline_timer asioSleep(boost::asio::io_context& context, std::chrono::duration<Rep, Period> value)
   {
      return boost::asio::deadline_timer{context, impl::toBoostPosixTime(value)};
   }
   

   inline void throwError(const boost::system::error_code& error, const char* msg)
   {
      if (error)
         throw boost::system::system_error(error, msg);
   }

   template<>
   class Awaiter<boost::asio::deadline_timer>
   {
   private:
      boost::asio::deadline_timer& mTimer;
      boost::system::error_code mTimeoutError;

   public:
      Awaiter(boost::asio::deadline_timer& timer) : mTimer{ timer }
      {
      }

      bool await_ready()
      {
         return mTimer.expires_from_now() < boost::posix_time::time_duration{};
      }
      
      void await_synchron()
      {
         std::this_thread::sleep_for(std::chrono::microseconds{ mTimer.expires_from_now().total_microseconds() });
      }

      void await_resume()
      {
         throwError(mTimeoutError, "async_wait");
      }

      template <typename Handle>
      bool await_suspend(Handle&& cb)
      {
         mTimer.async_wait([&cb, this](boost::system::error_code ec) { 
            mTimeoutError = ec;
            cb();
         });
         return true;
      }
   };

   template <class Rep, class Period>
   class Awaiter<std::chrono::duration<Rep, Period>> : public Awaiter<boost::asio::deadline_timer>
   {
   public:
      Awaiter(const std::chrono::duration<Rep, Period>& val)
       : Awaiter<boost::asio::deadline_timer>(asioSleep(co::Routine::currentIoContext(), val))
      {}       
   };
   
   template<typename T>
   using AwaiterFor = Awaiter<std::decay_t<T>>;
   
   template<typename T, typename = void>
   struct SupportsSynchronAwait : std::false_type
   {
       auto operator()(AwaiterFor<T>& awaiter)
       {
           throw std::runtime_error("The given type can only be awaited inside of a coroutine!");
           return awaiter.await_resume();
       }
   };

   template<typename T>
   struct SupportsSynchronAwait<T, std::void_t<decltype(std::declval<AwaiterFor<T>>().await_synchron())>>  : std::true_type
   {
       auto operator()(AwaiterFor<T>& awaiter)
       {
           return awaiter.await_synchron();
       }
   };
      
   template<typename T>
   constexpr bool supportsSynchronAwait = SupportsSynchronAwait<T>::value;
   
   template <typename T>
   auto await(T&& awaitable) // -> decltype(std::declval_t<Awaiter<std::decay_t<decltype(awaitable)>>>().await_resume())
   {
      assert(supportsSynchronAwait<T> || Routine::current());
      
      AwaiterFor<T> awaiter{awaitable};
      if (awaiter.await_ready())
         return awaiter.await_resume();

      Routine::Data* current = Routine::current();
      if (!current)
      {
         SupportsSynchronAwait<T> synchron;
         return synchron(awaiter);
      }
      
      struct PostLeave : public Routine::PostLeaveFunction
      {
         AwaiterFor<T>& mAw;
         Routine::Runner mRunner;

         PostLeave(AwaiterFor<T>& aw, Routine::Data& caller) : mAw(aw), mRunner(&caller)
         {
         }

         bool operator()() override
         {
            return mAw.await_suspend(mRunner);
         }
      };

      PostLeave pl{awaiter, const_cast<Routine::Data&>(*current)};
      current->leave(&pl);

      return awaiter.await_resume();
   }
   
   template<typename T>
   using PmrVector = std::vector<T, boost::container::pmr::polymorphic_allocator<T>>;

}
