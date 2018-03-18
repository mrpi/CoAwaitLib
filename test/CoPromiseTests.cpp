#include <catch.hpp>

#include <co/future.hpp>

#include <thread>

TEST_CASE("co::promise")
{
   co::promise<int> promise;
   auto future = promise.get_future();
   REQUIRE(future.valid());
   REQUIRE_FALSE(future.is_ready());
      
   SECTION("set_value() synchron")
   {
      promise.set_value(23);
      
      REQUIRE(future.is_ready());       
      REQUIRE(future.get() == 23);
   }
   
   SECTION("set_value() in std::thread")
   {
      std::thread t{[&promise]() { promise.set_value(42); }};
      
      // The following check could fail sporadically as the thread may have executed already
      // REQUIRE_FALSE(future.is_ready());
      
      REQUIRE(future.get() == 42);
      t.join();
   }
}
