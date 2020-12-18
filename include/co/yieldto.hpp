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
      void operator()(ArgsX&&... args) const
      {
         parent->args = std::forward_as_tuple(std::forward<ArgsX>(args)...);
         parent->p.set_value(impl::StatelessT{});
      }
   };

 public:
   YieldTo(Args&... args) : args(std::tie(args...)) {}

   YieldTo(const YieldTo&) = delete;
   YieldTo& operator=(const YieldTo&) = delete;

   YieldTo(YieldTo&&) = delete;
   YieldTo& operator=(YieldTo&&) = delete;

   ~YieldTo() { co::await(p); }

   Ref operator*() && { return Ref{this}; }

   template <typename T>
   operator T() &&
   {
      static_assert(std::is_convertible_v<Ref, T>);
      return Ref{this};
   }
};

template <typename... Args>
YieldTo(Args&... args) -> YieldTo<Args&...>;

template <typename... Args>
inline auto yieldTo(Args&... args)
{
   return YieldTo{args...};
}

} // namespace co
