//#pragma once
#ifndef HTTP_H
#define HTTP_H


#include <boost/asio/connect.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>

#include <string>

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;

namespace util
{

    class HTTPSession
    {
        using Request = http::request<http::string_body>;
        using Response = http::response<http::string_body>;

    public:

        http::response<http::string_body> get_config(const std::string& _uri, const std::string& _target);
    private:
        http::response<http::string_body> request(http::request<http::string_body> req);
    private:
        net::io_context ioc;
    };
}
#endif
