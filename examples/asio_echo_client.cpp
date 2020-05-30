
// blocking_tcp_echo_client.cpp
// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~
//
// Copyright (c) 2003-2013 Christopher M. Kohlhoff (chris at kohlhoff dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <boost/asio.hpp>
#include <cstdlib>
#include <cstring>
#include <iostream>

#include <co/networking.hpp>

#include "../test/helper.hpp"

constexpr size_t MaxPackageLength = 1024;
constexpr size_t MessagesPerCoroutine = 100000;
constexpr size_t CoroutineCount = 220;

int main(int argc, char* argv[])
{
   try
   {
      if (argc != 3)
      {
         std::cerr << "Usage: blocking_tcp_echo_client <host> <port>\n";
         return 1;
      }

      boost::asio::io_context io_context;

      auto func = [&]() {
         co::ip::tcp::resolver resolver(io_context);
         co::ip::tcp::socket s(io_context);

         std::string msg = "My test message";
         auto itr = resolver.resolve({argv[1], argv[2]});

         boost::asio::connect(s, itr);
         s.set_option(boost::asio::ip::tcp::no_delay(false));

         for (int i = 0; i < MessagesPerCoroutine; i++)
         {
            // std::cout << "Enter message: ";
            // char request[max_length];
            // std::cin.getline(request, max_length);
            size_t request_length = msg.size();
            boost::asio::write(s, boost::asio::buffer(msg.data(), request_length));

            char reply[MaxPackageLength];
            size_t reply_length = boost::asio::read(s, boost::asio::buffer(reply, request_length));
            if (std::string{reply, reply_length} != msg)
               std::cerr << "Server did not return correct result" << std::endl;
         }

         std::cout << "Coroutine done" << std::endl;
      };

      std::vector<co::Routine> coros(CoroutineCount);

      auto startTime = co_tests::now();

      for (auto& coro : coros)
         coro = co::Routine{func};

      auto threads = co::IoContextThreads::usePercentageOfHardwareThreads(100, io_context);

      for (auto& coro : coros)
         coro.join();

      auto endTime = co_tests::now();

      auto totalRuntime = endTime - startTime;
      auto msgCnt = MessagesPerCoroutine * CoroutineCount;
      auto totalSec = totalRuntime.total_milliseconds() / 1000.0;
      auto msgPerSec = msgCnt / totalSec;

      std::cout << "Runtime: " << totalSec << "s"
                << " (" << msgPerSec << " messages per second)" << std::endl;
   }
   catch (std::exception& e)
   {
      std::cerr << "Exception: " << e.what() << "\n";
   }

   return 0;
}
