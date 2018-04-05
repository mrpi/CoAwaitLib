#pragma once

#include "mutex.hpp"

#include <iterator>

namespace co
{
   template<typename T, typename TagT>
   struct StrongTypeDef
   {
      T value;
      
      explicit StrongTypeDef(const T& val)
       : value(val)
       {}
      
      explicit StrongTypeDef(T&& val)
       : value(std::move(val))
       {}
   };
   
   using MaxParallelity = StrongTypeDef<size_t, struct MaxParallelityTag>;
   
   // Current implementation requires random access iterator. 
   // TODO: A less efficient fallback implementation for input iterators should be possible.
   template<typename RangeT, typename Func>
   void forEach(boost::asio::io_context& context, MaxParallelity parallelity, RangeT&& r, Func&& func)
   {
      auto s = r.size();
      auto coroCnt = std::min(parallelity.value, s);
      std::vector<Routine> coros(coroCnt);
      std::exception_ptr firstError;
      
      auto itr = std::begin(r);
      auto end = std::end(r);
      
      using Iterator = std::decay_t<decltype(std::begin(r))>;
      auto itemsPerCoro = s / coroCnt;
      
      Mutex m;
      
      for (size_t i=0; i < coroCnt; i++)
      {
         auto itr = std::begin(r) + (i*itemsPerCoro);
         auto end = itr + itemsPerCoro;
         if (i == coroCnt-1)
            end = std::end(r);
         
         coros[i] = Routine{context, [&, func, itr, end]() mutable {
               try {
                  while(true)
                  {
                     if (itr == end)
                        break;
                     
                     auto&& itm = *(itr++);
                     
                     func(itm);
                  }
                  
                  // TODO: Some form of job stealing (may be only optional)
                  // Implementation idea: after 1/3 of the items is done, check, if the coroutine count is less than max parallelity and split the remaining work between this and a newley created coroutine
                  // This may spilt less efficient than actual job stealing but the synchronisation may be less
               } catch(...) {
                  std::unique_lock<Mutex> l{m};
                  if (!firstError)
                     firstError = std::current_exception();
               }
            }};
      }
      
      for (auto& coro : coros)
         await(coro);
      
      if (firstError)
         std::rethrow_exception(firstError);
   }   
}

