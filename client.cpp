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

int main()
{
    basic::init_log();
    boost::asio::io_context io_context;
    tcp::socket s(io_context);
    tcp::resolver resolver(io_context);
    boost::asio::connect(s, resolver.resolve("zion01", "12000"));

    record([&](){ ; }, "base");

    pack::packet_pointer ptr = std::make_shared<pack::packet>();
    ptr->header.type = pack::msg_t::put;
    ptr->header.key = pack::key_t{7, 8, 7, 8, 7, 8, 7, 8,
                                  7, 8, 7, 8, 7, 8, 7, 8,
                                  7, 8, 7, 8, 7, 8, 7, 8,
                                  7, 8, 7, 8, 7, 8, 7, 8};

    int const times = 10000;

    {
        BOOST_LOG_TRIVIAL(trace) << "connecting to zion01:12000";
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<pack::unit_t> distrib(1, 6);

        std::generate_n(std::back_inserter(ptr->data.buf), 4, [&] { return distrib(gen); });

        BOOST_LOG_TRIVIAL(trace) << "writinging to zion01:12000";

        long int put_write_counter = 0;
        long int put_read_counter = 0;
        long int put = 0;
        long int get_write_counter = 0;
        long int get_header_read_counter = 0;
        long int get_body_read_counter = 0;
        long int get = 0;

        for (int i = 0; i < times; i++)
        {
            ptr->header.gen();
            auto buf = ptr->serialize();
            auto const tstart = std::chrono::high_resolution_clock::now();
            boost::asio::write(s, boost::asio::buffer(buf->data(), buf->size()));
            auto const tsend = std::chrono::high_resolution_clock::now();

            put_write_counter += std::chrono::duration_cast<std::chrono::nanoseconds>(tsend - tstart).count();

            pack::packet_pointer resp = std::make_shared<pack::packet>();
            std::vector<pack::unit_t> headerbuf(pack::packet_header::bytesize);

            auto const treadstart = std::chrono::high_resolution_clock::now();
            boost::asio::read(s, boost::asio::buffer(headerbuf.data(), headerbuf.size()));
            auto const treadend = std::chrono::high_resolution_clock::now();

            put_read_counter += std::chrono::duration_cast<std::chrono::nanoseconds>(treadend - treadstart).count();

            put += std::chrono::duration_cast<std::chrono::nanoseconds>(treadend - tstart).count();

            resp->header.parse(headerbuf.data());
            BOOST_LOG_TRIVIAL(debug) << resp->header;

            {
                auto rptr = std::make_shared<pack::packet>();
                rptr->header = ptr->header;
                rptr->header.gen_sequence();
                rptr->header.type = pack::msg_t::get;

                auto buf = rptr->serialize();
                BOOST_LOG_TRIVIAL(trace) << "get writing";

                auto const tgetstart = std::chrono::high_resolution_clock::now();
                boost::asio::write(s, boost::asio::buffer(buf->data(), buf->size()));
                auto const tgetend = std::chrono::high_resolution_clock::now();

                get_write_counter += std::chrono::duration_cast<std::chrono::nanoseconds>(tgetend - tgetstart).count();

                pack::packet_pointer resp = std::make_shared<pack::packet>();
                std::vector<pack::unit_t> headerbuf(pack::packet_header::bytesize);

                BOOST_LOG_TRIVIAL(trace) << "get reading";

                auto const tgetheader_readstart = std::chrono::high_resolution_clock::now();
                boost::asio::read(s, boost::asio::buffer(headerbuf.data(), headerbuf.size()));
                auto const tgetheader_readend = std::chrono::high_resolution_clock::now();

                get_header_read_counter += std::chrono::duration_cast<std::chrono::nanoseconds>(tgetheader_readend - tgetheader_readstart).count();

                resp->header.parse(headerbuf.data());
                std::vector<pack::unit_t> bodybuf(resp->header.datasize);

                auto const tgetbody_readstart = std::chrono::high_resolution_clock::now();
                boost::asio::read(s, boost::asio::buffer(bodybuf.data(), bodybuf.size()));
                auto const tgetbody_readend = std::chrono::high_resolution_clock::now();

                get_body_read_counter += std::chrono::duration_cast<std::chrono::nanoseconds>(tgetbody_readend - tgetbody_readstart).count();
                resp->data.parse(resp->header.datasize, bodybuf.data());
                get += std::chrono::duration_cast<std::chrono::nanoseconds>(tgetbody_readend - tgetstart).count();
            }
        }

        std::cout << "put_write_counter " << put_write_counter / times << " ns\n";
        std::cout << "put_read_counter " << put_read_counter / times << " ns\n";
        std::cout << "put " << put / times << " ns\n";
        std::cout << "get_write_counter " << get_write_counter / times << " ns\n";
        std::cout << "get_header_read_counter " << get_header_read_counter / times << " ns\n";
        std::cout << "get_body_read_counter " << get_body_read_counter / times << " ns\n";
        std::cout << "get " << get / times << " ns\n";
    }



//    {
//        BOOST_LOG_TRIVIAL(trace) << "connecting to zion01:12000";
//        std::random_device rd;
//        std::mt19937 gen(rd());
//        std::uniform_int_distribution<pack::unit_t> distrib(1, 6);
//
//        std::generate_n(std::back_inserter(ptr->data.buf), 4, [&] { return distrib(gen); });
//        for (pack::unit_t i : ptr->data.buf)
//            BOOST_LOG_TRIVIAL(trace) << "gen: " <<static_cast<int>(i);
//
//        BOOST_LOG_TRIVIAL(trace) << "writinging to zion01:12000";
//        auto buf = ptr->serialize();
//
//        long int counter = 0;
//        for (int i = 0; i < times; i++)
//        {
//            auto const start = std::chrono::high_resolution_clock::now();
//            boost::asio::write(s, boost::asio::buffer(buf->data(), buf->size()));
//
//            pack::packet_pointer resp = std::make_shared<pack::packet>();
//            std::vector<pack::unit_t> headerbuf(pack::packet_header::bytesize);
//            boost::asio::read(s, boost::asio::buffer(headerbuf.data(), headerbuf.size()));
//            auto const now = std::chrono::high_resolution_clock::now();
//            auto relativetime = std::chrono::duration_cast<std::chrono::nanoseconds>(now - start).count();
//
//            resp->header.parse(headerbuf.data());
//            BOOST_LOG_TRIVIAL(info) << resp->header;
//            counter += relativetime;
//        }
//
//        std::cout << counter / times << " ns\n";
//    }
//
//    {
//        auto rptr = std::make_shared<pack::packet>();
//        rptr->header = ptr->header;
//        rptr->header.gen_sequence();
//        rptr->header.type = pack::msg_t::get;
//
//        auto buf = rptr->serialize();
//        auto const start = std::chrono::high_resolution_clock::now();
//        BOOST_LOG_TRIVIAL(trace) << "get writing";
//        boost::asio::write(s, boost::asio::buffer(buf->data(), buf->size()));
//        auto const now = std::chrono::high_resolution_clock::now();
//
//        pack::packet_pointer resp = std::make_shared<pack::packet>();
//        std::vector<pack::unit_t> headerbuf(pack::packet_header::bytesize);
//
//        BOOST_LOG_TRIVIAL(trace) << "get reading";
//        boost::asio::read(s, boost::asio::buffer(headerbuf.data(), headerbuf.size()));
//        auto const next = std::chrono::high_resolution_clock::now();
//        BOOST_LOG_TRIVIAL(info)
//            << std::chrono::duration_cast<std::chrono::nanoseconds>(now - start).count() << " "
//            << std::chrono::duration_cast<std::chrono::nanoseconds>(next - now).count() ;
//
//        resp->header.parse(headerbuf.data());
//        BOOST_LOG_TRIVIAL(trace) << "get reading header = " << resp->header;
//
//        BOOST_LOG_TRIVIAL(trace) << "resp header " << resp->header;
//        BOOST_LOG_TRIVIAL(trace) << "reading body from zion01:12000";
//        std::vector<pack::unit_t> bodybuf(resp->header.datasize);
//
//        long int counter = 0;
//        for (int i = 0; i < times; i++)
//            counter += record([&](){ boost::asio::read(s, boost::asio::buffer(bodybuf.data(), bodybuf.size())); }, "get");
//        std::cout << counter / times << " ns\n";
//
//        resp->data.parse(resp->header.datasize, bodybuf.data());
//
//        for (pack::unit_t i : resp->data.buf)
//            BOOST_LOG_TRIVIAL(trace) << "read: " <<static_cast<int>(i);
//    }

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
