//
// blocking_tcp_echo_server.cpp
// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~
//
// Copyright (c) 2003-2017 Christopher M. Kohlhoff (chris at kohlhoff dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <cstdlib>
#include <iostream>
#include <thread>
#include <utility>
#include <boost/asio.hpp>

#include <co/networking.hpp>

const int max_length = 1024;

std::atomic<size_t> sessions{};

void session(co::ip::tcp::socket sock)
{
    try {
        for (;;) {
            char data[max_length];

            boost::system::error_code error;
            size_t length = sock.read_some(boost::asio::buffer(data), error);
            if (error == boost::asio::error::eof)
                break; // Connection closed cleanly by peer.
            else if (error)
                throw boost::system::system_error(error); // Some other error.

            boost::asio::write(sock, boost::asio::buffer(data, length));
    
            //auto cnt = ++sessions;
            //if (cnt % 50000 == 0)
            //   std::cout << "Executed #" << cnt<< " sessions" << std::endl;
         }
    } catch (std::exception& e) {
        std::cerr << "Exception in thread: " << e.what() << "\n";
    }
}

void server(boost::asio::io_service& io_service, unsigned short port)
{
    co::Routine(
    [&]() {
        co::ip::tcp::acceptor a(io_service, boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), port));
        for (;;) {
            co::ip::tcp::socket sock(io_service);
            a.accept(sock);
            co::Routine {[&]()
            {
                session(std::move(sock));
            }
                        } .detach();
        }
    }
    ).detach();
}

int main(int argc, char* argv[])
{
    try {
        if (argc != 2) {
            std::cerr << "Usage: blocking_tcp_echo_server <port>\n";
            return 1;
        }

        boost::asio::io_service io_service;
        co::setDefaultIoContextProvider([&]() -> boost::asio::io_service& { return io_service; });
        server(io_service, std::atoi(argv[1]));

        co::IoContextThreads threads{2, io_service};
        io_service.run();
    } catch (std::exception& e) {
        std::cerr << "Exception: " << e.what() << "\n";
    }

    return 0;
}

