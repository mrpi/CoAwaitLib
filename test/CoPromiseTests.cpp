#include <catch2/catch.hpp>

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
      std::mutex mutex;
      std::unique_lock<std::mutex> lock{mutex};

      std::thread t{[&promise, &mutex]() {
         {
            std::unique_lock<std::mutex> lock{mutex};
         }
         promise.set_value(42);
      }};

      REQUIRE_FALSE(future.is_ready());
      lock.unlock();

      REQUIRE(future.get() == 42);
      t.join();
   }
}

TEST_CASE("co::promise<void>")
{
   co::promise<void> promise;
   auto future = promise.get_future();
   REQUIRE(future.valid());
   REQUIRE_FALSE(future.is_ready());

   SECTION("set_value() synchron")
   {
      promise.set_value();

      REQUIRE(future.is_ready());
      REQUIRE_NOTHROW(future.get());
   }

   SECTION("set_value() in std::thread")
   {
      std::mutex mutex;
      std::unique_lock<std::mutex> lock{mutex};

      std::thread t{[&promise, &mutex]() {
         {
            std::unique_lock<std::mutex> lock{mutex};
         }
         promise.set_value();
      }};

      REQUIRE_FALSE(future.is_ready());
      lock.unlock();

      REQUIRE_NOTHROW(future.get());
      t.join();
   }
}
