#pragma once
#ifndef LAUNCHER_HPP__
#define LAUNCHER_HPP__

#include "basic.hpp"
#include "serializer.hpp"
#include "worker.hpp"
#include "uuid.hpp"

#include <oneapi/tbb/concurrent_unordered_set.h>
#include <oneapi/tbb/concurrent_queue.h>
#include <oneapi/tbb/concurrent_hash_map.h>

#include <boost/signals2.hpp>

#include <iterator>
#include <atomic>

namespace launcher
{

class job
{
public:
    enum class state : std::uint8_t
    {
        registered,
        started,
        finished
    };
    state state_ = state::registered;

    using on_completion_callable = boost::signals2::signal<void (pack::packet_pointer)>;
    on_completion_callable on_completion_;
    pack::packet_pointer pack_;

    boost::asio::steady_timer timer_;

    template<typename Next>
    job (net::io_context& ioc, pack::packet_pointer p, Next && next):
        pack_{p}, timer_{ioc} { on_completion_.connect(next); }
};

using job_ptr = std::shared_ptr<job>;
template<typename T>
concept CanResolveZookeeper = requires(T t)
{
    { t.get_uuid(std::declval<std::string>()) }
        -> std::convertible_to<net::ip::tcp::endpoint>;
};


class launcher
{
    net::io_context& io_context_;
    std::shared_ptr<trigger::invoker<beast::ssl_stream<beast::tcp_stream>>> itrigger_;

    oneapi::tbb::concurrent_hash_map<std::shared_ptr<df::worker>, int /* not used */> worker_set_;
    using worker_set_accessor = decltype(worker_set_)::accessor;

    oneapi::tbb::concurrent_hash_map<pack::packet_header,
                                     std::shared_ptr<df::worker>,
                                     pack::packet_header_key_hash_compare> fileid_to_worker_;
    using fileid_to_worker_accessor = decltype(fileid_to_worker_)::accessor;
    using fileid_worker_pair = decltype(fileid_to_worker_)::value_type;

    oneapi::tbb::concurrent_queue<job_ptr> pending_jobs_;
    using jobmap =
        oneapi::tbb::concurrent_unordered_map<
            pack::packet_header,
            job_ptr,
            pack::packet_header_key_hash,
            pack::packet_header_key_compare>;
    jobmap started_jobs_;
    net::io_context::strand started_jobs_strand_, job_launch_strand_;
    uuid::uuid const& id_;
    std::string announce_host_;
    net::ip::port_type announce_port_;

public:
    launcher(net::io_context& io, uuid::uuid const& id, std::string const& announce, net::ip::port_type port):
        io_context_{io},
        started_jobs_strand_{io},
        job_launch_strand_{io},
        id_{id},
        announce_host_{announce},
        announce_port_{port} {}

    void add_worker(tcp::socket socket, pack::packet_pointer)
    {
        auto worker_ptr = std::make_shared<df::worker>(io_context_, std::move(socket), *this);
        bool ok = worker_set_.emplace(worker_ptr, 0);

        if (not ok)
            BOOST_LOG_TRIVIAL(error) << "Emplace worker not success";

        worker_ptr->start_read_header();
        start_jobs();
    }

    void on_worker_response(pack::packet_pointer pack)
    {
        net::post(
            net::bind_executor(
                started_jobs_strand_,
                [this, pack] () {
                    job_ptr j = started_jobs_[pack->header];
                    j->on_completion_(pack);
                    j->state_ = job::state::finished;
                    BOOST_LOG_TRIVIAL(info) << "job " << j->pack_->header << " complete";
                }));
    }

    void on_worker_ack(pack::packet_pointer pack)
    {
        net::post(
            net::bind_executor(
                started_jobs_strand_,
                [this, pack] () {
                    BOOST_LOG_TRIVIAL(debug) << "job " << pack->header << " get ack. cancel job timer";
                    if (pack->empty())
                        return;
                    job_ptr j = started_jobs_[pack->header];
                    //BOOST_LOG_TRIVIAL(debug) << "job " << j->pack_->header << " get ack. cancel job timer";
                    j->state_ = job::state::started;
                    j->timer_.cancel();
                }));
    }

    void on_worker_close(std::shared_ptr<df::worker> worker) {
        worker_set_.erase(worker);
    }

    auto get_worker_from_pool(pack::packet_pointer /*packet_ptr*/) -> std::shared_ptr<df::worker>
    {
        for (auto&& worker_pair : worker_set_)
        {
            if (worker_pair.first->pending_jobs() <= 5 and worker_pair.first->is_valid())
                return worker_pair.first;
        }
        return nullptr;
    }

