#pragma once

#include "await.hpp"
#include <pplx/pplxtasks.h>

namespace co
{

template <typename T>
class Awaiter<pplx::task<T>>
{
 private:
   pplx::task<T>& mTask;

 public:
   Awaiter(pplx::task<T>& task) : mTask{task} {}

   bool await_ready() { return mTask.is_done(); }

   auto await_resume() -> decltype(mTask.get()) { return mTask.get(); }

   auto await_synchron() -> decltype(mTask.get()) { return mTask.get(); }

   template <typename Handle>
   bool await_suspend(Handle&& cb)
   {
      mTask.then([cb = std::ref(cb)](auto&&) { cb(); });
      return true;
   }
};

} // namespace co
