#pragma once

#include "await.hpp"
#include "yieldto.hpp"

namespace co
{

namespace impl
{

template <typename Parent>
class BasicResolver : public Parent
{
 private:
   auto& parent() { return static_cast<Parent&>(*this); }

 public:
   using Parent::Parent;

   using query = typename Parent::query;
   using iterator = typename Parent::iterator;
   using endpoint_type = typename Parent::endpoint_type;

   iterator resolve(const query& q)
   {
      boost::system::error_code ec{};
      auto itr = resolve(q, ec);
      throwError(ec, "resolve");
      return itr;
   }

   iterator resolve(const query& q, boost::system::error_code& ec)
   {
      iterator itr{};
      parent().async_resolve(q, *YieldTo(ec, itr));
      return itr;
   }

   iterator resolve(const endpoint_type& e)
   {
      boost::system::error_code ec{};
      auto itr = resolve(e, ec);
      throwError(ec, "resolve");
      return itr;
   }

   iterator resolve(const endpoint_type& e, boost::system::error_code& ec)
   {
      iterator itr{};
      parent().async_resolve(e, *YieldTo(ec, itr));
      return itr;
   }
};

template <typename Parent>
class BasicSocket : public Parent
{
 private:
   auto& parent() { return static_cast<Parent&>(*this); }

 public:
   using Parent::Parent;

   using endpoint_type = typename Parent::endpoint_type;
   using message_flags = typename Parent::message_flags;
   using wait_type = typename Parent::wait_type;

   void connect(const endpoint_type& peer_endpoint)
   {
      boost::system::error_code error;
      connect(peer_endpoint, error);
      throwError(error, "connect");
   }

   void connect(const endpoint_type& peer_endpoint, boost::system::error_code& ec)
   {
      parent().async_connect(peer_endpoint, *YieldTo(ec));
   }

   template <typename MutableBufferSequence>
   std::size_t read_some(const MutableBufferSequence& buffers)
   {
      boost::system::error_code error;
      auto res = read_some(buffers, error);
      throwError(error, "read_some");
      return res;
   }

   template <typename MutableBufferSequence>
   std::size_t read_some(const MutableBufferSequence& buffers, boost::system::error_code& ec)
   {
      std::size_t bytesTransferred{};

      boost::asio::socket_base::bytes_readable command(true);
      parent().io_control(command);
      auto bytesReadable = command.get();
      if (bytesReadable)
         return parent().read_some(buffers, ec);

      parent().async_read_some(buffers, *YieldTo(ec, bytesTransferred));

      return bytesTransferred;
   }

   template <typename ConstBufferSequence>
   std::size_t write_some(const ConstBufferSequence& buffers)
   {
      boost::system::error_code error;
      auto res = write_some(buffers, error);
      throwError(error, "write_some");
      return res;
   }

   template <typename ConstBufferSequence>
   std::size_t write_some(const ConstBufferSequence& buffers, boost::system::error_code& ec)
   {
      std::size_t bytesTransferred{};
      parent().async_write_some(buffers, *YieldTo(ec, bytesTransferred));
      return bytesTransferred;
   }

   template <typename ConstBufferSequence>
   std::size_t send(const ConstBufferSequence& buffers)
   {
      return send(buffers, 0);
   }

   template <typename ConstBufferSequence>
   std::size_t send(const ConstBufferSequence& buffers, message_flags flags)
   {
      boost::system::error_code error;
      auto res = send(buffers, 0, error);
      throwError(error, "send");
      return res;
   }

   template <typename ConstBufferSequence>
   std::size_t send(const ConstBufferSequence& buffers, message_flags flags,
                    boost::system::error_code& ec)
   {
      std::size_t bytesTransferred{};

      parent().async_send(buffers, flags, *YieldTo(ec, bytesTransferred));
      throwError(ec, "send");

      return bytesTransferred;
   }

   template <typename MutableBufferSequence>
   std::size_t receive(const MutableBufferSequence& buffers)
   {
      std::size_t bytesTransferred{};
      boost::system::error_code ec;

      parent().async_receive(buffers, *YieldTo(ec, bytesTransferred));
      throwError(ec, "receive");

      return bytesTransferred;
   }

   template <typename MutableBufferSequence>
   std::size_t receive(const MutableBufferSequence& buffers, message_flags flags)
   {
      std::size_t bytesTransferred{};
      boost::system::error_code ec;

      bytesTransferred = receive(buffers, flags, ec);
      throwError(ec, "receive");

      return bytesTransferred;
   }

   template <typename MutableBufferSequence>
   std::size_t receive(const MutableBufferSequence& buffers, message_flags flags,
                       boost::system::error_code& ec)
   {
      std::size_t bytesTransferred{};

      parent().async_receive(buffers, flags, *YieldTo(ec, bytesTransferred));

      return bytesTransferred;
   }

   void wait(wait_type w)
   {
      boost::system::error_code ec;
      wait(w, ec);
      throwError(ec, "wait");
   }

   void wait(wait_type w, boost::system::error_code& ec) { parent().async_wait(w, *YieldTo(ec)); }
};

template <typename Parent>
class BasicAcceptor : public Parent
{
 private:
   auto& parent() { return static_cast<Parent&>(*this); }

 public:
   using Parent::Parent;

   using protocol_type = typename Parent::protocol_type;
   using endpoint_type = typename Parent::endpoint_type;

   template <typename Protocol>
   using basic_socket = boost::asio::basic_socket<Protocol>;

   template <typename Protocol1>
   void
   accept(basic_socket<Protocol1>& peer,
          typename std::enable_if<std::is_convertible<protocol_type, Protocol1>::value>::type* = 0)
   {
      boost::system::error_code ec{};
      accept(peer, ec);
      throwError(ec, "accept");
   }

   template <typename Protocol1>
   boost::system::error_code
   accept(basic_socket<Protocol1>& peer, boost::system::error_code& ec,
          typename std::enable_if<std::is_convertible<protocol_type, Protocol1>::value>::type* = 0)
   {
      parent().async_accept(peer, *YieldTo(ec));
      return ec;
   }

   void accept(basic_socket<protocol_type>& peer, endpoint_type& peer_endpoint)
   {
      boost::system::error_code ec{};
      accept(peer, peer_endpoint, ec);
      throwError(ec, "accept");
   }

   boost::system::error_code accept(basic_socket<protocol_type>& peer, endpoint_type& peer_endpoint,
                                    boost::system::error_code& ec)
   {
      parent().async_accept(peer, peer_endpoint, *YieldTo(ec));
      return ec;
   }
};

} // namespace impl

namespace ip
{
namespace tcp
{
using socket = impl::BasicSocket<boost::asio::ip::tcp::socket>;
using acceptor = impl::BasicAcceptor<boost::asio::ip::tcp::acceptor>;
using resolver = impl::BasicAcceptor<boost::asio::ip::tcp::resolver>;
} // namespace tcp
} // namespace ip
} // namespace co
