#pragma once

#include "channel.hpp"
#include "mutex.hpp"

#include <iterator>

#include <range/v3/view/common.hpp>

namespace co
{

// TODO: detect std:range support
namespace rng_ns = ::ranges;

template <typename T, typename TagT>
struct StrongTypeDef
{
   T value;

   explicit StrongTypeDef(const T& val) : value(val) {}

   explicit StrongTypeDef(T&& val) : value(std::move(val)) {}
};

using MaxParallelity = StrongTypeDef<size_t, struct MaxParallelityTag>;

/**
 * Executes the given callable with each element of the range.
 * The execution is done in multiple coroutines (at most maxParallelity executions at the same
 * time). The callable is copyed and each copy is only executed once at a time.
 *
 * Exception handling:
 * If one exception occured while processing the elements of the range, this exception is rethrown.
 * In case of error, it is undefined, which elements have already been processed and which not.
 * If more than one exception occures, it is undefined, which one of these is rethrown by forEach().
 */
template <typename RangeT, typename Func>
void forEach(boost::asio::io_context& context, MaxParallelity maxParallelity, RangeT&& r,
             Func&& func)
{
   static_assert(rng_ns::input_range<RangeT>);
   using ValueT = rng_ns::range_value_t<RangeT>;

   std::vector<Routine> coros;

   std::mutex mutex;
   std::exception_ptr firstError;
   const auto handleError = [&]() {
      std::unique_lock l{mutex};
      if (!firstError)
         firstError = std::current_exception();
   };

   if constexpr (rng_ns::random_access_range<RangeT> && rng_ns::sized_range<RangeT>)
   {
      const size_t s = rng_ns::size(r);
      const auto coroCnt = std::min(maxParallelity.value, s);
      coros.resize(coroCnt);

      const auto itemsPerCoro = s / coroCnt;
      auto remaining = s % coroCnt;
      size_t startPos = 0;

      auto begin = rng_ns::begin(r);
      auto end = begin;
      for (auto& coro : coros)
      {
         begin = end;
         end = begin + itemsPerCoro;
         if (remaining)
         {
            ++end;
            --remaining;
         }

         coro = Routine{context, [handleError, func, begin, end]() mutable {
                           try
                           {
                              for (auto itr = begin; itr != end; ++itr)
                                 func(*itr);

                              // TODO: Some form of job stealing?
                           }
                           catch (...)
                           {
                              handleError();
                           }
                        }};
      }
   }
   else
   {
      coros.resize(maxParallelity.value);

      auto chan = makeBufferedChannel<ValueT>(maxParallelity.value);

      for (auto& coro : coros)
      {
         coro = Routine{context, [handleError, receiver = chan.receiver, func]() {
                           try
                           {
                              for (auto&& itm : *receiver)
                                 func(itm);
                           }
                           catch (...)
                           {
                              handleError();
                           }
                        }};
      }
      chan.receiver = nullptr;

      for (auto&& itm : r)
      {
         if (!(*chan.sender)(itm))
         {
            assert(firstError);
            break;
         }
      }
   }

   for (auto& coro : coros)
      await(coro);

   if (firstError)
      std::rethrow_exception(firstError);
}

template <typename RangeT, typename Func>
void forEach(MaxParallelity maxParallelity, RangeT&& r, Func&& func)
{
   forEach(co::defaultIoContext(), maxParallelity, std::forward<RangeT>(r),
           std::forward<Func>(func));
}

} // namespace co
