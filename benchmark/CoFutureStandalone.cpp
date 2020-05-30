#include <co_future.hpp>

constexpr size_t innerLoopCnt = 10000000;

auto now() { return boost::posix_time::microsec_clock::local_time(); }

int main()
{
   boost::asio::io_context ioService;
   std::vector<std::thread> myThreads(2);

   boost::optional<boost::asio::io_context::work> work{ioService};
   for (auto&& t : myThreads)
   {
      t = std::thread{[&] { ioService.run(); }};
   }

   auto lastFuture = co::make_ready_future<int>(42);

   auto begin = now();
   for (size_t i = 0; i < innerLoopCnt; i++)
   {
      auto f = co::async(ioService, [] { return 42; });

      if (lastFuture.get() != 42)
         throw std::runtime_error("Invalid value");

      lastFuture = std::move(f);
   }

   if (lastFuture.get() != 42)
      throw std::runtime_error("Invalid value");
   auto end = now();

   auto runtime = end - begin;
   std::cout << "Items per second: " << innerLoopCnt / (runtime.total_microseconds() / 1000000.0)
             << std::endl;

   work = boost::none;

   for (auto&& t : myThreads)
      t.join();

   return 0;
}
