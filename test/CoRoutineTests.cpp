#include <co/await.hpp>

#include <catch.hpp>

std::vector<std::uint8_t> stackBuf1(boost::context::stack_traits::default_size() * 2);
std::vector<std::uint8_t> stackBuf2(boost::context::stack_traits::default_size(), 0);
std::vector<std::uint8_t> stackBuf3(boost::context::stack_traits::default_size(), 0);

TEST_CASE("co::Routine: await sleep on boost::asio::io_service")
{
   using namespace std::literals;
   
   boost::asio::io_service ios;
   boost::optional<boost::asio::io_service::work> iosWork{ios};
   
   std::thread t{[&ios](){ ios.run(); }};
   
   co::InlineBufferStackAllocator bufAlloc{stackBuf1};
   
   co::Routine coro{bufAlloc, [&ios]() {
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
   coro.get();
   
   iosWork.reset();
   t.join();
}

TEST_CASE("co::Routine: await sleep on boost::asio::io_service directly with chrono type")
{
   using namespace std::literals;
   
   co::IoContextThreads threads{1};
   
   co::InlineBufferStackAllocator bufAlloc{stackBuf1};
   
   co::Routine coro{bufAlloc, []() {
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
   coro.get();
}

TEST_CASE("co::Routine: await boost::asio::io_service (switch to a thread that is executing io_service::run())")
{
   boost::asio::io_service ios;
   boost::optional<boost::asio::io_service::work> iosWork{ios};
   
   std::thread t{[&ios](){ ios.run(); }};
   
   co::InlineBufferStackAllocator bufAlloc{stackBuf1};
   
   co::Routine coro{bufAlloc, [&ios]() {
      auto startThread = std::this_thread::get_id();

      co::await(ios);

      auto endThread = std::this_thread::get_id();
      REQUIRE(endThread != startThread);
   }};
   coro.get();
   
   iosWork.reset();
   t.join();
}

inline auto now() { return boost::posix_time::microsec_clock::local_time(); }

class Bench
{
private:
   boost::posix_time::ptime mStartTime;
   size_t mIdx{0};
   size_t mTotal{0};
   static constexpr size_t BlockSize = 1024 * 16;
   
public:
   Bench()
    : mStartTime(now())
   {}
   
   void update()
   {
       mTotal++;
       if (mIdx++ != BlockSize)
          return;
     
       mIdx = 0;
       auto endTime = now();
       
       auto runtime = endTime - mStartTime;
       auto perSecond = static_cast<double>(BlockSize) / runtime.total_milliseconds() * 1000.0;
       std::cout << "#" << std::setw(8) << mTotal << " (" << std::setw(10) << std::setprecision(2) << std::fixed << perSecond << " per second)" << std::endl;
       
       mStartTime = now();
   }
};

TEST_CASE("co::Routine: coroutine in coroutine")
{
   using namespace std::literals;
   
   static_assert(co::supportsSynchronAwait<co::Routine&>, "");
   
   co::IoContextThreads threads{2};
   auto& ios = co::defaultIoContext();
   
   std::cout << "buf1: " << (void*)stackBuf1.data() << " with size " << stackBuf1.size() << std::endl;
   co::InlineBufferStackAllocator bufAllocOuter{stackBuf1};
   std::cout << "buf2: " << (void*)stackBuf2.data() << " with size " << stackBuf2.size() << std::endl;
   co::InlineBufferStackAllocator bufAllocInner1{stackBuf2};
   std::cout << "buf3: " << (void*)stackBuf3.data() << " with size " << stackBuf3.size() << std::endl;
   co::InlineBufferStackAllocator bufAllocInner2{stackBuf3};
   
   SECTION("with inner coroutine empty")
   {
      bool processed = false;
      co::Routine{bufAllocOuter, [&]() {
         co::await(co::Routine{bufAllocInner1, [](){}});
         processed = true;
      }}.get();
      
      REQUIRE(processed);
   }
   
   SECTION("with inner coroutine awaiting")
   {
      bool processed = false;
      co::Routine{bufAllocOuter, [&]() {
         co::await(co::Routine{bufAllocInner1, [&](){
            co::await(ios);
            processed = true;
         }});
      }}.get();
      
      REQUIRE(processed);
   }
   
   SECTION("with one inner coroutine", "[.benchmark]")
   {
      constexpr size_t LoopCnt = 250000;
      
      size_t calls{};
      std::set<std::thread::id> outerCoRoEndThreads;
      
      std::cout << "io_context: " << (void*)&co::defaultIoContext() << std::endl;
      
      Bench bench;
      for (int i=0; i < LoopCnt; i++)
      {
         bench.update();
         
         co::Routine outerCoRo{bufAllocOuter, [&]() {
            auto func = [&](){
                  for(int i=0; i < 1; i++)
                     co::await(ios);
               };

            co::Routine innerCoRo1{bufAllocInner1, func};
            co::await(innerCoRo1);
            calls++;
            outerCoRoEndThreads.insert(std::this_thread::get_id());
         }};
         
         co::await(outerCoRo);
         
         REQUIRE(calls == i+1);
      }
      
      REQUIRE(outerCoRoEndThreads.size() >= 1);
      REQUIRE(outerCoRoEndThreads.size() <= 3);
   }
   
   SECTION("with multiple inner coroutines", "[.benchmark]")
   {
      constexpr size_t LoopCnt = 250000;
      
      std::cout << "io_context: " << (void*)&co::defaultIoContext() << std::endl;
      
     size_t calls{};
      
      Bench bench;
      for (int i=0; i < LoopCnt; i++)
      {
         bench.update();
         
         co::Routine outerCoRo{bufAllocOuter, [&]() {
            auto func = [&](){
                  for(int i=0; i < 1; i++)
                     co::await(ios);
            };
               
            co::Routine innerCoRo1{bufAllocInner1, func};
            co::Routine innerCoRo2{bufAllocInner2, func};            

            co::await(innerCoRo1);
            co::await(innerCoRo2);

            calls++;
         }};
         
         co::await(outerCoRo);
         
         REQUIRE(calls == i+1);
      }
   }
   
   SECTION("with inner coroutine (always ready)", "[.benchmark]")
   {
      constexpr size_t LoopCnt = 250000;
      size_t calls{};
      
      Bench bench;
      for (int i=0; i < LoopCnt; i++)
      {
         bench.update();
         
         co::Routine outerCoRo{bufAllocOuter, [&]() {
            co::Routine innerCoRo{bufAllocInner1, [&](){
                  for(int i=0; i < 1; i++)
                     co::await(ios);
               }};
            
            while(!innerCoRo.is_ready())
               ;
               
            co::await(innerCoRo);

            calls++;
         }};
         
         outerCoRo.get();
         
         REQUIRE(calls == i+1);
      }
   }
}
