#include "basic.hpp"
#include "serializer.hpp"
#include "trigger.hpp"

#include <boost/program_options.hpp>
#include <boost/log/trivial.hpp>
#include <boost/asio.hpp>
#include <boost/signals2.hpp>

#include <oneapi/tbb/concurrent_unordered_map.h>
#include <oneapi/tbb/concurrent_queue.h>

#include <algorithm>
#include <iostream>

#include <memory>
#include <array>
#include <list>
#include <thread>
#include <vector>

using net::ip::tcp;

class bucket
{
    net::io_context& io_context_;
    net::io_context::strand event_io_strand_;

    // for receiving messages
    oneapi::tbb::concurrent_queue<pack::packet_data> message_queue_;

    // to issue a request to binded http url when a message comes in
    std::shared_ptr<trigger::invoker> binding_;

    // holds callbacks of listeners
    boost::signals2::signal<void (pack::packet_pointer)> listener_;

public:
    bucket(net::io_context& io):
        io_context_{io},
        event_io_strand_{io} {}

    void to_trigger()
    {
        static std::string const url = "http://zion01:2016/api/v1/namespaces/_/actions/slsfs-datafunction?blocking=false&result=false";
        if (binding_ == nullptr)
            binding_ = std::make_shared<trigger::invoker>(io_context_, url);
    }

    void start_trigger_post(std::string const& body)
    {
        to_trigger();
        binding_->start_post(body);
    }

    template<typename Function>
    void get_connect(Function &&f)
    {
        listener_.connect(std::forward<Function>(f));
    }

    template<typename Msg>
    void push_message(Msg && m) { message_queue_.push(std::forward<Msg>(m)); }

    void start_handle_events(pack::packet_pointer key)
    {
        if (message_queue_.empty())
            return;

        net::post(
            io_context_,
            net::bind_executor(
                event_io_strand_,
                [this, key] {
                    pack::packet_pointer resp = std::make_shared<pack::packet>();
                    resp->header = key->header;
                    resp->header.type = pack::msg_t::response;

                    if (resp->header.is_trigger())
                    {
                        while (message_queue_.try_pop(resp->data))
                        {
                            // trigger
                            std::string body;
                            std::copy(key->data.buf.begin(),
                                      key->data.buf.end(),
                                      std::back_inserter(body));
                            start_trigger_post(body);
                        }
                    }
                    else
                    {
                        if (listener_.empty())
                            return;

                        while (message_queue_.try_pop(resp->data))
                            listener_(resp);

                        BOOST_LOG_TRIVIAL(trace) << "clear listener_ ";
                        listener_.disconnect_all_slots();
                    }
                }));
    }
};

using topics =
    oneapi::tbb::concurrent_unordered_map<
        pack::packet_header,
        bucket,
        pack::packet_header_key_hash,
        pack::packet_header_key_compare>;

class tcp_connection : public std::enable_shared_from_this<tcp_connection>
{
    net::io_context& io_context_;
    topics& topics_;
    tcp::socket socket_;
    net::io_context::strand write_io_strand_;

public:
    using pointer = std::shared_ptr<tcp_connection>;

    tcp_connection(net::io_context& io, topics& s, tcp::socket socket):
        io_context_{io},
        topics_{s},
        socket_{std::move(socket)},
        write_io_strand_{io} {}

    auto socket() -> tcp::socket& { return socket_; }

    auto get_bucket(pack::packet_header &h) -> bucket&
    {
        if (not topics_.contains(h))
            topics_.emplace(h, io_context_);
        return topics_.at(h);
    }

    void start_read_header()
    {
        BOOST_LOG_TRIVIAL(trace) << "start_read_header";
        auto read_buf = std::make_shared<std::array<pack::unit_t, pack::packet_header::bytesize>>();
        net::async_read(
            socket_,
            net::buffer(read_buf->data(), read_buf->size()),
            [self=shared_from_this(), read_buf] (boost::system::error_code ec, std::size_t /*length*/) {
                if (not ec)
                {
                    pack::packet_pointer pack = std::make_shared<pack::packet>();
                    pack->header.parse(read_buf->data());


                    switch (pack->header.type)
                    {
                    case pack::msg_t::put:
                        BOOST_LOG_TRIVIAL(debug) << "put " << pack->header;
                        self->start_read_body(pack);
                        break;

                    case pack::msg_t::get:
                        BOOST_LOG_TRIVIAL(debug) << "get " << pack->header;
                        self->start_load(pack);
                        self->start_read_header();
                        break;

                    case pack::msg_t::error:
                        BOOST_LOG_TRIVIAL(error) << "packet error" << pack->header;
                        self->start_read_header();
                        break;

                    case pack::msg_t::response:
                        BOOST_LOG_TRIVIAL(error) << "server should not get response error" << pack->header;
                        self->start_read_header();
                        break;
                    }
                }
                else
                {
                    if (ec != boost::asio::error::eof)
                        BOOST_LOG_TRIVIAL(error) << ", start_read_header err: " << ec.message();
                }
            });
    }

