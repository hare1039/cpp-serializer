#pragma once
#ifndef TRIGGER_HPP__
#define TRIGGER_HPP__

#include "basic.hpp"
#include "serializer.hpp"

#include <oneapi/tbb/concurrent_unordered_map.h>
#include <Poco/URI.h>

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
        req->set(http::field::authorization, "Basic Nzg5YzQ2YjEtNzFmNi00ZWQ1LThjNTQtODE2YWE0ZjhjNTAyOmFiY3pPM3haQ0xyTU42djJCS0sxZFhZRnBYbFBrY2NPRnFtMTJDZEFzTWdSVTRWck5aOWx5R1ZDR3VNREdJd1A=");
        req->target(target);
        return req;
    }
};

class invoker : public std::enable_shared_from_this<invoker>
{
    net::io_context& io_context_;
    net::io_context::strand write_io_strand_;
    tcp::resolver resolver_;
    beast::tcp_stream stream_;
    beast::flat_buffer buffer_;
    Poco::URI uriparser_;
    httphost httphost_;
    std::size_t retried_ = 0;

public:
    invoker(net::io_context& io,
            std::string const& url):
        io_context_{io}, write_io_strand_{io}, resolver_{io}, stream_{io},
        uriparser_{url}, httphost_ {io, uriparser_}
    {
        using namespace std::literals;
        stream_.expires_after(300s);
    }

    void start_post(std::string const &body)
    {
        BOOST_LOG_TRIVIAL(trace) << "in post";
        auto req = httphost_.gen_request();
        req->body() = body;
        req->method(http::verb::post);

        if (stream_.socket().is_open())
            start_write(req);
        else
            start_resolve(req);
    }

    void start_resolve(std::shared_ptr<http::request<http::string_body>> req)
    {
        resolver_.async_resolve(
            httphost_.host, httphost_.port,
            [self=shared_from_this(), req](beast::error_code ec, tcp::resolver::results_type results) {
                if (not ec)
                    self->start_connect(results, req);
                else
                    BOOST_LOG_TRIVIAL(error) << "start_connect error: " << ec.message();
            });
    }

    void start_connect(tcp::resolver::results_type results, std::shared_ptr<http::request<http::string_body>> req)
    {
        stream_.async_connect(
            results,
            [self=shared_from_this(), req](beast::error_code ec, tcp::resolver::results_type::endpoint_type) {
                if (not ec)
                    self->start_write(req);
                else
                    BOOST_LOG_TRIVIAL(error) << "start_write error: " << ec.message();
            });
    }

    void start_write(std::shared_ptr<http::request<http::string_body>> req)
    {
        BOOST_LOG_TRIVIAL(trace) << "start sending http request: " << *req;

        req->prepare_payload();
        http::async_write(
            stream_, *req,
            net::bind_executor(
                write_io_strand_,
                [self=shared_from_this(), req](beast::error_code ec, std::size_t bytes_transferred) {
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
            [self=shared_from_this(), res](beast::error_code ec, std::size_t bytes_transferred) {
                if (not ec)
                    BOOST_LOG_TRIVIAL(debug) << "read resp: " << res->body();
                else
                    BOOST_LOG_TRIVIAL(error) << "start_write error: " << ec.message();
            });
    }
};

} // namespace trigger

#endif // TRIGGER_HPP__
