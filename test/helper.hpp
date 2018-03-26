
#pragma once

namespace co_tests
{

inline auto now() { return boost::posix_time::microsec_clock::local_time(); }

class Bench
{
private:
   const boost::posix_time::ptime mStartTime;
   boost::posix_time::ptime mBlockStartTime;
   size_t mIdx{0};
   size_t mTotal{0};
   static constexpr size_t BlockSize = 1024 * 16;
   
   template<typename PrefixT>
   void log(PrefixT&& prefix, const boost::posix_time::ptime& startTime, size_t cnt)
   {
       auto endTime = now();
       
       auto runtime = endTime - startTime;
       auto perSecond = static_cast<double>(cnt) / runtime.total_microseconds() * 1000000.0;
       std::cout << prefix << ": #" << std::setw(8) << mTotal << " (" << std::setw(11) << std::setprecision(2) << std::fixed << perSecond << " per second)" << std::endl;      
   }
   
public:
   Bench()
    : mStartTime(now()), mBlockStartTime(mStartTime)
   {}
   
   ~Bench()
   {
       log("Total", mStartTime, mTotal);
   }
   
   void update()
   {
       mTotal++;
       if (mIdx++ != BlockSize)
          return;
     
       mIdx = 0;
       log(std::this_thread::get_id(), mBlockStartTime, BlockSize);
       
       mBlockStartTime = now();
   }
};
   
};
