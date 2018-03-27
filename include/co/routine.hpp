#pragma once

#include "future.hpp"
#include "executor.hpp"

#include <boost/coroutine2/all.hpp>
#include <boost/coroutine2/protected_fixedsize_stack.hpp>

#include <boost/variant.hpp>
#include <boost/container/pmr/polymorphic_allocator.hpp>
#include <boost/container/pmr/memory_resource.hpp>
#include <boost/container/pmr/global_resource.hpp>
#include <unordered_map>

namespace co
{

   class Routine
   {
   private:
      using T = void;
      
      using CoRo = boost::coroutines2::coroutine<void>;

      struct Data;
      
   public:
      using allocator_type = boost::container::pmr::polymorphic_allocator<std::uint8_t>;
      using executor_type = boost::asio::io_context::executor_type;
      
      struct PostLeaveFunction
      {
         virtual bool operator()() = 0;
      };

      static constexpr size_t DefaultStackSize = 128 * 1024;
      
      Routine() = default;
      
      Routine(const Routine&) = delete;
      Routine& operator=(const Routine&) = delete;
      
      Routine& operator=(Routine&&) = default;

      template <typename Func, typename = std::enable_if_t<!std::is_convertible<Func, allocator_type>::value>>
      Routine(Func&& func, allocator_type alloc = boost::container::pmr::get_default_resource())
       : Routine(defaultIoContext(), DefaultStackSize, std::forward<Func>(func), alloc)
      {
      }

      template <typename Func, typename = std::enable_if_t<!std::is_convertible<Func, allocator_type>::value>>
      Routine(boost::asio::io_context& context, Func&& func, allocator_type alloc = boost::container::pmr::get_default_resource())
       : Routine(context, DefaultStackSize, std::forward<Func>(func), alloc)
      {
      }

      template <typename Func, typename = std::enable_if_t<!std::is_convertible<Func, allocator_type>::value>>
      Routine(boost::asio::io_context& context, size_t stackSize, Func&& func, allocator_type alloc = boost::container::pmr::get_default_resource())
       : d(Destructor::create(context, stackSize, std::forward<Func>(func), alloc))
      {
      }
      
      Routine(Routine&& other, allocator_type = boost::container::pmr::get_default_resource())
       : d(std::move(other.d))
      {
      }
      
      void detach()
      {
         assert(joinable());
         
         auto expected = false;
         if (d->mIsDetached.compare_exchange_strong(expected, true))
            d.release();
         else
            d.reset();
      }
      
      static std::atomic<Data*>& current();
      
      static boost::asio::io_context& currentIoContext()
      {
         co::Routine::Data* currentCoro = current();
         if (currentCoro)
            return currentCoro->mContext;
         
         return defaultIoContext();
      }
      
      void* get_id() const noexcept
      {
         assert(joinable());
         
         return d.get();
      }
      
      explicit operator bool() const
      {
         if (d->mPull)
            return true;
         return false;
      }

      void join()
      {
         d->mResult.get_blocking();
      }

      bool joinable() const noexcept
      {
         return d != nullptr;
      }
      
      inline void await_synchron()
      {
         assert(joinable());
         
          return join();
      }

      inline bool is_ready()
      {
         assert(joinable());
         
         return d->mResult.is_ready();
      }
      
      inline bool await_ready()
      {
         assert(joinable());
         
         return d->mResult.is_ready_weak();
      }

      inline auto await_resume()
      {
         assert(joinable());
         
         return d->mResult.get_unchecked();
      }

      struct Runner : public impl::ContinuationTask
      {
         std::atomic<Data*> mCaller;
         boost::optional<std::atomic<size_t>> mAwaitableCnt;
         
         explicit Runner(Data* caller)
          : mCaller(caller)
         {}
         
         explicit Runner(Data* caller, size_t awaitableCnt)
          : mCaller(caller)
         {
             mAwaitableCnt.emplace(awaitableCnt);
         }
         
