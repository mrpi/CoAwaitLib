#include <co/foreach.hpp>
#include <co/generate.hpp>

#include <set>

#include <catch2/catch.hpp>

#include "helper.hpp"

SCENARIO("co::forEach usage on random-access range")
{
   constexpr size_t coroCnt = 4;
   constexpr size_t itmCnt = 10000 / coroCnt * coroCnt + 1;
   static_assert(itmCnt % coroCnt, "Item count should not be devideable by the coroutine count");

   GIVEN("a vector with many item")
   {
      std::vector<int> v(itmCnt);

      WHEN("calling co::forEach() from a normal thread with an io_context that is running in some "
           "threads")
      {
         boost::asio::io_context context;
         co::IoContextThreads threads{4, context};

         co::forEach(context, co::MaxParallelity{coroCnt}, v, [&, cnt = 0](int& i) mutable {
            i++;
            if (++cnt % 100 == 0)
               co::await(context);
         });

         THEN("the function should have been called exactly once for every item")
         {
            for (const int& i : v)
               REQUIRE(i == 1);
         }
      }

      WHEN("calling co::forEach() from a coroutine that is associated to a io_context that is only "
           "running in the main thread")
      {
         boost::asio::io_context context;
         auto mainThread = std::this_thread::get_id();
         std::set<std::thread::id> usedThreads;

         co::Routine r{context, [&]() {
                          co::forEach(context, co::MaxParallelity{coroCnt}, v,
                                      [&, cnt = 0](int& i) mutable {
                                         i++;
                                         usedThreads.insert(std::this_thread::get_id());
                                         if (++cnt % 100 == 0)
                                            co::await(context);
                                      });
                       }};

         context.run();
         r.join();

         THEN("the function should only have been executed on this thread")
         {
            REQUIRE(usedThreads == std::set<std::thread::id>{mainThread});
         }

         THEN("the function should have been called exactly once for every item")
         {
            for (const int& i : v)
               REQUIRE(i == 1);
         }
      }
   }
}

TEST_CASE("co::forEach on input range")
{
   boost::asio::io_context context;
   co::IoContextThreads threads{4, context};

   auto generator = co::generate<int>(context, [](auto& yield) mutable {
      for (int i = 0; i < 100; i++)
      {
         if (!yield(i))
            FAIL("Receiver closed unexpectedly!");
      }
   });

   std::atomic<int> out{};

   auto runForEach = [&]() {
      co::forEach(context, co::MaxParallelity{4}, generator, [&out](int val) { out += val + 1; });
   };

   SECTION("Called from coroutine")
   {
      co::Routine{context, runForEach}.join();
      REQUIRE(out == 5050);
   }

   SECTION("Called from regular thread")
   {
      runForEach();
      REQUIRE(out == 5050);
   }
}
