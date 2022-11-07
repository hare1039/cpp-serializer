#include "basic.hpp"
#include "serializer.hpp"
#include "json-replacement.hpp"

#include <fmt/core.h>
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
auto record(Function &&f) -> long int
{
    //std::chrono::high_resolution_clock::time_point;
    auto const start = std::chrono::high_resolution_clock::now();
    std::invoke(f);
    auto const now = std::chrono::high_resolution_clock::now();
    auto relativetime = std::chrono::duration_cast<std::chrono::nanoseconds>(now - start).count();
    return relativetime;
}

template<typename Iterator>
void stats(Iterator start, Iterator end, std::string const memo = "")
{
    int const size = std::distance(start, end);

    double sum = std::accumulate(start, end, 0.0);
    double mean = sum / size, var = 0;

    std::map<int, int> dist;
    for (; start != end; start++)
    {
        dist[(*start)/1000000]++;
        var += std::pow((*start) - mean, 2);
    }

    var /= size;
    BOOST_LOG_TRIVIAL(info) << fmt::format("{0} avg={1:.3f} sd={2:.3f}", memo, mean, std::sqrt(var));
    for (auto && [time, count] : dist)
        BOOST_LOG_TRIVIAL(info) << fmt::format("{0} {1}: {2}", memo, time, count);
}

void readtest (int const times, int const bufsize, std::function<int(int)> genpos, std::string const memo = "")
{
    boost::asio::io_context io_context;
    tcp::socket s(io_context);
    tcp::resolver resolver(io_context);
    boost::asio::connect(s, resolver.resolve("ow-ctrl", "12000"));

    std::string const buf(bufsize, 'A');
    std::list<double> records;

    for (int i = 0; i < times; i++)
    {
        pack::packet_pointer ptr = std::make_shared<pack::packet>();

        ptr->header.type = pack::msg_t::trigger;
        ptr->header.key = pack::key_t{
            7, 8, 7, 8, 7, 8, 7, 8,
            7, 8, 7, 8, 7, 8, 7, 8,
            7, 8, 7, 8, 7, 8, 7, 8,
            7, 8, 7, 8, 7, 8, 7, 8};

        //std::string const payload = fmt::format("{{\"operation\": \"read\", \"filename\": \"/embl1.txt\", \"type\": \"file\", \"position\": {}, \"size\": {} }}", genpos(i), buf.size());
        jsre::request r;
        r.type = jsre::type_t::file;
        r.operation = jsre::operation_t::read;
        r.uuid = ptr->header.key;
        r.position = genpos(i);
        r.size = buf.size();
        r.to_network_format();

        ptr->data.buf.resize(sizeof (r));
        std::memcpy(ptr->data.buf.data(), &r, sizeof (r));

        //std::copy(payload.begin(), payload.end(), std::back_inserter(ptr->data.buf));

        ptr->header.gen();
        auto sendbuf = ptr->serialize();
        records.push_back(record([&]() {
            boost::asio::write(s, boost::asio::buffer(sendbuf->data(), sendbuf->size()));

            pack::packet_pointer resp = std::make_shared<pack::packet>();
            std::vector<pack::unit_t> headerbuf(pack::packet_header::bytesize);
            boost::asio::read(s, boost::asio::buffer(headerbuf.data(), headerbuf.size()));

            resp->header.parse(headerbuf.data());
            BOOST_LOG_TRIVIAL(debug) << "read resp: " << resp->header;

            std::string data(resp->header.datasize, '\0');
            boost::asio::read(s, boost::asio::buffer(data.data(), data.size()));
            BOOST_LOG_TRIVIAL(info) << data ;
        }));
    }

    stats(records.begin(), records.end(), fmt::format("read {}", memo));
}

