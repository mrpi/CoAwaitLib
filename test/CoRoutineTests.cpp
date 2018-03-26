#include <co/await.hpp>

#include <catch.hpp>

#include <boost/container/pmr/synchronized_pool_resource.hpp>
#include <boost/container/pmr/vector.hpp>
#include <boost/container/flat_map.hpp>
#include <boost/container/small_vector.hpp>
#include <boost/container/pmr/vector.hpp>

#include "helper.hpp"

class MultiBufferPoolsMemoryResource : public boost::container::pmr::memory_resource
{
private:
   boost::container::pmr::memory_resource* mUpstream;
   boost::container::flat_map<size_t, boost::container::small_vector<void*, 4>> mPools;
   size_t mMaxSize{0};
   
public:
   MultiBufferPoolsMemoryResource(boost::container::pmr::memory_resource* upstream = boost::container::pmr::get_default_resource())
    : mUpstream(upstream)
   {}
   
   ~MultiBufferPoolsMemoryResource()
   {
       for(auto& p : mPools)
       {
           auto size = p.first;
           for(auto& buf : p.second)
              mUpstream->deallocate(buf, size, 8);
       }
   }
   
    virtual void* do_allocate(std::size_t bytes, std::size_t alignment)
    {
        assert(alignment <= 8); // other alignments have to be supported in destructor too
       
        auto& list = mPools[bytes];
        if (list.empty())
           return mUpstream->allocate(bytes, 8);
        
        auto res = list.back();
        list.pop_back();
        return res;
    }
    
    virtual void do_deallocate(void* p, std::size_t bytes, std::size_t alignment)
    {
        auto& entry = mPools[bytes];
        entry.push_back(p);
        auto s = entry.size();
        if (s > mMaxSize)
        {
           mMaxSize = s;
           std::cout << "Max size of pool " << bytes << " is " << s << std::endl;
        }
    }
    
    virtual bool do_is_equal(const boost::container::pmr::memory_resource& other) const noexcept
    {
        return this == &other;
    }
};

TEST_CASE("co::Routine: await sleep on boost::asio::io_service")
{
   using namespace std::literals;
   
   boost::asio::io_service ios;
   boost::optional<boost::asio::io_service::work> iosWork{ios};
   
   std::thread t{[&ios](){ ios.run(); }};
   
   co::Routine coro{[&ios]() {
      auto startThread = std::this_thread::get_id();

      co::await(co::asioSleep(ios, 1ms));

      auto middleThread = std::this_thread::get_id();

      co::await(co::asioSleep(ios, 1ms));

      auto endThread = std::this_thread::get_id();

      REQUIRE(middleThread != startThread);
      REQUIRE(endThread != startThread);
      
      // there is only one thread executing io_service::run()
      REQUIRE(middleThread == endThread);
   }};
   coro.join();
   
   iosWork.reset();
   t.join();
}

TEST_CASE("co::Routine: await sleep on boost::asio::io_service directly with chrono type")
{
   using namespace std::literals;
   
   co::IoContextThreads threads{1};
   
   co::Routine coro{[]() {
      auto startThread = std::this_thread::get_id();

      co::await(1ms);

      auto middleThread = std::this_thread::get_id();

      co::await(1ms);

      auto endThread = std::this_thread::get_id();

      REQUIRE(middleThread != startThread);
      REQUIRE(endThread != startThread);
      
      // there is only one thread executing io_service::run()
      REQUIRE(middleThread == endThread);
   }};
   coro.join();
}

TEST_CASE("co::Routine: await boost::asio::io_service (switch to a thread that is executing io_service::run())")
{
   boost::asio::io_service ios;
   boost::optional<boost::asio::io_service::work> iosWork{ios};
   
   std::thread t{[&ios](){ ios.run(); }};

   co::Routine coro{[&ios]() {
      auto startThread = std::this_thread::get_id();

      co::await(ios);

      auto endThread = std::this_thread::get_id();
      REQUIRE(endThread != startThread);
   }};
   coro.join();
   
   iosWork.reset();
   t.join();
}

