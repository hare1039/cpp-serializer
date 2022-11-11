#pragma once
#ifndef ZOOKEEPER_HPP__
#define ZOOKEEPER_HPP__

#include "basic.hpp"
#include "uuid.hpp"
#include "ring.hpp"

#define ZKPP_FUTURE_USE_BOOST 1
#include <zk/client.hpp>

#include <chrono>
#include <thread>

namespace zookeeper
{
// assumes owner is main()
class zookeeper
{
    net::io_context& io_context_;
    zk::client client_;

    void erase(zk::string_view sv)
    {
        try
        {
            BOOST_LOG_TRIVIAL(trace) << "erasing " << sv;
            if (client_.exists(sv).get())
            {
                // zk::get_children_result::children_list_type
                auto list = client_.get_children(sv).get().children();
                for (std::string const& child : list)
                    erase(std::string(sv) + "/" + child);
                client_.erase(sv).get();
            }
        }
        catch (std::exception&) {}
    }

public:
    zookeeper(net::io_context& io):
        io_context_{io},
        client_{zk::client::connect("zk://zookeeper-1:2181").get()} { }

    void reset(uuid::uuid const& server_id)
    {
        erase("/slsfs");

        std::vector<char> dummy{};
        BOOST_LOG_TRIVIAL(trace) << "creating /slsfs";
        client_.create("/slsfs", dummy).then(
            [this, &server_id] (auto v) {
                BOOST_LOG_TRIVIAL(info) << "create /slsfs: " << v.get();
                std::vector<char> dummy{};
                client_.create("/slsfs/proxy", dummy).then(
                    [this, &server_id] (auto v) {
                        BOOST_LOG_TRIVIAL(info) << "create /slsfs/proxy: " << v.get();
                        start_setup(server_id.encode_base64(), server_id.to_vector());
                    });
            });
    }

    void start_setup(std::string const& child, std::vector<char> const& payload)
    {
        using namespace std::string_literals;
        BOOST_LOG_TRIVIAL(info) << "start_setup start";
        auto shared_future = client_.create("/slsfs/proxy/"s + child, payload).share();

        shared_future.then(
            [this, child, shared_future] (auto v) {
                BOOST_LOG_TRIVIAL(debug) << "create on /slsfs/proxy/" << child << ": " << v.get();
                start_watch(child);
                BOOST_LOG_TRIVIAL(debug) << "finish in setup start_watch()";
            });
    }

    void start_watch(std::string const& child)
    {
        BOOST_LOG_TRIVIAL(info) << "watch /slsfs/proxy start";

        client_.watch_children("/slsfs/proxy").then(
            [this, child] (zk::future<zk::watch_children_result> children) {
                [[maybe_unused]] auto&& res = children.get();
                BOOST_LOG_TRIVIAL(trace) << "set watch ok";

                res.next().then(
                    [this, children=std::move(children)] (zk::future<zk::event> event) {
                        zk::event const & e = event.get();
                        BOOST_LOG_TRIVIAL(info) << "watch event get: " << e.type();
                        io_context_.post();
                    });
            });
    }
};

} // namespace launcher


#endif // ZOOKEEPER_HPP__
