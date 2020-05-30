#pragma once

#include <algorithm>
#include <functional>

#include <boost/asio/io_context.hpp>
#include <boost/optional.hpp>

namespace co
{
using IoContextProvider = std::function<boost::asio::io_context&()>;

namespace impl
{
inline boost::asio::io_context& defaultIoContextProvider()
{
   static boost::asio::io_context ioc;
   return ioc;
}

inline IoContextProvider& currentIoContextProvider()
{
   static IoContextProvider provider{&defaultIoContextProvider};
   return provider;
}
} // namespace impl

inline void setDefaultIoContextProvider(IoContextProvider ioContextProvider)
{
   impl::currentIoContextProvider() = ioContextProvider;
}

inline boost::asio::io_context& defaultIoContext() { return impl::currentIoContextProvider()(); }

class IoContextThreads
{
 private:
   boost::optional<boost::asio::io_context::work> mIosWork;
   std::vector<std::thread> mThreads;

 public:
   explicit IoContextThreads(size_t cnt, boost::asio::io_context& ioc = defaultIoContext())
    : mIosWork{ioc}, mThreads(cnt)
   {
      if (ioc.stopped())
         ioc.reset();

      for (auto& t : mThreads)
         t = std::thread{[&ioc]() { ioc.run(); }};
   }

   static IoContextThreads
   usePercentageOfHardwareThreads(double percent, boost::asio::io_context& ioc = defaultIoContext())
   {
      auto hwThreads = static_cast<int>(std::thread::hardware_concurrency() * percent / 100);
      hwThreads = std::max(1, hwThreads);
      return IoContextThreads{static_cast<size_t>(hwThreads), ioc};
   }

   IoContextThreads(const IoContextThreads&) = delete;
   IoContextThreads& operator=(const IoContextThreads&) = delete;

   IoContextThreads(IoContextThreads&&) = default;
   IoContextThreads& operator=(IoContextThreads&&) = default;

   size_t size() const { return mThreads.size(); }

   void joinAll()
   {
      mIosWork.reset();

      for (auto& t : mThreads)
         t.join();
   }

   ~IoContextThreads() { joinAll(); }
};
}; // namespace co
