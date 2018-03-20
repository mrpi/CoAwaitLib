#include "future.hpp"

#include <boost/coroutine2/all.hpp>
#include <boost/coroutine2/protected_fixedsize_stack.hpp>

#include <boost/variant.hpp>

namespace co
{
   struct InlineBufferStackAllocator
   {
      std::vector<std::uint8_t>* mBuffer;
      // void* mOverwriteCheck{this};
      
      InlineBufferStackAllocator(std::vector<std::uint8_t>& buffer)
        : mBuffer(&buffer)
      {
      }
      
      boost::context::stack_context allocate()
      {
          //std::cout << "stack allocating " << (void*)mBuffer->data() << std::endl;
          boost::context::stack_context res;
          res.sp = mBuffer->data() + mBuffer->size() - 1;
          res.size = mBuffer->size();
          return res;
      }

      void deallocate(boost::context::stack_context& sc)
      {
          //std::cout << "stack deallocating " << (void*)sc.sp << std::endl;
          //assert(mOverwriteCheck == this);
      }
   };
   
   class Routine
   {
   private:
      using T = void;
      
      using CoRo = boost::coroutines2::coroutine<void>;

   public:
      struct PostLeaveFunction
      {
         virtual bool operator()() = 0;
      };

      static auto& current()
      {
         static thread_local std::atomic<Routine*> current{};
         return current;
      }

      template <typename StackAllocator, typename Func>
      Routine(StackAllocator& alloc, Func&& func)
       : mPull(alloc, [ this, f = std::forward<Func>(func) ](CoRo::push_type & sink) {
            mPush = &sink;
            mOuter = current().exchange(this, std::memory_order_acquire);

            impl::ValueHandling<T>::setByResult(mSetResult.mValue, f);
               
#define USE_RESULTSETTER
#ifdef USE_RESULTSETTER
            auto replaced = mPostLeave.exchange(&mSetResult, std::memory_order_release);
            assert(replaced == nullptr);
#endif

            auto exitedCoro = current().exchange(mOuter, std::memory_order_release);
            assert(this == exitedCoro);
      })
      {
         if (!runPostLeave())
            resume();
         assert(current() != this);
      }

      ~Routine()
      {
         if (mPull)
         {
            std::cout << "Coro still active" << std::endl;
            std::abort();
         }
      }
      
      explicit operator bool() const
      {
         if (mPull)
            return true;
         return false;
      }

      Routine* resume()
      {
         do
         {
            mOuter = current().exchange(this, std::memory_order_acquire);

            mPull();
            
            if (!mPull)
            {
                auto res = mContinuation.exchange(reinterpret_cast<Routine*>(1));
                auto postRes = runPostLeave();
                assert(postRes);
                return res;
            }
         } while (!runPostLeave());

         assert(current() != this);
         
         return nullptr;
      }

      void leave(PostLeaveFunction* postFunc)
      {
         auto replaced = mPostLeave.exchange(postFunc, std::memory_order_seq_cst);
         assert(replaced == nullptr);
         
         auto exitedCoro = current().exchange(mOuter, std::memory_order_release);
         assert(this == exitedCoro);

         (*mPush)();
      }

      bool runPostLeave()
      {
         auto postLeave = mPostLeave.exchange(nullptr, std::memory_order_seq_cst);
         if (!postLeave)
            return true;

         return (*postLeave)();
      }

      auto get()
      {
         return mResult.get_blocking();
      }
      
      inline auto await_synchron()
      {
          return get();
      }

      inline bool is_ready()
      {
         return mResult.is_ready();
      }
      
      inline bool await_ready()
      {
         return mResult.is_ready_weak();
      }

      inline auto await_resume()
      {
         return mResult.get_unchecked();
      }

      struct Runner : public impl::ContinuationTask
      {
         std::atomic<Routine*> mCaller;
         
         explicit Runner(Routine* caller)
          : mCaller(caller)
         {}
         
         void operator()()
         {
             auto continuation = mCaller.exchange(reinterpret_cast<Routine*>(1), std::memory_order_acquire);
             
             while(continuation)
             {
                if (continuation == reinterpret_cast<Routine*>(1))
                   break;
                
                continuation = continuation->resume();
               //  if (continuation)
               //    std::cout << "RUNNER HAS CONTINUATION" << std::endl;
             }
         }
      };

      inline bool await_suspend(Runner& cb)
      {
         auto continuation = cb.mCaller.exchange(nullptr, std::memory_order_seq_cst);
         if (!mResult.suspend(cb))
             return false;
         
         Routine* expected = nullptr;
         if (!mContinuation.compare_exchange_strong(expected, continuation))
         {
             assert(expected == reinterpret_cast<Routine*>(1));
             while(cb.mCaller != reinterpret_cast<Routine*>(1))
                std::this_thread::yield();
             //std::cout  << "case x" << std::endl;
             return false;
         }
         
         return true;
      }

   private:
#ifdef USE_RESULTSETTER
      struct ResultSetter : PostLeaveFunction
      {
         Routine* mParent{};
         impl::Value<T> mValue;
         
         ResultSetter(Routine* parent)
          : mParent(parent)
         {}
         
         virtual bool operator()()
         {                              
               Routine* expected = nullptr;
               Routine* disabled =  reinterpret_cast<Routine*>(1);
               mParent->mContinuation.compare_exchange_strong(expected, disabled, std::memory_order_relaxed);
               if (impl::isException(mValue))
                  mParent->mResult.set_exception(std::move(boost::get<std::exception_ptr>(mValue)));
               else
                  mParent->mResult.set_value(std::move(boost::get<impl::PlaceholderType<T>>(mValue)));
               return true;
         }
      };
      
      ResultSetter mSetResult{this};
#endif
      std::atomic<PostLeaveFunction*> mPostLeave{};

      impl::LightFutureData<impl::PlaceholderType<T>> mResult;
      std::atomic<Routine*> mContinuation{};

      CoRo::push_type* mPush{};
      Routine* mOuter{};
      CoRo::pull_type mPull;
   };

}
