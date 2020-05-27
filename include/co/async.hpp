#include "future.hpp"

#include "await.hpp"

namespace co
{
   template<typename Function>
   auto async(boost::asio::io_context& ioc, Function&& func)
   {
       promise<std::result_of_t<std::decay_t<Function>()>>> prom;
       auto f = prom.get_future();
       
       auto f = [f = std::forward<Func>(f), p = std::move(prom)](){
          try {
             p.set_value(f());
          } catch(...) {
             p.set_exception(std::current_exception());
          }
      };
          
       ioc.post( [f = std::move(f), &ioc]() mutable {
          routine{ioc, std::move(f)}.detach();
      } );
       return f;
   }
}