         void operator()()
         {
            Data* curr = current();
            if (curr)
            {
               curr->mContext.post(std::ref(*this));
               return;
            }
            
             if(mAwaitableCnt)
             {
                 if (--(*mAwaitableCnt))
                    return;
             }
            
             auto continuation = mCaller.exchange(reinterpret_cast<Data*>(1), std::memory_order_acquire);
             
             while(continuation)
             {
                if (continuation == reinterpret_cast<Data*>(1))
                   break;
                
                continuation = continuation->resume();
               //  if (continuation)
               //    std::cout << "RUNNER HAS CONTINUATION" << std::endl;
             }
         }
      };

      inline bool await_suspend(Runner& cb)
      {
         assert(joinable());
         
         auto continuation = cb.mCaller.exchange(nullptr, std::memory_order_seq_cst);
         if (!d->mResult.suspend(cb))
             return false;
         
         Data* expected = nullptr;
         if (!d->mContinuation.compare_exchange_strong(expected, continuation))
         {
             assert(expected == reinterpret_cast<Data*>(1));
             while(cb.mCaller != reinterpret_cast<Data*>(1))
                std::this_thread::yield();
             //std::cout  << "case x" << std::endl;
             return false;
         }
         
         return true;
      }
      
      executor_type get_executor()
      {
         assert(joinable());
         
         return d->mContext.get_executor();
      }

   private:
      struct ResultSetter : PostLeaveFunction
      {
         Data* mParent{};
         impl::Value<T> mValue;
         
         ResultSetter(Data* parent)
          : mParent(parent)
         {}

         virtual bool operator()();
      };
      
      struct StackAllocator
      {
         Data* data{};
               
         boost::context::stack_context allocate()
         {
            //std::cout << "stack allocating " << (void*)mBuffer->data() << std::endl;
            boost::context::stack_context res;
            auto pos = reinterpret_cast<std::uint8_t*>(data);
            pos += sizeof(Data);
            pos += data->mStackSize;
            pos -= 1;
            res.sp = pos;
            res.size = data->mStackSize;
            return res;
         }

         void deallocate(boost::context::stack_context& sc)
         {
            //std::cout << "stack deallocating " << (void*)sc.sp << std::endl;
            //assert(mOverwriteCheck == this);
         }
      };
      
      struct Data
      {
         template <typename Func>
         Data(boost::asio::io_context& context, size_t stackSize, Func&& func, boost::container::pmr::polymorphic_allocator<std::uint8_t> alloc)
          : mContext(context), mStackSize{stackSize}, mAllocator(alloc), mLocalStorage(mAllocator), mPull(StackAllocator{this}, [ this, f = std::forward<Func>(func) ](CoRo::push_type & sink) {
               mPush = &sink;
               mOuter = current().exchange(this, std::memory_order_acquire);

               impl::ValueHandling<T>::setByResult(mSetResult.mValue, f);
                  
               auto replaced = mPostLeave.exchange(&mSetResult, std::memory_order_release);
               assert(replaced == nullptr);

               auto exitedCoro = current().exchange(mOuter, std::memory_order_release);
               assert(this == exitedCoro);
         })
         {
            if (!runPostLeave())
               resume();
            assert(current() != this);
         }
         
         ~Data()
         {
            if (mPull)
            {
               std::cerr << "co::Routine still active and not detached!" << std::endl;
               std::terminate();
            }
         }

         static std::atomic<Data*>& current()
         {
            static thread_local std::atomic<Data*> current{};
            return current;
         }

         bool runPostLeave()
         {
            auto postLeave = mPostLeave.exchange(nullptr, std::memory_order_seq_cst);
            if (!postLeave)
               return true;

            return (*postLeave)();
         }

         void leave(PostLeaveFunction* postFunc)
         {
            auto replaced = mPostLeave.exchange(postFunc, std::memory_order_seq_cst);
            assert(replaced == nullptr);
            
            auto exitedCoro = current().exchange(mOuter, std::memory_order_release);
            assert(this == exitedCoro);

            (*mPush)();
         }

         Data* resume()
         {
            do
            {
               mOuter = current().exchange(this, std::memory_order_acquire);

               mPull();
               
               if (!mPull)
               {
                  auto res = mContinuation.exchange(reinterpret_cast<Data*>(1));
                  auto postRes = runPostLeave();
                  assert(postRes);
                  return res;
               }
            } while (!runPostLeave());

            assert(current() != this);
            
            return nullptr;
         }

