#pragma once
#ifndef ZOOKEEPER_HPP__
#define ZOOKEEPER_HPP__

#include "basic.hpp"

#define ZKPP_FUTURE_USE_BOOST 1
#include <zk/client.hpp>

namespace zookeeper
{

class zookeeper
{
    net::io_context& io_context_;
    zk::client client_;

public:
    zookeeper(net::io_context& io):
        io_context_{io},
        client_{zk::client::connect("zk://ow-invoker-1:2181").get()}
    { }

    void test()
    {
        std::vector<char> v{65, 66, 65};
        client_.set("/slsfs/members", v)
            .then([](auto v) { BOOST_LOG_TRIVIAL(info) << "set members: " << v.get(); });
        client_.get("/slsfs/members")
            .then([](auto v) { BOOST_LOG_TRIVIAL(info) << "get members: " << v.get(); });
    }
};

} // namespace launcher


#endif // ZOOKEEPER_HPP__
