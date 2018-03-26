#pragma once

#include "await.hpp"

namespace co
{

inline void throwError(const boost::system::error_code& error, const char* msg)
{
    if (error)
        throw boost::system::system_error(error, msg);
}

namespace impl
{

template<typename Parent>
class BasicResolver : public Parent
{
private:
    auto& parent()
    {
        return static_cast<Parent&>(*this);
    }

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
       
        impl::LightFutureData<impl::StatelessT> p;
        parent().async_resolve(q, [&](const boost::system::error_code & error, iterator itrParam) {
            ec = error;
            itr = itrParam;
            p.set_value(impl::StatelessT {});
        });
        co::await(p);
        
        return itr;
    }
    
    iterator resolve(const endpoint_type& e)
    {
        boost::system::error_code ec{};
        auto itr = resolve(e, ec);
        throwError(ec, "resolve");
        return itr;
    }

    iterator resolve(const endpoint_type& e, boost::system::error_code & ec)
    {
        iterator itr{};
       
        impl::LightFutureData<impl::StatelessT> p;
        parent().async_resolve(e, [&](const boost::system::error_code & error, iterator itrParam) {
            ec = error;
            itr = itrParam;
            p.set_value(impl::StatelessT {});
        });
        co::await(p);
        
        return itr;
    }
};

template<typename Parent>
class BasicSocket : public Parent
{
private:
    auto& parent()
    {
        return static_cast<Parent&>(*this);
    }

public:
    using Parent::Parent;

    using endpoint_type = typename Parent::endpoint_type;
    using message_flags = typename Parent::message_flags;
#if 0 // ony available since boost 1.66
    using wait_type = typename Parent::wait_type;
#endif

    void connect(const endpoint_type& peer_endpoint)
    {
        boost::system::error_code error;
        connect(peer_endpoint, error);
        throwError(error, "connect");
    }

    void connect(const endpoint_type& peer_endpoint, boost::system::error_code& ec)
    {
        impl::LightFutureData<impl::StatelessT> p;
        parent().async_connect(peer_endpoint, [&](const boost::system::error_code & error) {
            ec = error;
            p.set_value(impl::StatelessT {});
        });
        co::await(p);
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
        std::size_t res {};

        impl::LightFutureData<impl::StatelessT> p;
        parent().async_read_some(buffers, [&](const boost::system::error_code & error, std::size_t bytes_transferred) {
            ec = error;
            res = bytes_transferred;
            p.set_value(impl::StatelessT {});
        });
        co::await(p);

        return res;
    }

    template<typename ConstBufferSequence>
    std::size_t write_some(const ConstBufferSequence& buffers)
    {
        boost::system::error_code error;
        auto res = write_some(buffers, error);
        throwError(error, "write_some");
        return res;
    }

    template<typename ConstBufferSequence>
    std::size_t write_some(const ConstBufferSequence& buffers, boost::system::error_code& ec)
    {
        std::size_t res {};

        impl::LightFutureData<impl::StatelessT> p;
        parent().async_write_some(buffers, [&](const boost::system::error_code & error, std::size_t bytes_transferred) {
            ec = error;
            res = bytes_transferred;
            p.set_value(impl::StatelessT {});
        });
        co::await(p);
        throwError(ec, "receive");

        return res;
    }

    template<typename MutableBufferSequence>
    std::size_t receive(const MutableBufferSequence& buffers)
    {
        std::size_t res {};
        boost::system::error_code ec;

        impl::LightFutureData<impl::StatelessT> p;
        parent().async_receive(buffers, [&](const boost::system::error_code & error, std::size_t bytes_transferred) {
            ec = error;
            res = bytes_transferred;
            p.set_value(impl::StatelessT {});
        });
        co::await(p);

        return res;
    }

    template<typename MutableBufferSequence>
    std::size_t receive(const MutableBufferSequence& buffers, message_flags flags)
    {
        std::size_t res {};
        boost::system::error_code ec;

        res = receive(buffers, flags, ec);
        throwError(ec, "receive");

        return res;
    }

    template<typename MutableBufferSequence>
    std::size_t receive(const MutableBufferSequence& buffers, message_flags flags, boost::system::error_code& ec)
    {
        std::size_t res {};

        impl::LightFutureData<impl::StatelessT> p;
        parent().async_receive(buffers, flags, [&](const boost::system::error_code & error, std::size_t bytes_transferred) {
            ec = error;
            res = bytes_transferred;
            p.set_value(impl::StatelessT {});
        });
        co::await(p);

        return res;
    }

#if 0 // ony available since boost 1.66
    void wait(wait_type w)
    {
        boost::system::error_code ec;
        wait(w, ec);
        throwError(ec, "wait");
    }

    void wait(wait_type w, boost::system::error_code& ec)
    {
        impl::LightFutureData<impl::StatelessT> p;
        parent().async_wait(w, [&](const boost::system::error_code & error) {
            ec = error;
            p.set_value(impl::StatelessT {});
        });
        co::await(p);
    }
#endif
};

template<typename Parent>
class BasicAcceptor : public Parent
{
private:
    auto& parent()
    {
        return static_cast<Parent&>(*this);
    }

public:
    using Parent::Parent;

    using protocol_type = typename Parent::protocol_type;
    using endpoint_type = typename Parent::endpoint_type;

    template<typename Protocol>
    using basic_socket = boost::asio::basic_socket<Protocol>;

    template<typename Protocol1>
    void accept(basic_socket<Protocol1>& peer, typename std::enable_if<std::is_convertible<protocol_type, Protocol1>::value>::type* = 0)
    {
        boost::system::error_code ec {};
        accept(peer, ec);
        throwError(ec, "accept");
    }

    template<typename Protocol1>
    boost::system::error_code accept(basic_socket<Protocol1>& peer, boost::system::error_code& ec, typename std::enable_if<std::is_convertible<protocol_type, Protocol1>::value>::type* = 0)
    {
        impl::LightFutureData<impl::StatelessT> p;
        parent().async_accept(peer, [&](const boost::system::error_code & error) {
            ec = error;
            p.set_value(impl::StatelessT {});
        });
        co::await(p);

        return ec;
    }

    void accept(basic_socket<protocol_type>& peer, endpoint_type& peer_endpoint)
    {
        boost::system::error_code ec {};
        accept(peer, peer_endpoint, ec);
        throwError(ec, "accept");
    }

    boost::system::error_code accept(basic_socket<protocol_type>& peer, endpoint_type& peer_endpoint, boost::system::error_code& ec)
    {
        impl::LightFutureData<impl::StatelessT> p;
        parent().async_accept(peer, peer_endpoint, [&](const boost::system::error_code & error) {
            ec = error;
            p.set_value(impl::StatelessT {});
        });
        co::await(p);

        return ec;
    }
};

}

namespace ip
{
namespace tcp
{
using socket = impl::BasicSocket<boost::asio::ip::tcp::socket>;
using acceptor = impl::BasicAcceptor<boost::asio::ip::tcp::acceptor>;
using resolver = impl::BasicAcceptor<boost::asio::ip::tcp::resolver>;
}
}
}