#include <co/mutex.hpp>
#include <co/timed_mutex.hpp>

#include <catch2/catch.hpp>

#include "helper.hpp"

template<typename MutexT>
void testAgainstMutexConcept()
{
    REQUIRE_FALSE(std::is_copy_constructible<MutexT>::value);
    REQUIRE_FALSE(std::is_copy_assignable<MutexT>::value);
    REQUIRE_FALSE(std::is_move_constructible<MutexT>::value);
    REQUIRE_FALSE(std::is_move_assignable<MutexT>::value);

    GIVEN("an unlocked mutex") {
        MutexT mutex;

        WHEN("the mutex is locked") {
            mutex.lock();

            THEN("another try to lock should fail") {
                REQUIRE_FALSE(mutex.try_lock());
            }

            AND_WHEN("the mutex is unlocked") {
                mutex.unlock();

                THEN("locking sould be possible again") {
                    mutex.lock();

                    AND_THEN("another try to lock should fail") {
                        REQUIRE_FALSE(mutex.try_lock());
                    }
                }
            }

            mutex.unlock();
        }

        WHEN("the mutex is tried to be locked") {
            auto aquiredLock = mutex.try_lock();

            THEN("this attempt should succeed") {
                REQUIRE(aquiredLock);

                AND_THEN("another try to lock should fail") {
                    REQUIRE_FALSE(mutex.try_lock());
                }
            }

            AND_WHEN("the mutex is unlocked") {
                mutex.unlock();

                THEN("locking sould be possible again") {
                    auto aquiredLock2 = mutex.try_lock();
                    REQUIRE(aquiredLock2);

                    AND_THEN("another try to lock should fail") {
                        REQUIRE_FALSE(mutex.try_lock());
                    }
                }
            }

            mutex.unlock();
        }

        WHEN("the mutex is locked in a coroutine") {
            auto startThread = std::this_thread::get_id();
            co::Routine([&]() {
                mutex.lock();

                THEN("this should not change the exection thread of the coroutine") {
                    REQUIRE(startThread == std::this_thread::get_id());
                }

                mutex.unlock();
            }).join();
        }

        WHEN("the mutex is destructed without use, this should not fail") {
        }
    }
    
   GIVEN("a already locked mutex") {
        MutexT mutex;
        mutex.lock();
        
        WHEN("the mutex is locked in a coroutine") {
            auto startThread = std::this_thread::get_id();
            co::Routine coro([&]() {
                mutex.lock();

                THEN("the coroutine should be resumed inline / in the same thread") {
                    REQUIRE(startThread == std::this_thread::get_id());
                }

                mutex.unlock();
            });
            
            REQUIRE(coro);
            
            AND_WHEN("the mutex is unlocked outside of a coroutine") {
               mutex.unlock();
            
               coro.join();
            }
        }
        
        WHEN("the mutex is locked in a coroutine") {
            co::IoContextThreads threads{1};
            auto startThread = std::this_thread::get_id();
            
            co::Routine coro([&]() {
                mutex.lock();

                THEN("the coroutine should not be resumed inline (in the same thread)") {
                    REQUIRE_FALSE(startThread == std::this_thread::get_id());
                }

                mutex.unlock();
            });
            
            REQUIRE(coro);
            
            AND_WHEN("the mutex is unlocked in an other coroutine") {
               co::Routine([&]() {
                  mutex.unlock();
               }).join();
            
               coro.join();
            }
        }       
   }   
}

SCENARIO("co::Mutex should fulfill the Mutex concept")
{
   testAgainstMutexConcept<co::Mutex>();
}

SCENARIO("co::experimental::TimedMutex should fulfill the Mutex concept")
{
   testAgainstMutexConcept<co::experimental::TimedMutex>();
}

TEST_CASE("co::Mutex stress tests", "[.StressTest]")
{
   static constexpr size_t TotalCnt = 10000000;
   static constexpr size_t CoroCnt = 5;
   static constexpr size_t LoopCnt = TotalCnt / CoroCnt;
   
   co::Mutex mutex;
   co::IoContextThreads threads{4};
   co_tests::Bench bench;
   
   size_t callCnt = 0;
   size_t threadSwitches = 0;
   auto lastExecThread = std::this_thread::get_id();
   
   auto coroFunc = [&](){
          // Force execution in thread pool
          co::await(co::defaultIoContext());
      
          for (int i = 0; i < LoopCnt; i++)
          {
              std::unique_lock<co::Mutex> lock(mutex);
              callCnt++;
              bench.update();
              
              if (lastExecThread != std::this_thread::get_id())
              {
                  threadSwitches++;
                  lastExecThread = std::this_thread::get_id();  
              }
          }
      };
   
   std::vector<co::Routine> coros;
   coros.reserve(CoroCnt);
   for (int i=0; i < CoroCnt; i++)
       coros.emplace_back(coroFunc);
   
   for (auto& coro : coros)
       co::await(coro);
   
   REQUIRE(callCnt == LoopCnt * CoroCnt);
   REQUIRE(threadSwitches <= LoopCnt * CoroCnt);
   REQUIRE(threadSwitches >= 4);
   std::cout << "Thread switches: "<< threadSwitches << std::endl;
}
