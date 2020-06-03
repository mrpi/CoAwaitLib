#pragma once

#include "await.hpp"

namespace co
{

template <typename... Args>
struct YieldTo
{
 private:
   impl::LightFutureData<impl::StatelessT> p;
   std::tuple<Args...> args;

   struct Ref
   {
      YieldTo* parent;

      template <typename... ArgsX>
      void operator()(ArgsX&&... args)
      {
         parent->args = std::tie(args...);
         parent->p.set_value(impl::StatelessT{});
      }
   };

 public:
   YieldTo(Args&... args) : args(std::tie(args...)) {}

   ~YieldTo() { co::await(p); }

   Ref operator*() && { return Ref{this}; }
};

template <typename... Args>
YieldTo(Args&... args) -> YieldTo<Args&...>;

} // namespace co
