#pragma once
#ifndef ZOOKEEPER_HPP__
#define ZOOKEEPER_HPP__

#include "basic.hpp"
#include "uuid.hpp"
#include "launcher.hpp"

#define ZKPP_FUTURE_USE_BOOST 1
#include <zk/client.hpp>
#include <zk/results.hpp>

#include <string>
#include <chrono>
#include <thread>
#include <vector>

namespace zookeeper
{
// assumes owner is main()
class zookeeper
{
    net::io_context& io_context_;
    zk::client client_;
    launcher::launcher& launcher_;

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
    zookeeper(net::io_context& io, launcher::launcher &l):
        io_context_{io},
        client_{zk::client::connect("zk://zookeeper-1:2181").get()},
        launcher_{l} { }

    void reset(uuid::uuid const& server_id, std::vector<char> const& payload)
    {
        erase("/slsfs");

        std::vector<char> dummy{};
        BOOST_LOG_TRIVIAL(trace) << "creating /slsfs";

        client_.create("/slsfs", dummy).then(
            [this, dummy, &server_id, &payload] (auto v) {
                BOOST_LOG_TRIVIAL(info) << "create /slsfs: " << v.get();
                client_.create("/slsfs/proxy", dummy).then(
                    [this, &server_id, &payload] (auto v) {
                        BOOST_LOG_TRIVIAL(info) << "create /slsfs/proxy: " << v.get();
                        start_setup(server_id.encode_base64(), payload);
                    });
            });
    }

    void start_setup(std::string const& child, std::vector<char> const& payload)
    {
        using namespace std::string_literals;
        BOOST_LOG_TRIVIAL(trace) << "zk setup start";
        client_.create("/slsfs/proxy/"s + child, payload).then(
            [this, child] (auto v) {
                BOOST_LOG_TRIVIAL(debug) << "create on /slsfs/proxy/" << child << ": " << v.get();
                start_watch(child);
            });
        BOOST_LOG_TRIVIAL(trace) << "zk setup start finish";
    }

    void start_watch(std::string const& child)
    {
        BOOST_LOG_TRIVIAL(info) << "watch /slsfs/proxy start";

        client_.watch_children("/slsfs/proxy").then(
            [this, child] (zk::future<zk::watch_children_result> children) {
                [[maybe_unused]] auto&& res = children.get();
                BOOST_LOG_TRIVIAL(trace) << "set watch ok";

                res.next().then(
                    [this, child, children=std::move(children)] (zk::future<zk::event> event) {
                        zk::event const & e = event.get();
                        BOOST_LOG_TRIVIAL(info) << "watch event get: " << e.type();
                        start_reconfigure();
                        start_watch(child);
                    });
            });
    }

    void start_reconfigure()
    {
        BOOST_LOG_TRIVIAL(info) << "start_reconfigure";

        client_.get_children("/slsfs/proxy").then(
            [this] (zk::future<zk::get_children_result> children) {
                std::vector<uuid::uuid> new_proxy_list;
                std::vector<std::string> list = children.get().children();

                std::transform (list.begin(), list.end(),
                                std::back_inserter(new_proxy_list),
                                uuid::decode_base64);

                std::sort(new_proxy_list.begin(), new_proxy_list.end());
                launcher_.reconfigure(new_proxy_list.begin(), new_proxy_list.end());
            });
    }

    void start_heartbeat (std::string const& child, std::vector<char> const& payload)
    {
        BOOST_LOG_TRIVIAL(trace) << "send heartbeat";
        using namespace std::string_literals;

        client_.set("/slsfs/proxy"s + child, payload).then(
            [this, child, payload] (zk::future<zk::set_result> result) {
                using namespace std::chrono_literals;
                BOOST_LOG_TRIVIAL(trace) << "heartbeat set: " << result.get();
                std::this_thread::sleep_for(2s);
                start_heartbeat(child, payload);
            });
    }
};

} // namespace launcher


#endif // ZOOKEEPER_HPP__
