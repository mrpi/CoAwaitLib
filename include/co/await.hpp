#include "coroutine.hpp"

#include <boost/optional.hpp>

namespace co
{
   using IoContextProvider = boost::asio::io_service& (*)();
   
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
      
      inline boost::asio::io_service& defaultIoContextProvider()
      {
          static boost::asio::io_service ioc;
          return ioc;
      }
      
      inline IoContextProvider& currentIoContextProvider()
      {
          static IoContextProvider provider{&defaultIoContextProvider};
          return provider;
      }
   }
   
   inline void setDefaultIoContextProvider(IoContextProvider ioContextProvider)
   {
       impl::currentIoContextProvider() = ioContextProvider;
   }
   
   inline boost::asio::io_service& defaultIoContext()
   {
       return impl::currentIoContextProvider()();
   }
   
   class IoContextThreads
   {
   private:
      boost::optional<boost::asio::io_service::work> mIosWork;
      std::vector<std::thread> mThreads;
               
   public:
      explicit IoContextThreads(size_t cnt, boost::asio::io_service& ioc = defaultIoContext())
       : mIosWork{ioc}, mThreads(cnt)
      {
          if (ioc.stopped())
             ioc.reset();
         
          for (auto& t : mThreads)
             t = std::thread{[&ioc](){ ioc.run(); }};
      }
      
      IoContextThreads(const IoContextThreads&) = delete;
      IoContextThreads& operator=(const IoContextThreads&) = delete;
      
      ~IoContextThreads()
      {
         mIosWork.reset();
   
         for (auto& t : mThreads)
            t.join();

      }
   };
      
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
      return boost::posix_time::microseconds{std::chrono::duration_cast<std::chrono::microseconds>(dur).count()};
   }
   }

   template <class Rep, class Period>
   class Awaiter<AsioSleep<Rep, Period>>
   {
   private:
      std::chrono::duration<Rep, Period> mValue;
      boost::asio::deadline_timer mTimer;

   public:
      Awaiter(AsioSleep<Rep, Period> value) : mValue{std::move(value.sleepFor)}, mTimer{value.ioService, impl::toBoostPosixTime(mValue)}
      {
      }

      bool await_ready()
      {
         return mValue < std::chrono::duration<Rep, Period>{};
      }
      
      void await_synchron()
      {
          std::this_thread::sleep_for(mValue);
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

   template <class Rep, class Period>
   class Awaiter<std::chrono::duration<Rep, Period>> : public Awaiter<AsioSleep<Rep, Period>>
   {
   public:
      Awaiter(const std::chrono::duration<Rep, Period>& val)
       : Awaiter<AsioSleep<Rep, Period>>(asioSleep(defaultIoContext(), val))
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

      auto current = Routine::current().load();
      if (!current)
      {
         SupportsSynchronAwait<T> synchron;
         return synchron(awaiter);
      }
      
      struct PostLeave : public Routine::PostLeaveFunction
      {
         AwaiterFor<T>& mAw;
         Routine::Runner mRunner;

         PostLeave(AwaiterFor<T>& aw, Routine& caller) : mAw(aw), mRunner(&caller)
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
