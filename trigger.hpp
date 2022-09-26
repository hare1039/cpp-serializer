#pragma once
#ifndef TRIGGER_HPP__
#define TRIGGER_HPP__

#include "basic.hpp"
#include "serializer.hpp"

#include <boost/beast/ssl.hpp>
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wuninitialized"
#include <oneapi/tbb/concurrent_unordered_map.h>
#pragma GCC diagnostic pop

#include <boost/signals2.hpp>

#include <Poco/URI.h>

#include <concepts>

namespace trigger
{

struct httphost
{
    net::io_context::strand write_strand_;

    std::string host;
    std::string port;
    std::string target;

    httphost(net::io_context& io, Poco::URI const& uriparser):
        write_strand_{io},
        host {uriparser.getHost()},
        port {std::to_string(uriparser.getPort())},
        target {uriparser.getPathEtc()} { }

    auto gen_request() -> std::shared_ptr<http::request<http::string_body>>
    {
        auto req = std::make_shared<http::request<http::string_body>>();
        req->set(http::field::host, host);
        req->set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);
        req->set(http::field::content_type, "application/json");
//        req->set(http::field::authorization, "Basic Nzg5YzQ2YjEtNzFmNi00ZWQ1LThjNTQtODE2YWE0ZjhjNTAyOmFiY3pPM3haQ0xyTU42djJCS0sxZFhZRnBYbFBrY2NPRnFtMTJDZEFzTWdSVTRWck5aOWx5R1ZDR3VNREdJd1A="); admin
        req->set(http::field::authorization, "Basic MjNiYzQ2YjEtNzFmNi00ZWQ1LThjNTQtODE2YWE0ZjhjNTAyOjEyM3pPM3haQ0xyTU42djJCS0sxZFhZRnBYbFBrY2NPRnFtMTJDZEFzTWdSVTRWck5aOWx5R1ZDR3VNREdJd1A="); // guest
        req->target(target);
        return req;
    }
};

// StreamType = beast::ssl_stream<beast::tcp_stream> or beast::tcp_stream
using ssl_type = beast::ssl_stream<beast::tcp_stream>;

template <typename T>
concept IsSupportedStream =
    std::is_same_v<T, ssl_type> ||
    std::is_same_v<T, beast::tcp_stream>;

template<typename StreamType> requires IsSupportedStream<StreamType>
class invoker : public std::enable_shared_from_this<invoker<StreamType>>
{
    net::io_context& io_context_;
    net::io_context::strand io_strand_;
    tcp::resolver resolver_;

    StreamType stream_;

    beast::flat_buffer buffer_;
    Poco::URI uriparser_;
    httphost httphost_;
    std::size_t retried_ = 0;
    boost::signals2::signal<void(std::shared_ptr<http::response<http::string_body>>)> on_read_;

public:
    template<typename ... Arguments>
    invoker(net::io_context& io, std::string const& url, Arguments && ... args):
        io_context_{io}, io_strand_{io}, resolver_{io}, stream_{io, std::forward<Arguments>(args)...},
        uriparser_{url}, httphost_ {io, uriparser_}
    {
        using namespace std::literals;
        beast::get_lowest_layer(stream_).expires_after(300s);
    }

    template<typename Function>
    void register_on_read(Function &&f) {
        on_read_.connect(std::forward<Function>(f));
    }

    void start_post(std::string const &body)
    {
        BOOST_LOG_TRIVIAL(trace) << "in post";
        auto req = httphost_.gen_request();
        req->body() = body;
        req->method(http::verb::post);

        if (beast::get_lowest_layer(stream_).socket().is_open())
            start_write(req);
        else
            start_resolve(req);
    }

    void start_resolve(std::shared_ptr<http::request<http::string_body>> req)
    {
        if constexpr (std::is_same_v<StreamType, ssl_type>)
        {
            if (not SSL_set_tlsext_host_name(stream_.native_handle(), httphost_.host.c_str()))
            {
                beast::error_code ec {static_cast<int>(::ERR_get_error()), net::error::get_ssl_category()};
                BOOST_LOG_TRIVIAL(error) << ec.message() << "\n";
                return;
            }
        }

        resolver_.async_resolve(
            httphost_.host, httphost_.port,
            net::bind_executor(
                io_strand_,
                [self=this->shared_from_this(), req](beast::error_code ec, tcp::resolver::results_type results) {
                if (not ec)
                    self->start_connect(results, req);
                else
                    BOOST_LOG_TRIVIAL(error) << "start_connect error: " << ec.message();
                }));
    }

    void start_connect(tcp::resolver::results_type results, std::shared_ptr<http::request<http::string_body>> req)
    {
        BOOST_LOG_TRIVIAL(trace) << "start connect";
        beast::get_lowest_layer(stream_).async_connect(
            results,
            net::bind_executor(
                io_strand_,
                [self=this->shared_from_this(), req](beast::error_code ec, tcp::resolver::results_type::endpoint_type) {
                    if (not ec)
                    {
                        if constexpr (std::is_same_v<StreamType, beast::ssl_stream<beast::tcp_stream>>)
                        {
                            self->stream_.async_handshake(
                                ssl::stream_base::client,
                                net::bind_executor(
                                    self->io_strand_,
                                    [self=self->shared_from_this(), req] (beast::error_code ec) {
                                        if (not ec)
                                            self->start_write(req);
                                        else
                                            BOOST_LOG_TRIVIAL(error) << "start_handshake(ssl) error: " << ec.message();
                                    }));
                        }
                        else
                            self->start_write(req);
                    }
                    else
                        BOOST_LOG_TRIVIAL(error) << "start_connect error: " << ec.message();
                }));
    }

    void start_write(std::shared_ptr<http::request<http::string_body>> req)
    {
        BOOST_LOG_TRIVIAL(trace) << "start sending http request: " << *req;

        req->prepare_payload();
        http::async_write(
            stream_, *req,
            net::bind_executor(
                io_strand_,
                [self=this->shared_from_this(), req](beast::error_code ec, std::size_t /*bytes_transferred*/) {
                    if (not ec)
                        self->start_read();
                    else if (self->retried_ < 3)
                    {
                        self->start_resolve(req);
                        BOOST_LOG_TRIVIAL(warning) << "trigger(" << self->retried_++ << "); start_write error: " << ec.message();
                    }
                    else
                    {
                        BOOST_LOG_TRIVIAL(error) << "trigger start_write error: " << ec.message();
                        self->retried_ = 0;
                    }
                }));
    }

    void start_read()
    {
        auto res = std::make_shared<http::response<http::string_body>>();
        http::async_read(
            stream_, buffer_, *res,
            [self=this->shared_from_this(), res](beast::error_code ec, std::size_t /*bytes_transferred*/) {
                if (not ec)
                {
                    self->on_read_(res);
                    self->on_read_.disconnect_all_slots();
                    BOOST_LOG_TRIVIAL(info) << "read resp: " << res->body();
                }
                else
                    BOOST_LOG_TRIVIAL(error) << "start_read error: " << ec.message();
            });
    }
};

} // namespace trigger

#endif // TRIGGER_HPP__
