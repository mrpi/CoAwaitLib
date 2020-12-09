#include "future.hpp"

#include "await.hpp"
#include "routine.hpp"

namespace co
{
template <typename Function>
auto async(boost::asio::io_context& ioc, Function&& func)
{
   promise<std::result_of_t<std::decay_t<Function>()>> p;
   auto f = p.get_future();

   auto fun = [func = std::forward<Function>(func), p = std::move(p)]() mutable {
      try
      {
         p.set_value(func());
      }
      catch (...)
      {
         p.set_exception(std::current_exception());
      }
   };

   ioc.post([fun = std::move(fun), &ioc]() mutable { Routine{ioc, std::move(fun)}.detach(); });

   return f;
}
} // namespace co
