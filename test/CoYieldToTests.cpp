#include <catch2/catch.hpp>

#include <functional>

#include <co/routine.hpp>
#include <co/yieldto.hpp>

#include <functional>
#include <thread>

using Callback = std::function<void(int)>;

namespace
{
void MyApiCallAsync(Callback cb)
{
   std::thread{[cb]() {
      std::this_thread::yield();
      cb(42);
   }}.detach();
}

using CallbackWithPayload = std::function<void(int, void*)>;

void MyApiCallAsyncWithPayload(CallbackWithPayload cb, void* payload)
{
   std::thread{[cb, payload]() {
      std::this_thread::yield();
      cb(42, payload);
   }}.detach();
}

using CallbackWithMoveOnlyParam = std::function<void(std::unique_ptr<int>)>;

void MyApiCallAsyncWithMoveOnlyParam(CallbackWithMoveOnlyParam cb)
{
   std::thread{[cb]() {
      std::this_thread::yield();
      cb(std::make_unique<int>(42));
   }}.detach();
}

} // namespace

TEST_CASE("co::yieldTo")
{
   co::IoContextThreads t{1};

   SECTION("single parameter")
   {
      co::Routine{[]() {
         int ret{};
         MyApiCallAsync(*co::YieldTo{ret});
         REQUIRE(ret == 42);
      }}.join();
   }

   SECTION("unused parameter")
   {
      co::Routine{[]() {
         int ret{};
         MyApiCallAsyncWithPayload(*co::YieldTo{ret, co::ignoreParam}, nullptr);
         REQUIRE(ret == 42);
      }}.join();
   }

   SECTION("move only param")
   {
      co::Routine{[]() {
         std::unique_ptr<int> ret{};
         MyApiCallAsyncWithMoveOnlyParam(*co::YieldTo{ret});
         REQUIRE(ret);
         REQUIRE(*ret == 42);
      }}.join();
   }
}
