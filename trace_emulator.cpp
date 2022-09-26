#include "trace_reader.hpp"
#include "basic.hpp"
#include "serializer.hpp"

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

    for (; start != end; start++)
        var += std::pow((*start) - mean, 2);

    var /= size;
    BOOST_LOG_TRIVIAL(info) << fmt::format("{0} avg={1:.3f} sd={2:.3f}", memo, mean, std::sqrt(var));
}

void readtest (int const times, int const bufsize, std::function<int(int)> genpos, std::string const filename ,std::string const memo = "")
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
            7, 8, 7, 8, 7, 8, 7, 9};
        std::string const payload = fmt::format("{{\"operation\": \"read\", \"filename\": \"/{}\", \"type\": \"file\", \"position\": {}, \"size\": {} }}", filename, genpos(i), buf.size());
        std::copy(payload.begin(), payload.end(), std::back_inserter(ptr->data.buf));

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
            //BOOST_LOG_TRIVIAL(info) << data ;
        }));
    }

    stats(records.begin(), records.end(), fmt::format("read {}", memo));
}

void writetest (int const times, int const bufsize, std::function<int(int)> genpos, std::string const filename, std::string const memo = "")
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
        std::string const payload = fmt::format("{{ \"operation\": \"write\", \"filename\": \"/{}\", \"type\": \"file\", \"position\": {}, \"size\": {}, \"data\": \"{}\" }}", filename, genpos(i), buf.size(), buf);
        //= std::string("{\"operation\": \"write\", \"filename\": \"/helloworld.txt\", \"type\": \"file\", \"position\": 0, \"size\": ") + buf.size() + ", \"data\": \"" + buf + "\"}";
        std::copy(payload.begin(), payload.end(), std::back_inserter(ptr->data.buf));

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
            //BOOST_LOG_TRIVIAL(info) << data ;
       }));
    }
    stats(records.begin(), records.end(), fmt::format("write {}", memo));
}

int main(int argc, char const *argv[])
{
    if (argc < 2) {
        printf("usage: trace_emulator num \n");
    }

    printf("%s\n",argv[1]);
    trace_parser parser(argv[1], 10);

    for (auto row = parser.trace_begin(); row != parser.trace_end(); row++)
    {
        if (row->access_type == read_op) {
            readtest(1, row->blob_bytes, 0, "seq");
        } else {
            writetest(1, row->blob_bytes, 0, "seq");
        }
    }
    return 0;
}