    void start_read_body(pack::packet_pointer pack)
    {
        BOOST_LOG_TRIVIAL(trace) << "start_read_body";
        auto read_buf = std::make_shared<std::vector<pack::unit_t>>(pack->header.datasize);
        net::async_read(
            socket_,
            net::buffer(read_buf->data(), read_buf->size()),
            [self=shared_from_this(), read_buf, pack] (boost::system::error_code ec, std::size_t length) {
                if (not ec)
                {
                    pack->data.parse(length, read_buf->data());
                    self->start_store(pack);
                    self->start_read_header();
                }
                else
                    BOOST_LOG_TRIVIAL(error) << "start_read_body: " << ec.message();
            });
    }

    void start_store(pack::packet_pointer pack)
    {
        net::post(
            io_context_,
            [pack, self=shared_from_this()] {
                self->get_bucket(pack->header).push_message(pack->data);
                self->get_bucket(pack->header).start_handle_events(pack);
            });
    }

    void start_load(pack::packet_pointer pack)
    {
        BOOST_LOG_TRIVIAL(trace) << "load";
        net::post(
            io_context_,
            [self=shared_from_this(), pack] {
                BOOST_LOG_TRIVIAL(trace) << "load: register listener";

                self->get_bucket(pack->header).get_connect(
                    [self=self->shared_from_this()](pack::packet_pointer pack) {
                        BOOST_LOG_TRIVIAL(trace) << "run signaled write";
                        self->start_write(pack);
                    });

                self->get_bucket(pack->header).start_handle_events(pack);
            });
    }

    void start_write(pack::packet_pointer pack)
    {
        BOOST_LOG_TRIVIAL(trace) << "write";
        auto buf_pointer = pack->serialize();
        net::async_write(
            socket_,
            net::buffer(buf_pointer->data(), buf_pointer->size()),
            net::bind_executor(
                write_io_strand_,
                [self=shared_from_this(), buf_pointer] (boost::system::error_code ec, std::size_t length) {
                    if (not ec)
                        BOOST_LOG_TRIVIAL(debug) << "sent msg";
                }));
    }
};

class tcp_server
{
    net::io_context& io_context_;
    tcp::acceptor acceptor_;
    topics topics_;
public:
    tcp_server(net::io_context& io_context, net::ip::port_type port)
        : io_context_(io_context),
          acceptor_(io_context, tcp::endpoint(tcp::v4(), port)) {
        start_accept();
    }

    void start_accept()
    {
        acceptor_.async_accept(
            [this] (boost::system::error_code const& error, tcp::socket socket) {
                if (not error)
                {
                    auto accepted = std::make_shared<tcp_connection>(
                        io_context_,
                        topics_,
                        std::move(socket));
                    accepted->start_read_header();
                    start_accept();
                }
            });
    }
};

int main(int argc, char* argv[])
{
    basic::init_log();

    namespace po = boost::program_options;
    po::options_description desc{"Options"};
    desc.add_options()
        ("help,h", "Print this help messages")
        ("listen,l", po::value<unsigned short>()->default_value(12000), "listen on this port");
    po::positional_options_description pos_po;
    po::variables_map vm;
    po::store(po::command_line_parser(argc, argv)
              .options(desc)
              .positional(pos_po).run(), vm);
    po::notify(vm);

    if (vm.count("help"))
    {
        BOOST_LOG_TRIVIAL(info) << desc;
        return EXIT_FAILURE;
    }

    int const worker = std::thread::hardware_concurrency();
    net::io_context ioc {worker};
    net::signal_set listener(ioc, SIGINT, SIGTERM);
    listener.async_wait(
        [&ioc](boost::system::error_code const& error, int signal_number) {
            BOOST_LOG_TRIVIAL(info) << "Stopping... sig=" << signal_number;
            ioc.stop();
        });

    unsigned short const port = vm["listen"].as<unsigned short>();

    tcp_server server{ioc, port};
    BOOST_LOG_TRIVIAL(info) << "listen on " << port;

    std::vector<std::thread> v;
    v.reserve(worker);
    for(int i = 1; i < worker; i++)
        v.emplace_back([&ioc] { ioc.run(); });
    ioc.run();

    for (std::thread& th : v)
        th.join();

    return EXIT_SUCCESS;
}
