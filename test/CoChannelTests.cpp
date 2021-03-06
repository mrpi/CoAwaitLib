#include <catch2/catch.hpp>

#include <co/async.hpp>
#include <co/channel.hpp>
#include <co/foreach.hpp>
#include <co/generate.hpp>
#include <co/routine.hpp>

#include <boost/optional/optional_io.hpp>

#include <range/v3/range/access.hpp>

#include <set>
#include <thread>

namespace
{
template <typename Sender, typename Receiver>
void run(Sender&& sender, Receiver&& receiver)
{
   boost::asio::io_context ioContext;

   SECTION("sender in coroutine and receiver in coroutine")
   {
      co::Routine senderRoutine{ioContext, sender};
      co::Routine receiverRoutine{ioContext, receiver};

      ioContext.run();

      senderRoutine.join();
      receiverRoutine.join();
   }

   SECTION("sender in coroutine and receiver in thread")
   {
      co::Routine senderRoutine{ioContext, sender};
      std::thread receiverRoutine{receiver};

      ioContext.run();

      senderRoutine.join();
      receiverRoutine.join();
   }

   SECTION("sender in thread and receiver in coroutine")
   {
      std::thread senderRoutine{sender};
      co::Routine receiverRoutine{ioContext, receiver};

      ioContext.run();

      senderRoutine.join();
      receiverRoutine.join();
   }

   SECTION("sender in thread and receiver in thread")
   {
      std::thread senderRoutine{sender};
      std::thread receiverRoutine{receiver};

      senderRoutine.join();
      receiverRoutine.join();
   }
}

} // namespace

TEST_CASE("co::makeUnbufferedChannel")
{
   auto&& [sender, receiver] = co::makeUnbufferedChannel<int>();

   using ReceiverT = std::decay_t<decltype(receiver)>;

   static_assert(co::rng_ns::range<ReceiverT>);
   static_assert(co::rng_ns::input_range<ReceiverT>);
   static_assert(!co::rng_ns::forward_range<ReceiverT>);
   static_assert(!co::rng_ns::sized_range<ReceiverT>);
   static_assert(!co::rng_ns::random_access_range<ReceiverT>);

   static constexpr int valueCnt = 100;

   auto senderFun = [&sender]() {
      auto send = std::move(sender);
      for (int i = 0; i < valueCnt; i++)
      {
         if (!send(i))
            return;
      }
   };

   auto receiverFun = [&receiver]() {
      int expected = 0;

      auto receive = std::move(receiver);
      for (auto&& val : receive)
         REQUIRE(expected++ == val);
      REQUIRE(expected == valueCnt);
   };

   run(senderFun, receiverFun);
}

TEST_CASE("co::UnbufferedChannel")
{
   co::UnbufferedChannel<int> channel;

   static constexpr int valueCnt = 100;

   auto sender = [&channel]() {
      co::Sender send{&channel};
      for (int i = 0; i < valueCnt; i++)
      {
         if (!send(i))
            return;
      }
   };

   SECTION("receive all")
   {
      auto receiver = [&channel]() {
         int expected = 0;

         for (auto&& val : co::Receiver{&channel})
            REQUIRE(expected++ == val);
         REQUIRE(expected == valueCnt);
      };

      run(sender, receiver);
   }

   SECTION("stop receiving before end of input")
   {
      auto receiver = [&channel]() {
         int expected = 0;
         for (int i = 0; i < valueCnt - 1; i++)
         {
            auto val = channel.pop();
            REQUIRE(val);
            REQUIRE(expected++ == *val);
         }
         channel.closeReceiver();
      };

      run(sender, receiver);
   }
}

template <bool BreakReceiver = false>
void testBufferedChannel()
{
   boost::asio::io_context ioContext;
   auto c = co::makeBufferedChannel<int>();

   using ReceiverT = std::decay_t<decltype(*c.receiver)>;

   static_assert(co::rng_ns::range<ReceiverT>);
   static_assert(co::rng_ns::input_range<ReceiverT>);
   static_assert(!co::rng_ns::forward_range<ReceiverT>);
   static_assert(!co::rng_ns::sized_range<ReceiverT>);
   static_assert(!co::rng_ns::random_access_range<ReceiverT>);

   std::atomic<int> cnt{0};

   static constexpr int tstCount = 100000;
   static constexpr int tstExpected = BreakReceiver ? (tstCount / 2) : tstCount;

   {
      auto send = [sender = c.sender, &cnt]() {
         while (true)
         {
            auto i = cnt++;
            if (i >= tstCount)
               break;
            if (!(*sender)(i))
               break;
         }
      };
      co::Routine{ioContext, send}.detach();
      co::Routine{ioContext, send}.detach();
      co::Routine{ioContext, send}.detach();
      c.sender = nullptr;
   }

   auto receive = [receiver = c.receiver]() {
      std::set<int> res;
      for (auto&& val : *receiver)
      {
         res.insert(val);

         if constexpr (BreakReceiver)
         {
            if (res.size() == tstExpected / 2)
               break;
         }
      }
      return res;
   };

   co::IoContextThreads threads{2, ioContext};

   auto res1Fut = co::async(ioContext, receive);
   auto res2Fut = co::async(ioContext, receive);
   c.receiver = nullptr;

   auto res1 = co::await(res1Fut);
   auto res2 = co::await(res2Fut);

   REQUIRE(res1.size());
   REQUIRE(res2.size());

   REQUIRE(res1.size() + res2.size() == tstExpected);
   res1.insert(res2.begin(), res2.end());

   REQUIRE(*res1.begin() == 0);
   REQUIRE(*(--res1.end()) == tstExpected - 1);
   REQUIRE(res1.size() == tstExpected);
}

TEST_CASE("co::BufferedChannel")
{
   SECTION("Read till end of input") { testBufferedChannel<false>(); }

   SECTION("Break reading before end of input") { testBufferedChannel<true>(); }
}

TEST_CASE("co::generate")
{
   co::IoContextThreads t{2};

   auto gen = co::generate<int>([i = 0](auto& yield) mutable {
      while (yield(i++))
         ;
   });

   int expected = 0;
   for (int val : gen)
   {
      REQUIRE(expected++ == val);

      if (val == 3)
         break;
   }
}

TEST_CASE("co::generateForMultiConsumer")
{
   co::IoContextThreads t{2};

   auto gen = co::generateForMultiConsumer<int>([i = 0](auto& yield) mutable {
      while (yield(i++))
         ;
   });

   std::mutex m;
   std::set<int> res;

   auto sink = [&res, &m, gen]() {
      for (auto val : *gen)
      {
         if (val > 10)
            break;

         std::unique_lock lock{m};
         res.insert(val);
      }
   };
   auto c1 = co::Routine{sink};
   auto c2 = co::Routine{sink};
   auto c3 = co::Routine{sink};
   gen = nullptr;

   c1.join();
   c2.join();
   c3.join();

   std::unique_lock lock{m};
   REQUIRE(res.size() == 11);
   REQUIRE(*res.begin() == 0);
   REQUIRE(*(--res.end()) == 10);
}

TEST_CASE("co::runOutsideOfCoroutine")
{
   co::IoContextThreads t{2};

   SECTION("from normal thread")
   {
      auto threadIdExec = co::runOutsideOfCoroutine([]() { return std::this_thread::get_id(); });
      REQUIRE(threadIdExec == std::this_thread::get_id());
   }

   SECTION("from coroutine")
   {
      co::Routine{[]() {
         co::runOutsideOfCoroutine([]() { REQUIRE(co::Routine::current() == nullptr); });
      }}.join();
   }
}
