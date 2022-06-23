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
#include <chrono>

using boost::asio::ip::tcp;

template<typename Function>
auto record(Function &&f, std::string memo = "") -> long int
{
    //std::chrono::high_resolution_clock::time_point;
    auto const start = std::chrono::high_resolution_clock::now();
    std::invoke(f);
    auto const now = std::chrono::high_resolution_clock::now();
    auto relativetime = std::chrono::duration_cast<std::chrono::nanoseconds>(now - start).count();
    std::cout << memo << ": " << relativetime << "\n";
    return relativetime;
}

int main(int argc, char* argv[])
{
    basic::init_log();
    boost::asio::io_context io_context;
    tcp::socket s(io_context);
    tcp::resolver resolver(io_context);
    boost::asio::connect(s, resolver.resolve("zion01", "12000"));

    record([&](){ ; }, "base");

    std::array<pack::unit_t, 4> salt;

    {
        BOOST_LOG_TRIVIAL(trace) << "connecting to zion01:12000";
        pack::packet_pointer ptr = std::make_shared<pack::packet>();
        ptr->header.gen_random_salt();
        salt = ptr->header.random_salt;
        ptr->header.gen_sequence();

        ptr->header.type = pack::msg_t::put;
        ptr->header.key = pack::key_t{7, 8, 7, 8, 7, 8, 7, 8,
                                      7, 8, 7, 8, 7, 8, 7, 8,
                                      7, 8, 7, 8, 7, 8, 7, 8,
                                      7, 8, 7, 8, 7, 8, 7, 8};

        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<pack::unit_t> distrib(1, 6);

        std::generate_n(std::back_inserter(ptr->data.buf), 4, [&] { return distrib(gen); });
        for (pack::unit_t i : ptr->data.buf)
            BOOST_LOG_TRIVIAL(trace) << "gen: " <<static_cast<int>(i);

        BOOST_LOG_TRIVIAL(trace) << "writinging to zion01:12000";
        auto buf = ptr->serialize();
        BOOST_LOG_TRIVIAL(trace) << ptr->header;

        long int counter = 0;
        for (int i = 0; i < 1; i++)
            counter += record([&](){ boost::asio::write(s, boost::asio::buffer(buf->data(), buf->size())); }, "put");
        std::cout << counter / 1 << " ns\n";
    }

    std::thread th([&](){
                       auto ptr = std::make_shared<pack::packet>();
                       ptr->header.random_salt = salt;
                       ptr->header.type = pack::msg_t::get;
                       ptr->header.key = pack::key_t{7, 8, 7, 8, 7, 8, 7, 8,
                                                     7, 8, 7, 8, 7, 8, 7, 8,
                                                     7, 8, 7, 8, 7, 8, 7, 8,
                                                     7, 8, 7, 8, 7, 8, 7, 8};
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

                       long int counter = 0;
                       for (int i = 0; i < 1; i++)
                           counter += record([&](){ boost::asio::read(s, boost::asio::buffer(bodybuf.data(), bodybuf.size())); }, "get");
                       std::cout << counter / 1 << " ns\n";

                       resp->data.parse(resp->header.datasize, bodybuf.data());

                       for (pack::unit_t i : resp->data.buf)
                           BOOST_LOG_TRIVIAL(trace) << "read: " <<static_cast<int>(i);
                   });

    th.join();

//    {
//        BOOST_LOG_TRIVIAL(trace) << "issueing to zion01:12000";
//        pack::packet_pointer ptr = std::make_shared<pack::packet>();
//        ptr->header.type = pack::msg_t::put;
//        ptr->header.key = pack::key_t{7, 8, 7, 8, 7, 8, 7, 8,
//                                      7, 8, 7, 8, 7, 8, 7, 8,
//                                      7, 8, 7, 8, 7, 8, 7, 8,
//                                      7, 8, 7, 8, 7, 8, 7, 8};
//
//        std::string url="{ \"data\": \"super\"}";
//        std::copy(url.begin(), url.end(), std::back_inserter(ptr->data.buf));
//
//        BOOST_LOG_TRIVIAL(trace) << "writinging to zion01:12000";
//        auto buf = ptr->serialize();
//        BOOST_LOG_TRIVIAL(trace) << ptr->header;
//
//        long int counter = 0;
//        for (int i = 0; i < 1; i++)
//            counter += record([&](){ boost::asio::write(s, boost::asio::buffer(buf->data(), buf->size())); }, "issueing");
//        std::cout << counter / 1 << " ns\n";
//    }


    return EXIT_SUCCESS;
}
