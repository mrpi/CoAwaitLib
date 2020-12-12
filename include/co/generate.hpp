#pragma once

#include "channel.hpp"
#include "routine.hpp"

namespace co
{

template <typename T, typename Func>
auto generate(Func&& func)
{
   auto chan = makeUnbufferedChannel<T>();

   auto sender =
       std::make_shared<Sender<std::shared_ptr<UnbufferedChannel<T>>>>(std::move(chan.sender));
   co::Routine{[func, sender]() mutable { func(*sender); }}.detach();

   return std::move(chan.receiver);
}

} // namespace co
