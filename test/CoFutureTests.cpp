#include <co/future.hpp>

#include <catch.hpp>

auto now() { return boost::posix_time::microsec_clock::local_time(); }

TEST_CASE("co::future")
{
   SECTION("default constructed")
   {
      co::future<int> f;
      static_assert(std::is_same<decltype(f.get()), int>::value, "f.get() should return type T");
      REQUIRE(f.valid() == false);
   }
}

TEST_CASE("co::make_ready_future")
{
   auto f = co::make_ready_future(42);
   REQUIRE(f.valid());
   REQUIRE(f.is_ready());
   REQUIRE(f.await_resume() == 42);
   REQUIRE(f.get() == 42);
}

TEST_CASE("co::make_exceptional_future")
{
   co::future<int> f;
   
   SECTION("from exception value")
   {
      f = co::make_exceptional_future<int>(42);
   }

   SECTION("from std::exception_ptr")
   {
      f = [](){
         try {
            throw 42;
         } catch(...) {
            return co::make_exceptional_future<int>(std::current_exception());
         }
         
         FAIL("expected return from catch(int)");
      }();
   }

   REQUIRE(f.valid());
   REQUIRE(f.is_ready());
   REQUIRE_THROWS_AS(f.await_resume(), int);
   REQUIRE_THROWS_AS(f.get(), int);
   
   try {
      f.get();
   } catch(int val) {
      REQUIRE(val == 42);
   }
}
