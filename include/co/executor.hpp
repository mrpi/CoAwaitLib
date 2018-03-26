#pragma once

namespace co
{
   using IoContextProvider = std::function<boost::asio::io_service& ()>;
   
   namespace impl
   {
      inline boost::asio::io_service& defaultIoContextProvider()
      {
          static boost::asio::io_service ioc;
          return ioc;
      }
      
      inline IoContextProvider& currentIoContextProvider()
      {
          static IoContextProvider provider{&defaultIoContextProvider};
          return provider;
      }
   }
   
   inline void setDefaultIoContextProvider(IoContextProvider ioContextProvider)
   {
       impl::currentIoContextProvider() = ioContextProvider;
   }
   
   inline boost::asio::io_service& defaultIoContext()
   {
       return impl::currentIoContextProvider()();
   }
   
   class IoContextThreads
   {
   private:
      boost::optional<boost::asio::io_service::work> mIosWork;
      std::vector<std::thread> mThreads;
               
   public:
      explicit IoContextThreads(size_t cnt, boost::asio::io_service& ioc = defaultIoContext())
       : mIosWork{ioc}, mThreads(cnt)
      {
          if (ioc.stopped())
             ioc.reset();
         
          for (auto& t : mThreads)
             t = std::thread{[&ioc](){ ioc.run(); }};
      }
      
      IoContextThreads(const IoContextThreads&) = delete;
      IoContextThreads& operator=(const IoContextThreads&) = delete;
      
      ~IoContextThreads()
      {
         mIosWork.reset();
   
         for (auto& t : mThreads)
            t.join();

      }
   };
};
