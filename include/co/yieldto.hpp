#pragma once

#include "await.hpp"

namespace co
{

struct IgnoreParam
{
   template <typename T>
   constexpr const IgnoreParam& operator=(T&&) const
   {
      return *this;
   }
};

static inline constexpr IgnoreParam ignoreParam{};

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
         parent->args = std::forward_as_tuple(std::forward<ArgsX>(args)...);
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