    auto get_assigned_worker(pack::packet_pointer packet_ptr) -> std::shared_ptr<df::worker>
    {
        fileid_to_worker_accessor it;
        if (bool found = fileid_to_worker_.find(it, packet_ptr->header); found)
            return it->second;
        else
        {
            auto reuse_worker = get_worker_from_pool(packet_ptr);
            if (!reuse_worker)
            {
                BOOST_LOG_TRIVIAL(info) << "No avaliable worker. Start one.";
                create_worker(fmt::format("{{ \"type\": \"wakeup\", \"host\": \"{}\", \"port\": \"{}\" }}",
                                          announce_host_, announce_port_));
                return nullptr;
            }
            else
            {
                BOOST_LOG_TRIVIAL(trace) << "Reuse worker";
                if (not fileid_to_worker_.emplace(packet_ptr->header, reuse_worker))
                    return nullptr;
                else
                    return reuse_worker;
            }
        }
    }

    void start_jobs()
    {
        job_ptr j;
        while (pending_jobs_.try_pop(j))
        {
            BOOST_LOG_TRIVIAL(trace) << "Starting jobs";
            std::shared_ptr<df::worker> worker_ptr = get_assigned_worker(j->pack_);

            if (!worker_ptr)
            {
                pending_jobs_.push(j);
                break;
            }

            BOOST_LOG_TRIVIAL(trace) << "Starting jobs, Start post. ";
            started_jobs_.emplace(j->pack_->header, j);
            worker_ptr->start_write(j->pack_);

            using namespace std::chrono_literals;
            j->timer_.expires_from_now(2s);
            j->timer_.async_wait(
                [this, j] (boost::system::error_code ec) {
                    if (ec && ec != boost::asio::error::operation_aborted)
                    {
                        BOOST_LOG_TRIVIAL(debug) << "error: " << ec << "repush job " << j->pack_->header;
                        pending_jobs_.push(j);
                    }
                });
            BOOST_LOG_TRIVIAL(info) << "start job " << j->pack_->header;
        }
    }

    void create_worker(std::string const& body)
    {
        static std::string const url = "https://ow-ctrl/api/v1/namespaces/_/actions/slsfs-datafunction?blocking=false&result=false";

        if (not itrigger_)
            itrigger_ = std::make_shared<trigger::invoker<beast::ssl_stream<beast::tcp_stream>>>(io_context_, url, basic::ssl_ctx());

        net::post(
            net::bind_executor(
                job_launch_strand_,
                [this, body] {
                    itrigger_->start_post(body);
                }));
    }

    template<typename Callback>
    void start_trigger_post(std::string const& body, pack::packet_pointer original_pack, Callback next)
    {
        pack::packet_pointer pack = std::make_shared<pack::packet>();
        pack->header = original_pack->header;
//        pack->header.gen();
        pack->header.type = pack::msg_t::worker_push_request;
        pack->data.buf = std::vector<pack::unit_t>(body.size());
        std::memcpy(pack->data.buf.data(), body.data(), body.size());

        auto j = std::make_shared<job>(io_context_, pack, next);
        pending_jobs_.push(j);
        start_jobs();
    }

    // assumes begin -> end are sorted
    template<std::forward_iterator ForwardIterator, CanResolveZookeeper Zookeeper>
    void reconfigure(ForwardIterator begin, ForwardIterator end, Zookeeper&& zoo)
    {
        BOOST_LOG_TRIVIAL(trace) << "launcher reconfigure";
        for (auto&& pair : fileid_to_worker_)
        {
            auto it = std::upper_bound (begin, end, pair.first.key);
            if (it == end)
                it = begin;

            if (*it != id_)
                start_send_reconfigure_message(pair, zoo.get_uuid(it->encode_base64()));
        }
    }

    void start_send_reconfigure_message(fileid_worker_pair & pair, net::ip::tcp::endpoint new_proxy)
    {
        BOOST_LOG_TRIVIAL(debug) << "start_send_reconfigure_message: " << new_proxy;

        pack::packet_pointer p = std::make_shared<pack::packet>();
        p->header = pair.first;
        p->header.type = pack::msg_t::proxyjoin;

        auto ip = new_proxy.address().to_v4().to_bytes();
        auto port = pack::hton(new_proxy.port());
        p->data.buf = std::vector<pack::unit_t>(sizeof(ip) + sizeof(port));
        std::copy(ip.begin(), ip.end(), p->data.buf.begin());
        std::memcpy(p->data.buf.data() + ip.size(), &port, sizeof(port));

        pair.second->start_write(p);
    }
};

} // namespace launcher


#endif // LAUNCHER_HPP__

//        if (get_available_worker() == nullptr)
//        {
//            BOOST_LOG_TRIVIAL(debug) << "starting function as new worker";
//            create_worker(body);
//            itrigger_->register_on_read(
//                [next](std::shared_ptr<http::response<http::string_body>> /*resp*/) {});
//        }