TEST_CASE("co::Routine: coroutine in coroutine")
{
   using namespace std::literals;
   
   static_assert(co::supportsSynchronAwait<co::Routine&>, "");
   
   co::IoContextThreads threads{2};
   auto& ios = co::defaultIoContext();
   
   SECTION("with inner coroutine empty")
   {
      MultiBufferPoolsMemoryResource memoryPool;

      bool processed = false;
      co::Routine{[&]() {
         co::await(co::Routine{[](){}});
         processed = true;
      }}.join();
      
      REQUIRE(processed);
   }
   
   SECTION("with inner coroutine awaiting")
   {
      bool processed = false;
      co::Routine{[&]() {
         co::await(co::Routine{[&](){
            co::await(ios);
            processed = true;
         }});
      }}.join();
      
      REQUIRE(processed);
   }
}

TEST_CASE("co::Routine: coroutine in coroutine stress tests", "[.StressTest]")
{
   using namespace std::literals;
   
   static_assert(co::supportsSynchronAwait<co::Routine&>, "");
   
   co::IoContextThreads threads{2};
   auto& ios = co::defaultIoContext();
   
   SECTION("with one inner coroutine")
   {
      MultiBufferPoolsMemoryResource memoryPool;
      constexpr size_t LoopCnt = 250000;
      
      size_t calls{};
      std::set<std::thread::id> outerCoRoEndThreads;
      
      std::cout << "io_context: " << (void*)&co::defaultIoContext() << std::endl;
      
      co_tests::Bench bench;
      for (int i=0; i < LoopCnt; i++)
      {
         bench.update();
         
         co::Routine outerCoRo{[&]() {
            auto func = [&](){
                  for(int i=0; i < 1; i++)
                     co::await(ios);
               };

            co::Routine innerCoRo1{func};
            co::await(innerCoRo1);
            calls++;
            outerCoRoEndThreads.insert(std::this_thread::get_id());
         }, &memoryPool};
         
         co::await(outerCoRo);
         
         REQUIRE(calls == i+1);
      }
      
      REQUIRE(outerCoRoEndThreads.size() >= 1);
      REQUIRE(outerCoRoEndThreads.size() <= 3);
   }
   
   SECTION("with multiple inner coroutines")
   {
      MultiBufferPoolsMemoryResource memoryPool;
      constexpr size_t LoopCnt = 250000;
      
      std::cout << "io_context: " << (void*)&co::defaultIoContext() << std::endl;
      
     size_t calls{};
      
      co_tests::Bench bench;
      for (int i=0; i < LoopCnt; i++)
      {
         bench.update();
         
         co::Routine outerCoRo{[&]() {
            auto func = [&](){
                  for(int i=0; i < 1; i++)
                     co::await(ios);
            };
               
            co::PmrVector<co::Routine> innerCoRos(&memoryPool);
            innerCoRos.reserve(2);
            
            for (int i1=0; i1 < 2; i1++)
               innerCoRos.emplace_back(func);

            co::await(innerCoRos[0]);
            co::await(innerCoRos[1]);

            calls++;
         }, &memoryPool};
         
         co::await(outerCoRo);
         
         REQUIRE(calls == i+1);
      }
   }
   
   SECTION("with inner coroutine (always ready)")
   {
      MultiBufferPoolsMemoryResource memoryPool;
      constexpr size_t LoopCnt = 250000;
      size_t calls{};
      
      co_tests::Bench bench;
      for (int i=0; i < LoopCnt; i++)
      {
         bench.update();
         
         co::Routine outerCoRo{[&]() {
            co::Routine innerCoRo{[&](){
                  for(int i=0; i < 1; i++)
                     co::await(ios);
               }, &memoryPool};
            
            while(!innerCoRo.is_ready())
               ;
               
            co::await(innerCoRo);

            calls++;
         }, &memoryPool};
         
         outerCoRo.join();
         
         REQUIRE(calls == i+1);
      }
   }
}
