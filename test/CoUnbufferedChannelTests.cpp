#include <catch2/catch.hpp>

#include <co/channel.hpp>
#include <co/routine.hpp>

#include <boost/optional/optional_io.hpp>

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
