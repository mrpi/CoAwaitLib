#include <catch2/catch.hpp>

#include <co/networking.hpp>

#include <thread>

using namespace std::literals;

template <typename Session>
struct Server
{
 private:
   co::ip::tcp::acceptor mAcceptor;
   Session mSession;

 public:
   Server(boost::asio::io_context& ioContext, Session session)
    : mAcceptor(ioContext, boost::asio::ip::tcp::endpoint{boost::asio::ip::tcp::v6(), 0}),
      mSession(std::move(session))
   {
   }

   ~Server() { stop(); }

   void stop() { mAcceptor.close(); }

   std::uint16_t port() { return mAcceptor.local_endpoint().port(); }

   void start()
   {
      co::Routine([this]() {
         while (true)
         {
            co::ip::tcp::socket sock(mAcceptor.get_executor());
            mAcceptor.accept(sock);
            co::Routine([&sock, session = mSession]() mutable {
               try
               {
                  session(std::move(sock));
               }
               catch (std::exception&)
               {
                  return;
               }
            }).detach();
         }
      }).detach();
   }
};

class Client
{
 private:
   co::ip::tcp::socket mSocket;
   std::string mRecvBuf;

 public:
   Client(boost::asio::io_context& ioContext, boost::asio::ip::tcp::endpoint connectTo)
    : mSocket(ioContext)
   {
      mSocket.connect(connectTo);
   }

   void sendMsg(const std::string& msg)
   {
      boost::asio::write(mSocket, boost::asio::const_buffer{msg.data(), msg.size() + 1});
   }

   std::string recvMsg()
   {
      boost::asio::dynamic_string_buffer dynBuf{mRecvBuf};
      boost::system::error_code ec;
      auto len = boost::asio::read_until(mSocket, dynBuf, '\0', ec);
      if (len == 0)
         return "";

      auto res = mRecvBuf.substr(0, len - 1);
      mRecvBuf.erase(0, len);
      return res;
   }
};

TEST_CASE("co::ip::tcp")
{
   boost::asio::io_context ioContext;

   Server srv{ioContext, [](co::ip::tcp::socket socket) {
                 bool first = true;
                 while (true)
                 {
                    std::string buf;
                    boost::asio::dynamic_string_buffer dynBuf{buf};
                    boost::system::error_code ec;
                    auto len = boost::asio::read_until(socket, dynBuf, '\0', ec);
                    if (ec || len == 0)
                       return;
                    auto inMsg = std::string_view{buf}.substr(0, len - 1);
                    REQUIRE(inMsg == "Hello"s);

                    std::string response;
                    if (first)
                    {
                       response = "Client";
                       first = false;
                    }
                    else
                       response = "again";

                    boost::asio::write(
                        socket, boost::asio::const_buffer{response.data(), response.size() + 1});
                 }
              }};
   srv.start();

   co::Routine cliJob{[&srv, &ioContext, port = srv.port()]() {
      try
      {
         Client cli{ioContext, boost::asio::ip::tcp::endpoint{boost::asio::ip::tcp::v6(), port}};

         cli.sendMsg("Hello");
         REQUIRE(cli.recvMsg() == "Client");

         cli.sendMsg("Hello");
         REQUIRE(cli.recvMsg() == "again");

         srv.stop();
      }
      catch (...)
      {
         srv.stop();
         throw;
      }
   }};

   ioContext.run();

   cliJob.join();
}
