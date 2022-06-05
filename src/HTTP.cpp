
#include "HTTP.hpp"

#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/asio/ssl/error.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <boost/beast/version.hpp>


namespace ssl = boost::asio::ssl;
using tcp = net::ip::tcp;

namespace util
{

    http::response<http::string_body> HTTPSession::get_config(const std::string& _uri, const std::string& _target){

        http::request<http::string_body> req(http::verb::get, _target, 11);
        // Задаём поля HTTPS заголовка
        req.set(http::field::host, _uri.c_str());
        req.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);
        ssl::context ctx{ssl::context::tls_client};
        ctx.set_default_verify_paths();

        tcp::resolver resolver{ioc};
        ssl::stream<tcp::socket> stream{ioc, ctx};

        // Set SNI Hostname (many hosts need this to handshake successfully)
        if (!SSL_set_tlsext_host_name(stream.native_handle(), _uri.c_str())) {
            boost::system::error_code ec{static_cast<int>(::ERR_get_error()),                                     net::error::get_ssl_category()};
            throw boost::system::system_error{ec};
        }

        auto const results = resolver.resolve(_uri.c_str(), "443");
        net::connect(stream.next_layer(), results.begin(), results.end());
        stream.handshake(ssl::stream_base::client);

        if (req.method() == http::verb::post) {
            req.set(http::field::content_type, "application/json");
        }

        http::write(stream, req);
        boost::beast::flat_buffer buffer;
        http::response<http::string_body> response;
        http::read(stream, buffer, response);

        boost::system::error_code ec;
        stream.shutdown(ec);
        if (ec == boost::asio::error::eof) {
            ec.assign(0, ec.category());
        }

        return response;
    }
}

