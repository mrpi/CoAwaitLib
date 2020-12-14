#pragma once

#include "channel.hpp"
#include "routine.hpp"

namespace co
{

template <typename T, typename Func>
auto generate(boost::asio::io_context& ioContext, Func&& func)
{
   auto chan = makeUnbufferedChannel<T>();

   auto sender =
       std::make_shared<Sender<std::shared_ptr<UnbufferedChannel<T>>>>(std::move(chan.sender));
   co::Routine{ioContext, [func, sender]() mutable { func(*sender); }}.detach();

   return std::move(chan.receiver);
}

template <typename T, typename Func>
auto generate(Func&& func)
{
   return generate<T>(co::defaultIoContext(), std::forward<Func>(func));
}

template <typename T, typename Func>
auto generateForMultiConsumer(boost::asio::io_context& ioContext, Func&& func)
{
   auto chan = makeBufferedChannel<T>();

   co::Routine{ioContext, [func, sender = chan.sender]() mutable { func(*sender); }}.detach();

   return std::move(chan.receiver);
}

template <typename T, typename Func>
auto generateForMultiConsumer(Func&& func)
{
   return generateForMultiConsumer<T>(co::defaultIoContext(), std::forward<Func>(func));
}

} // namespace co
