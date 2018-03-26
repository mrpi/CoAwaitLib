
// blocking_tcp_echo_client.cpp
// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~
//
// Copyright (c) 2003-2013 Christopher M. Kohlhoff (chris at kohlhoff dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <boost/asio.hpp>

#include <co/networking.hpp>

enum { max_length = 1024 };

int main(int argc, char* argv[])
{
    try {
        if (argc != 3) {
            std::cerr << "Usage: blocking_tcp_echo_client <host> <port>\n";
            return 1;
        }

        boost::asio::io_service io_service;
        
        auto func = [&]()
        {
            co::ip::tcp::resolver resolver(io_service);
            co::ip::tcp::socket s(io_service);
            
            std::string msg ="My test message";
            auto itr = resolver.resolve({argv[1], argv[2]});
            
            boost::asio::connect(s, itr);
               
            for (int i=0; i < 100000; i++)
            {
               //std::cout << "Enter message: ";
               //char request[max_length];
               //std::cin.getline(request, max_length);
               size_t request_length = msg.size();
               boost::asio::write(s, boost::asio::buffer(msg.data(), request_length));

               char reply[max_length];
               size_t reply_length = boost::asio::read(s,
                                                      boost::asio::buffer(reply, request_length));
               if (std::string{reply, reply_length} != msg)
                  std::cerr << "Server did not return correct result" << std::endl;
            }
            
            std::cout << "Coroutine done" << std::endl;
        };

        std::vector<co::Routine> coros(10);
        
        for (auto& coro : coros)
           coro = co::Routine{func};

        co::IoContextThreads threads{2, io_service};
        //io_service.run();
        
        for (auto& coro : coros)
           coro.join();
    } catch (std::exception& e) {
        std::cerr << "Exception: " << e.what() << "\n";
    }

    return 0;
}

