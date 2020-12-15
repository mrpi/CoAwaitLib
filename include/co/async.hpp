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

template <typename Function>
auto async(Function&& func)
{
   return async(defaultIoContext(), std::forward<Function>(func));
}

/**
 * Executes the given function inline, when called from a regular thread or post() it on the
 * io_context assigned to the current coroutine.
 *
 * This may be usefull, when you call external librarys, that may be incompatible with the coroutine
 * context or if rarely used code paths need more stack space than associated with the coroutine.
 * The calling coroutine is suspended while waiting on the function result.
 */
template <typename Func>
auto runOutsideOfCoroutine(Func&& func)
{
   if (auto curr = co::Routine::current())
   {
      using ResT = std::invoke_result_t<Func>;
      if constexpr (std::is_same_v<ResT, void>)
      {
         impl::LightFutureData<impl::StatelessT> f;
         curr->mContext.post([&]() {
            try
            {
               func();
               f.set_value({});
            }
            catch (...)
            {
               f.set_exception(std::current_exception());
            }
         });
         await(f);
         return;
      }
      else
      {
         impl::LightFutureData<std::decay_t<ResT>> f;
         curr->mContext.post([&]() {
            try
            {
               f.set_value(func());
            }
            catch (...)
            {
               f.set_exception(std::current_exception());
            }
         });
         return await(f);
      }
   }
   else
      return func();
}
} // namespace co
