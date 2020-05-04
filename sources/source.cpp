// Copyright 2018 Your Name <your_email>

#include <header.hpp>
#include <boost/beast.hpp>
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/version.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/error.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <boost/asio/buffers_iterator.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/asio/buffered_stream.hpp>
#include <iostream>
#include <fstream>
#include <string>
#include <gumbo.h>
#include <thread>
#include <functional>
#include <queue>

using tcp = boost::asio::ip::tcp;       // from <boost/asio/ip/tcp.hpp>
namespace ssl = boost::asio::ssl;       // from <boost/asio/ssl.hpp>
namespace http = boost::beast::http;    // from <boost/beast/http.hpp>
namespace beast = boost::beast; // from <boost/beast.hpp>
namespace http = beast::http;   // from <boost/beast/http.hpp>
namespace net = boost::asio;    // from <boost/asio.hpp>
namespace ssl = net::ssl;       // from <boost/asio/ssl.hpp>
using tcp = net::ip::tcp;       // from <boost/asio/ip/tcp.hpp>
static std::queue <std::string> links;
static std::queue <std::string> pages;
//boost::asio::io_context ioc;
//ssl::context ctx(ssl::context::tlsv12_client);
static std::mutex m1, queue_mutex, pics_mutex;
static std::queue <std::string> pics_queue;

struct link {
    explicit link(std::string url) {
        parse_url(url);
    }
    link(std::string host, std::string port,
            std::string target)
    {
        _host = host;
        _port = port;
        _target = target;
    }
    std::string _target;
    std::string _host;
    std::string _port;
    int _version = 10;

    void parse_url(const std::string &url) {
        if (url.find("https") != std::string::npos) {
            _port = "443";
            _host = url.substr(url.find("://") + 3,
                    url.find("/", 8) - url.find("://") - 3);

            if (url.find("/", 8) != std::string::npos) {
                _target = url.substr(url.find("/", 8),
                        url.size() - url.find("/", 8));
            } else {
                _target = "/";
            }
        } else {
            if (url.find("http") != std::string::npos) {
                _port = "80";
                _host = url.substr(url.find("://") + 3,
                        url.find("/", 8) - url.find("://") - 3);
                _target = url.substr(url.find("/", 8),
                        url.size() - url.find("/", 8));
                std::cout << _target;
            } else {
                std::cout << "wrong type of url no http or https" << std::endl;
                return;
            }
        }
        fflush(stdout);
    }
};

struct downloader {
    void search_for_links(GumboNode *node, int _level) {
        if (node->type != GUMBO_NODE_ELEMENT) {
            return;
        }
        GumboAttribute *href;
        if (node->v.element.tag == GUMBO_TAG_A &&
            (href = gumbo_get_attribute(&node->v.element.attributes, "href"))) {
            std::string res(href->value);
            if (res.find("https", 0) != std::string::npos) {
                if (res[0] == 'h') {
                    if (_level > 0) {
                        struct link n1(std::string(href->value));
                        downloader temp;
                        int nov = _level - 1;
                        try {
                            temp.crawl(n1, nov);
                        }
                        catch (...) {
                        }
                    } else {
                        try {
                            struct link n1(std::string(href->value));
                            download_page(n1);
                        }
                        catch (...) {
                        }
                    }
                    fflush(stdout);
                    std::cout << href->value << " " << _level << std::endl;
                }
            }
        }

        GumboVector *children = &node->v.element.children;
        for (unsigned int i = 0; i < children->length; ++i) {
            search_for_links
            (static_cast<GumboNode *>(children->data[i]),
                    _level);
        }
    }

    void search_for_links(GumboNode *node) {
        if (node->type != GUMBO_NODE_ELEMENT) {
            return;
        }
        GumboAttribute *href;
        if (node->v.element.tag == GUMBO_TAG_IMG &&
            (href = gumbo_get_attribute(&node->v.element.attributes, "src"))) {
            std::string res(href->value);
            pics_mutex.lock();
            pics_queue.push(href->value);
            pics_mutex.unlock();
            //std::cout<<href->value<<std::endl;
        }
        GumboVector *children = &node->v.element.children;
        for (unsigned int i = 0; i < children->length; ++i) {
            search_for_links(static_cast<GumboNode *>(children->data[i]));
        }
    }


    std::string download_page(struct link url) {
        boost::asio::io_context ioc;
        ssl::context ctx(ssl::context::tlsv12_client);
        // These objects perform our I/O
        tcp::resolver resolver(ioc);
        boost::beast::ssl_stream <boost::beast::tcp_stream> stream(ioc, ctx);
        // Look up the domain name
        auto const results = resolver.resolve(url._host, url._port);

        // Make the connection on the IP address we get from a lookup
        beast::get_lowest_layer(stream).connect(results);

        // Perform the SSL handshake
        stream.handshake(ssl::stream_base::client);

        // Set up an HTTP GET request message
        http::request <http::string_body> req{http::verb::get,
                                              url._target, url._version};
        req.set(http::field::host, url._host);
        req.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);

        // Send the HTTP request to the remote host
        http::write(stream, req);

        // This buffer is used for reading and must be persisted
        beast::flat_buffer buffer;

        // Declare a container to hold the response
        http::response <http::dynamic_body> res;
        std::cout << "waiting for response\n";
        fflush(stdout);
        // Receive the HTTP response
        http::read(stream, buffer, res);

        std::string result = boost::beast::buffers_to_string(res.body().data());
        m1.lock();
        pages.push(result);
        m1.unlock();
        beast::error_code ec;
        stream.shutdown(ec);
        return result;
    }

    void crawl(struct link url, int level) {
        GumboOutput *output = gumbo_parse(download_page(url).c_str());
        search_for_links(output->root, level);
        gumbo_destroy_output(&kGumboDefaultOptions, output);
    }

    void find_pics() {
        std::string tmp;
        bool status = false;
        while (!status) {
            queue_mutex.lock();
            if (!pages.empty()) {
                tmp = pages.front();
                pages.pop();
                queue_mutex.unlock();
                GumboOutput *output = gumbo_parse(tmp.c_str());
                search_for_links(output->root);
                gumbo_destroy_output(&kGumboDefaultOptions, output);
            } else {
                queue_mutex.unlock();
                status = true;
            }
        }
    }

    void myfunc() {
        std::vector <std::thread> threads;
        threads.reserve(5);
        for (int i = 0; i < 5; ++i) {
            threads.emplace_back(std::thread(&downloader::find_pics, this));
        }
        for (auto &thread : threads) {
            thread.join();
        }
    }
};
static std::vector <std::string> str;
int main() {
    struct link first("https://horo.mail.ru/");
    struct link second("https://codbit.wordpress.com/");
    downloader d1;
    std::thread t1(&downloader::crawl, d1, first, 1);
    std::thread t2(&downloader::crawl, d1, second, 1);
    t2.join();
    t1.join();
    std::cout << pages.size() << std::endl;
    std::cout << "Now links on pics" << std::endl;
    d1.myfunc();
    while (!pics_queue.empty()) {
        std::cout << pics_queue.front() << std::endl;
        pics_queue.pop();
    }
    return 0;
}
