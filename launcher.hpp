#pragma once
#ifndef LAUNCHER_HPP__
#define LAUNCHER_HPP__

#include "basic.hpp"
#include "serializer.hpp"
#include "worker.hpp"

#include <oneapi/tbb/concurrent_unordered_set.h>
#include <oneapi/tbb/concurrent_queue.h>
#include <boost/signals2.hpp>

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

class launcher
{
    net::io_context& io_context_;
    std::shared_ptr<trigger::invoker<beast::ssl_stream<beast::tcp_stream>>> itrigger_;
    oneapi::tbb::concurrent_unordered_set<std::shared_ptr<df::worker>> workers_;
    oneapi::tbb::concurrent_queue<job_ptr> registered_jobs_;
    using jobmap =
        oneapi::tbb::concurrent_unordered_map<
            pack::packet_header,
            job_ptr,
            pack::packet_header_key_hash,
            pack::packet_header_key_compare>;
    jobmap started_jobs_;
    net::io_context::strand started_jobs_strand_, job_launch_strand_;

public:
    launcher(net::io_context& io): io_context_{io}, started_jobs_strand_{io}, job_launch_strand_{io} { }

    void add_worker(tcp::socket socket, pack::packet_pointer /*request*/)
    {
        auto && [it, ok] = workers_.emplace(std::make_shared<df::worker>(io_context_, std::move(socket), *this));
        (*it)->start_read_header();
        start_jobs();
    }

    auto get_available_worker() -> std::shared_ptr<df::worker>
    {
        for (auto&& worker_ptr : workers_)
            if (worker_ptr->is_valid())
                return worker_ptr;
        return nullptr;
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
                    job_ptr j = started_jobs_[pack->header];
                    j->state_ = job::state::started;
                    BOOST_LOG_TRIVIAL(debug) << "job " << j->pack_->header << " get ack";
                    j->timer_.cancel();
                }));
    }

    void start_jobs()
    {
        job_ptr j;
        while (registered_jobs_.try_pop(j))
        {
            BOOST_LOG_TRIVIAL(trace) << "Starting jobs";
            std::shared_ptr<df::worker> worker_ptr = get_available_worker();

            if (!worker_ptr)
            {
                registered_jobs_.push(j);
                create_worker("{ \"type\": \"wakeup\" }");
                BOOST_LOG_TRIVIAL(trace) << "Starting jobs, but no worker. Start one.";
                break;
            }

            BOOST_LOG_TRIVIAL(trace) << "Starting jobs, Start post. ";

            worker_ptr->start_post(j->pack_);

            using namespace std::chrono_literals;
            j->timer_.async_wait(
                [this, j] (boost::system::error_code ec) {
                    if (ec && ec != boost::asio::error::operation_aborted)
                    {
                        BOOST_LOG_TRIVIAL(debug) << "error: " << ec << "repush job " << j->pack_->header;
                        registered_jobs_.push(j);
                    }
                });
            BOOST_LOG_TRIVIAL(info) << "start job " << j->pack_->header;
            j->timer_.expires_from_now(1s);
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
    void start_trigger_post(std::string const& body, Callback next)
    {
        pack::packet_pointer pack = std::make_shared<pack::packet>();

        pack->header.gen();
        pack->header.type = pack::msg_t::worker_push_request;
        pack->data.buf = std::vector<pack::unit_t>(body.size());
        std::memcpy(pack->data.buf.data(), body.data(), body.size());

        auto j = std::make_shared<job>(io_context_, pack, next);
        registered_jobs_.push(j);
        started_jobs_.emplace(pack->header, j);
        start_jobs();

//        if (get_available_worker() == nullptr)
//        {
//            BOOST_LOG_TRIVIAL(debug) << "starting function as new worker";
//            create_worker(body);
//            itrigger_->register_on_read(
//                [next](std::shared_ptr<http::response<http::string_body>> /*resp*/) {});
//        }
    }
};

} // namespace launcher


#endif // LAUNCHER_HPP__
