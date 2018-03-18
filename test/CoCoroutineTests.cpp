#include <co/await.hpp>

#include <catch.hpp>

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
   coro.get();
   
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
   coro.get();
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
   
   static boost::asio::io_service ioc;
   co::IoContextThreads threads{2, ioc};
   
#if 0
   SECTION("with inner coroutine empty")
   {
      bool processed = false;
      co::Routine{[&]() {
         co::await(co::Routine{[](){}});
         processed = true;
      }}.get();
      
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
      }}.get();
      
      REQUIRE(processed);
   }
#endif
   
   SECTION("with one inner coroutine")
   {
      constexpr size_t LoopCnt = 250000;
      
      size_t calls{};
      std::set<std::thread::id> outerCoRoEndThreads;
      
      std::cout << "io_context: " << (void*)&co::defaultIoContext() << std::endl;
      
      Bench bench;
      for (int i=0; i < LoopCnt; i++)
      {
         bench.update();
         
         co::Routine outerCoRo{[&]() {
            auto func = [&](){
                  for(int i=0; i < 1; i++)
                     co::await(ioc);
               };

            co::Routine innerCoRo1{func};
            co::await(innerCoRo1);
            calls++;
            outerCoRoEndThreads.insert(std::this_thread::get_id());
         }};
         
         co::await(outerCoRo);
         
         REQUIRE(calls == i+1);
      }
      
      REQUIRE(outerCoRoEndThreads.size() == 3);
   }
   
   SECTION("with multiple inner coroutines")
   {
      constexpr size_t LoopCnt = 250000000;
      
      std::cout << "io_context: " << (void*)&co::defaultIoContext() << std::endl;
      
     size_t calls{};
      
      Bench bench;
      for (int i=0; i < LoopCnt; i++)
      {
         bench.update();
         
         co::Routine outerCoRo{[&]() {
            auto func = [&](){
                  for(int i=0; i < 1; i++)
                     co::await(ioc);
            };
               
            co::Routine innerCoRo1{func};
            co::Routine innerCoRo2{func};            

            co::await(innerCoRo1);
            co::await(innerCoRo2);

            calls++;
         }};
         
         co::await(outerCoRo);
         
         REQUIRE(calls == i+1);
      }
   }
   
#if 0
   SECTION("with on inner coroutine (always ready)")
   {
      constexpr size_t LoopCnt = 20000;
     size_t calls{};
      
      for (int i=0; i < LoopCnt; i++)
      {
         if ((i+1 & 0x3F) == 0x3F)
            std::cout << "#" << i << std::endl;
         
         co::Routine outerCoRo{[&]() {
            co::Routine innerCoRo{[&](){
                  for(int i=0; i < 1; i++)
                     // co::await(co::asioSleep(ios, 50ms));
                     co::await(ios);
               }};
            
            while(!innerCoRo.await_ready())
               ;
               
            co::await(innerCoRo);

            calls++;
         }};
         
         outerCoRo.get();
         
         REQUIRE(calls == i+1);
      }
   }
#endif
}
