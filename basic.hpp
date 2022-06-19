#pragma once
#ifndef BASIC_HPP__
#define BASIC_HPP__

#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>

#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/spawn.hpp>
#include <boost/asio.hpp>
#include <boost/config.hpp>

#include <boost/log/trivial.hpp>
#include <boost/log/core.hpp>

#if defined(__clang__)
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wunknown-warning-option"
#endif
#include <boost/log/expressions.hpp>
#if defined(__clang__)
    #pragma clang diagnostic pop
#endif

#include <boost/log/sources/logger.hpp>
#include <boost/log/utility/setup/file.hpp>
#include <boost/log/utility/setup/console.hpp>
#include <boost/log/utility/setup/common_attributes.hpp>
#include <boost/log/support/date_time.hpp>
#include <boost/log/sinks/sync_frontend.hpp>
#include <boost/log/sinks/text_file_backend.hpp>
#include <boost/log/sinks/text_ostream_backend.hpp>
#include <boost/log/attributes/named_scope.hpp>

#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>

#include <boost/archive/iterators/base64_from_binary.hpp>
#include <boost/archive/iterators/binary_from_base64.hpp>
#include <boost/archive/iterators/transform_width.hpp>

#include <boost/json.hpp>

#include <boost/filesystem.hpp>

#pragma GCC diagnostic ignored "-Wunused-result"
#include <boost/process.hpp>
#pragma GCC diagnostic pop

#include <boost/optional.hpp>

#include <boost/date_time/posix_time/posix_time.hpp>

#include <fstream>
#include <charconv>

#include "scope_exit.hpp"

namespace beast = boost::beast;         // from <boost/beast.hpp>
namespace http  = boost::beast::http;   // from <boost/beast/http.hpp>
namespace net   = boost::asio;          // from <boost/asio.hpp>
namespace bp    = boost::process;       // from <boost/process.hpp>
using tcp = boost::asio::ip::tcp;       // from <boost/asio/ip/tcp.hpp>
using namespace std::string_literals;

namespace basic
{

// Report a failure
void fail(beast::error_code ec, char const* what)
{
    BOOST_LOG_TRIVIAL(error) << what << ": " << ec.message() << "\n";
}


void init_log()
{
    boost::log::add_common_attributes();
    boost::log::core::get()->add_global_attribute("Scope",
                                                  boost::log::attributes::named_scope());
//#ifdef NDEBUG
//    boost::log::core::get()->set_filter(
//        boost::log::trivial::severity >= boost::log::trivial::debug
//    );
//#else
    boost::log::core::get()->set_filter(
        boost::log::trivial::severity >= boost::log::trivial::trace
    );
//#endif // NDEBUG

    /* log formatter: https://gist.github.com/xiongjia/e23b9572d3fc3d677e3d
     * [TimeStamp] [Severity Level] [Scope] Log message
     */
    auto timestamp = boost::log::expressions::
        format_date_time<boost::posix_time::ptime>("TimeStamp", "%Y-%m-%d %H:%M:%S.%f");
    auto severity = boost::log::expressions::
        attr<boost::log::trivial::severity_level>("Severity");
    auto scope = boost::log::expressions::format_named_scope("Scope",
        boost::log::keywords::format = "%n(%f:%l)",
        boost::log::keywords::iteration = boost::log::expressions::reverse,
        boost::log::keywords::depth = 2);
    boost::log::formatter final_format =
        boost::log::expressions::format("[%1%] (%2%): %3%")
        % timestamp % severity
        % boost::log::expressions::smessage;

    /* console sink */
    auto console = boost::log::add_console_log(std::clog);
    console->set_formatter(final_format);
}

inline
auto genuuid() -> boost::uuids::uuid
{
    return boost::uuids::random_generator()();
}

template<typename StringView>
auto parse_host(StringView const& remote) -> std::pair<StringView, unsigned short>
{
    std::size_t const sap = remote.find(":");
    unsigned short port = 80;
    if (sap != remote.npos)
    {
        StringView p = remote.substr(sap + 1);
        std::from_chars_result result = std::from_chars(p.data(), p.data() + p.size(), port);
        if (result.ec == std::errc::invalid_argument)
            port = 80;
    }
    return {remote.substr(0, sap), port};
}

namespace sswitcher
{

constexpr inline
auto hash(char const * str, std::size_t h) -> long long int
{
    return (h == 0 ? 5381 : ((hash(str, h-1) * 33) ^ str[h-1]) & 0xFFFFFFFFFFFF);
}

constexpr inline
auto operator "" _(char const * p, std::size_t m) -> long long int { return hash(p, m); }

template<typename StringView> inline
auto hash(StringView const & s) -> long long int { return hash(s.data(), s.size()); }

}// namespace sswitcher

using strhash = long long int;

}// namespace basic

#endif // BASIC_HPP__