         boost::asio::io_context& mContext;
         size_t mStackSize{};
         std::atomic<bool> mIsDetached{false};
         boost::container::pmr::polymorphic_allocator<std::uint8_t> mAllocator;
         ResultSetter mSetResult{this};
         std::atomic<PostLeaveFunction*> mPostLeave{};

         impl::LightFutureData<impl::PlaceholderType<T>> mResult;
         std::atomic<Data*> mContinuation{};

         struct StorageItem
         {
            StorageItem() = default;
            
            template<typename T>
            explicit StorageItem(T* t, void (*ownCleanup)(T*))
              : data(t), cleanupFunction([ownCleanup](void* val){ ownCleanup(static_cast<T*>(val)); })
            {
            }
            
            ~StorageItem()
            {
               if (cleanupFunction)
                  cleanupFunction(data);
            }
            
            StorageItem(const StorageItem&) = delete;
            StorageItem& operator=(const StorageItem&) = delete;
            
            StorageItem(StorageItem&& other)
             : data(other.data), cleanupFunction(other.cleanupFunction)
            {
               other.cleanupFunction = std::function<void (void*)>{};
            }
            
            StorageItem& operator=(StorageItem&& other)
            {
               if (cleanupFunction)
                  cleanupFunction(data);
                
               data = other.data;
               cleanupFunction = other.cleanupFunction;

               other.cleanupFunction = std::function<void (void*)>{};
            }
            
            void* data{};
            std::function<void (void*)> cleanupFunction;
         };
               
         template<typename Key, typename T, typename Hash = std::hash<Key>, typename KeyEqual = std::equal_to<Key>>
         using PmrMap = std::unordered_map<Key, T, Hash, KeyEqual, boost::container::pmr::polymorphic_allocator< std::pair<const Key, T> >>;
         
         PmrMap<const void*, StorageItem> mLocalStorage;
         
         CoRo::push_type* mPush{};
         Routine::Data* mOuter{};
         CoRo::pull_type mPull;
      };
      
      struct Destructor
      {
         template<typename Func>
         static std::unique_ptr<Data, Destructor> create(boost::asio::io_context& context, size_t stackSize, Func&& func, boost::container::pmr::polymorphic_allocator<std::uint8_t> alloc)
         {
             Data* mem = reinterpret_cast<Data*>(alloc.allocate(sizeof(Data) + stackSize));
             alloc.construct(mem, context, stackSize, std::forward<Func>(func), alloc);
             return std::unique_ptr<Data, Destructor>(mem, Destructor{});
         }
         
         void operator()(Data* data)
         {
             size_t size = sizeof(Data) + data->mStackSize;
             auto alloc = data->mAllocator;
             alloc.destroy(data);
             alloc.deallocate(reinterpret_cast<std::uint8_t*>(data), size);
         }
      };
      
      template <typename T>
      class SpecificPtr;
      
      std::unique_ptr<Data, Destructor> d;
   };
      
   inline bool Routine::ResultSetter::operator()()
   {    
      {
         Data* expected = nullptr;
         Data* disabled =  reinterpret_cast<Data*>(1);
         mParent->mContinuation.compare_exchange_strong(expected, disabled, std::memory_order_relaxed);
         if (impl::isException(mValue))
            mParent->mResult.set_exception(std::move(boost::get<std::exception_ptr>(mValue)));
         else
            mParent->mResult.set_value(std::move(boost::get<impl::PlaceholderType<T>>(mValue)));
      }
         
      {   
         bool expected = false;
         if (!mParent->mIsDetached.compare_exchange_strong(expected, true, std::memory_order_relaxed))
         {
            auto data = mParent;
            size_t size = sizeof(Data) + data->mStackSize;
            auto alloc = data->mAllocator;
            alloc.destroy(data);
            alloc.deallocate(reinterpret_cast<std::uint8_t*>(data), size);
         }
      }
         
      return true;
   }      
      
   inline std::atomic<Routine::Data*>& Routine::current()
   {
      return Data::current();
   }

}