void writetest (int const times, int const bufsize, std::function<int(int)> genpos, std::string const memo = "")
{
    boost::asio::io_context io_context;
    tcp::socket s(io_context);
    tcp::resolver resolver(io_context);
    boost::asio::connect(s, resolver.resolve("ow-ctrl", "12000"));

    std::string const buf(bufsize, 'A');

    std::list<double> records;
    for (int i = 0; i < times; i++)
    {
        pack::packet_pointer ptr = std::make_shared<pack::packet>();

        ptr->header.type = pack::msg_t::trigger;
        ptr->header.key = pack::key_t{
            7, 8, 7, 8, 7, 8, 7, 8,
            7, 8, 7, 8, 7, 8, 7, 8,
            7, 8, 7, 8, 7, 8, 7, 8,
            7, 8, 7, 8, 7, 8, 7, 8};
        //std::string const payload = fmt::format("{{ \"operation\": \"write\", \"filename\": \"/embl1.txt\", \"type\": \"file\", \"position\": {}, \"size\": {}, \"data\": \"{}\" }}", genpos(i), buf.size(), buf);

        jsre::request r;
        r.type = jsre::type_t::file;
        r.operation = jsre::operation_t::write;
        r.uuid = ptr->header.key;
        r.position = genpos(i);
        r.size = buf.size();

        r.to_network_format();

        ptr->data.buf.resize(sizeof (r) + buf.size());
        std::memcpy(ptr->data.buf.data(), &r, sizeof (r));
        std::memcpy(ptr->data.buf.data() + sizeof (r), buf.data(), buf.size());

        //= std::string("{\"operation\": \"write\", \"filename\": \"/helloworld.txt\", \"type\": \"file\", \"position\": 0, \"size\": ") + buf.size() + ", \"data\": \"" + buf + "\"}";
        //std::copy(payload.begin(), payload.end(), std::back_inserter(ptr->data.buf));

        ptr->header.gen();
        auto buf = ptr->serialize();

        records.push_back(record([&]() {
            boost::asio::write(s, boost::asio::buffer(buf->data(), buf->size()));

            pack::packet_pointer resp = std::make_shared<pack::packet>();
            std::vector<pack::unit_t> headerbuf(pack::packet_header::bytesize);
            boost::asio::read(s, boost::asio::buffer(headerbuf.data(), headerbuf.size()));

            resp->header.parse(headerbuf.data());
            BOOST_LOG_TRIVIAL(debug) << "write resp:" << resp->header;

            std::string data(resp->header.datasize, '\0');
            boost::asio::read(s, boost::asio::buffer(data.data(), data.size()));
            BOOST_LOG_TRIVIAL(info) << data ;
       }));
    }
    stats(records.begin(), records.end(), fmt::format("write {}", memo));
}

int main(int argc, char *argv[])
{
    basic::init_log();

    boost::log::core::get()->set_filter(
        boost::log::trivial::severity >= boost::log::trivial::trace);

    if (argc < 4)
    {
        BOOST_LOG_TRIVIAL(fatal) << "usage: ./client [times] [# of client]";
        return -1;
    }

    int const times = std::stoi(argv[1]);
    int const clients = std::stoi(argv[2]);
    int const bufsize = std::stoi(argv[3]);


    //record([&](){ ; }, "base");

    writetest(2, 4096, [](int i) { return 0;}, "once");
    readtest(1, 4096, [](int i) { return 0;}, "once");


    return 0;

    {
        std::vector<std::thread> v;
        for (int i = 0; i < clients; i++)
            v.emplace_back(writetest, times, bufsize,
                           [bufsize](int i) { return i*bufsize; }, "seq");

        for (std::thread& th : v)
            th.join();
    }

//    {
//        std::mt19937 engine(19937);
//        std::uniform_int_distribution<> dist(0, 1000);
//        std::vector<std::thread> v;
//        for (int i = 0; i < clients; i++)
//            v.emplace_back(writetest, times, bufsize,
//                           [&engine, &dist](int ) { return dist(engine); }, "rand");
//
//        for (std::thread& th : v)
//            th.join();
//    }
//
//    {
//        std::vector<std::thread> v;
//        for (int i = 0; i < clients; i++)
//            v.emplace_back(readtest, times, bufsize,
//                           [bufsize](int i) { return i*bufsize; }, "seq");
//
//        for (std::thread& th : v)
//            th.join();
//    }
//
    {
        std::mt19937 engine(19937);
        std::uniform_int_distribution<> dist(0, 1000);
        std::vector<std::thread> v;
        for (int i = 0; i < clients; i++)
            v.emplace_back(readtest, times, bufsize,
                           [&engine, &dist](int) { return dist(engine); }, "rand");

        for (std::thread& th : v)
            th.join();
    }

    return EXIT_SUCCESS;
}
