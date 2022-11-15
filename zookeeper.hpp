#pragma once
#ifndef ZOOKEEPER_HPP__
#define ZOOKEEPER_HPP__

#include "basic.hpp"
#include "uuid.hpp"
#include "launcher.hpp"

#include <zk/client.hpp>
#include <zk/results.hpp>
#include <zookeeper/zookeeper.h>

#include <boost/asio.hpp>
#include <boost/chrono.hpp>
#include <boost/thread.hpp>
#include <boost/thread/executors/basic_thread_pool.hpp>

#include <oneapi/tbb/concurrent_hash_map.h>

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
    oneapi::tbb::concurrent_hash_map<std::string, net::ip::tcp::endpoint> uuid_cache_;
//    boost::executors::basic_thread_pool pool_;
    boost::launch pool_ = boost::launch::async;
    bool closed_ = false;

#ifdef NDEBUG
    constexpr static ZooLogLevel loglevel = ZOO_LOG_LEVEL_INFO;
#else
    ////constexpr static ZooLogLevel loglevel = ZOO_LOG_LEVEL_DEBUG;
    constexpr static ZooLogLevel loglevel = ZOO_LOG_LEVEL_INFO;
#endif // NDEBUG

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
        launcher_{l} {
        ::zoo_set_debug_level(loglevel);
    }

    void shutdown() { closed_ = true; }

    void reset(uuid::uuid const& server_id, std::vector<char> const& payload)
    {
        erase("/slsfs");

        std::vector<char> dummy{};
        BOOST_LOG_TRIVIAL(trace) << "creating /slsfs";

        client_.create("/slsfs", dummy).then(
            pool_,
            [this, dummy, &server_id, &payload] (auto v) {
                BOOST_LOG_TRIVIAL(info) << "create /slsfs: " << v.get();
                client_.create("/slsfs/proxy", dummy).then(
                    pool_,
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
            pool_,
            [this, child, payload] (auto v) {
                BOOST_LOG_TRIVIAL(debug) << "create on /slsfs/proxy/" << child << ": " << v.get();
                start_watch(child);
                start_heartbeat(child, payload);
            });
    }

    void start_watch(std::string const& child)
    {
        if (closed_)
            return;
        BOOST_LOG_TRIVIAL(trace) << "watch /slsfs/proxy start";

        client_.watch_children("/slsfs/proxy").then(
            pool_,
            [this, child] (zk::future<zk::watch_children_result> children) {
                auto&& res = children.get();
                BOOST_LOG_TRIVIAL(trace) << "set watch ok";

                res.next().then(
                    pool_,
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
            pool_,
            [this] (zk::future<zk::get_children_result> children) {
                std::vector<uuid::uuid> new_proxy_list;
                std::vector<std::string> list = children.get().children();

                std::transform (list.begin(), list.end(),
                                std::back_inserter(new_proxy_list),
                                uuid::decode_base64);

                std::sort(new_proxy_list.begin(), new_proxy_list.end());
                launcher_.reconfigure(new_proxy_list.begin(),
                                      new_proxy_list.end(),
                                      *this);
            });
    }

    void start_heartbeat (std::string const& child, std::vector<char> const& payload)
    {
        if (closed_)
            return;

        using namespace std::string_literals;
        client_.set("/slsfs/proxy/"s + child, payload).then(
            pool_,
            [this, child, payload] (zk::future<zk::set_result>) {
                //BOOST_LOG_TRIVIAL(trace) << "heartbeat set: " << result.get();
                auto timer = std::make_shared<net::deadline_timer>(io_context_, boost::posix_time::seconds(2));
                timer->async_wait(
                    [this, timer, child, payload] (boost::system::error_code const& error) {
                        if (error)
                            BOOST_LOG_TRIVIAL(error) << "have error = " << error;
                        else
                            start_heartbeat(child, payload);
                    });
            });
    }

    auto get_uuid (std::string const& child) -> net::ip::tcp::endpoint
    {
        using namespace std::string_literals;

        decltype(uuid_cache_)::accessor result;
        if (uuid_cache_.find(result, child))
            return result->second;

        zk::future<zk::get_result> resp = client_.get("/slsfs/proxy/"s + child);
        // std::vector<char>
        zk::buffer const buf = resp.get().data();

        auto const colon = std::find(buf.begin(), buf.end(), ':');

        std::string host(std::distance(buf.begin(), colon), '\0');
        std::copy(buf.begin(), colon, host.begin());

        std::string port(std::distance(std::next(colon), buf.end()), '\0');
        std::copy(std::next(colon), buf.end(), port.begin());

        net::ip::tcp::resolver resolver(io_context_);
        for (net::ip::tcp::endpoint resolved : resolver.resolve(host, port))
        {
            //net::ip::tcp::endpoint resolved = resolver.resolve(host, port);
            uuid_cache_.emplace(child, resolved);
            return resolved;
        }
        return {};
    }
};

} // namespace launcher


#endif // ZOOKEEPER_HPP__
