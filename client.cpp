#include "basic.hpp"
#include "serializer.hpp"

#include <boost/asio.hpp>

#include <algorithm>
#include <iostream>

#include <memory>
#include <array>
#include <list>
#include <thread>
#include <vector>
#include <random>

using boost::asio::ip::tcp;

int main(int argc, char* argv[])
{
    basic::init_log();
    boost::asio::io_context io_context;
    tcp::socket s(io_context);
    tcp::resolver resolver(io_context);
    boost::asio::connect(s, resolver.resolve("zion01", "12000"));

    std::thread th([&](){
                       auto ptr = std::make_shared<pack::packet>();
                       //return;
                       // 3
                       ptr->header.type = pack::msg_t::get;
                       ptr->data.buf = std::vector<pack::unit_t>{};
                       ptr->header.buf = pack::key_t{7, 8, 7, 8, 7, 8, 7, 8,
                                                     7, 8, 7, 8, 7, 8, 7, 8,
                                                     7, 8, 7, 8, 7, 8, 7, 8,
                                                     7, 8, 7, 8, 7, 8, 7, 8,
                                                     7, 8, 7, 8, 7, 8, 7, 9};
                       auto buf = ptr->serialize();
                       boost::asio::write(s, boost::asio::buffer(buf->data(), buf->size()));

                       pack::packet_pointer resp = std::make_shared<pack::packet>();
                       std::vector<pack::unit_t> headerbuf(pack::packet_header::bytesize);

                       BOOST_LOG_TRIVIAL(trace) << "reading from zion01:12000";
                       boost::asio::read(s, boost::asio::buffer(headerbuf.data(), headerbuf.size()));
                       resp->header.parse(headerbuf.data());

                       BOOST_LOG_TRIVIAL(trace) << "resp header " << resp->header;
                       BOOST_LOG_TRIVIAL(trace) << "reading body from zion01:12000";
                       std::vector<pack::unit_t> bodybuf(resp->header.datasize);
                       boost::asio::read(s, boost::asio::buffer(bodybuf.data(), bodybuf.size()));

                       resp->data.parse(resp->header.datasize, bodybuf.data());

                       for (pack::unit_t i : resp->data.buf)
                           BOOST_LOG_TRIVIAL(trace) << "read: " <<static_cast<int>(i);
                   });

    std::this_thread::sleep_for(std::chrono::seconds(1));
    // 1
    {
        BOOST_LOG_TRIVIAL(trace) << "connecting to zion01:12000";
        pack::packet_pointer ptr = std::make_shared<pack::packet>();
        ptr->header.type = pack::msg_t::put;
        ptr->header.buf = pack::key_t{7, 8, 7, 8, 7, 8, 7, 8,
                                      7, 8, 7, 8, 7, 8, 7, 8,
                                      7, 8, 7, 8, 7, 8, 7, 8,
                                      7, 8, 7, 8, 7, 8, 7, 8,
                                      7, 8, 7, 8, 7, 8, 7, 9};

        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<pack::unit_t> distrib(1, 6);

        std::generate_n(std::back_inserter(ptr->data.buf), 4, [&] { return distrib(gen); });
        for (pack::unit_t i : ptr->data.buf)
            BOOST_LOG_TRIVIAL(trace) << "gen: " <<static_cast<int>(i);


        BOOST_LOG_TRIVIAL(trace) << "writinging to zion01:12000";
        auto buf = ptr->serialize();
        BOOST_LOG_TRIVIAL(trace) << ptr->header;

        boost::asio::write(s, boost::asio::buffer(buf->data(), buf->size()));
    }
    {
        BOOST_LOG_TRIVIAL(trace) << "call_register to zion01:12000";
        auto ptr = std::make_shared<pack::packet>();
        ptr->header.type = pack::msg_t::call_register;
        ptr->header.buf = pack::key_t{7, 8, 7, 8, 7, 8, 7, 8,
                                      7, 8, 7, 8, 7, 8, 7, 8,
                                      7, 8, 7, 8, 7, 8, 7, 8,
                                      7, 8, 7, 8, 7, 8, 7, 8,
                                      7, 8, 7, 8, 7, 8, 7, 8};

        std::string url="http://zion01:2016/api/v1/namespaces/_/actions/example-app-slsfs?blocking=false&result=false";
        std::copy(url.begin(), url.end(), std::back_inserter(ptr->data.buf));
//
        BOOST_LOG_TRIVIAL(trace) << "writinging to zion01:12000";
        auto buf = ptr->serialize();
        BOOST_LOG_TRIVIAL(trace) << ptr->header;
//
        boost::asio::write(s, boost::asio::buffer(buf->data(), buf->size()));
    }

    {
        BOOST_LOG_TRIVIAL(trace) << "issueing to zion01:12000";
        pack::packet_pointer ptr = std::make_shared<pack::packet>();
        ptr->header.type = pack::msg_t::put;
        ptr->header.buf = pack::key_t{7, 8, 7, 8, 7, 8, 7, 8,
                                      7, 8, 7, 8, 7, 8, 7, 8,
                                      7, 8, 7, 8, 7, 8, 7, 8,
                                      7, 8, 7, 8, 7, 8, 7, 8,
                                      7, 8, 7, 8, 7, 8, 7, 8};

        std::string url="{ \"data\": \"super\"}";
        std::copy(url.begin(), url.end(), std::back_inserter(ptr->data.buf));

        BOOST_LOG_TRIVIAL(trace) << "writinging to zion01:12000";
        auto buf = ptr->serialize();
        BOOST_LOG_TRIVIAL(trace) << ptr->header;

        boost::asio::write(s, boost::asio::buffer(buf->data(), buf->size()));
    }

    th.join();
    return EXIT_SUCCESS;
}
