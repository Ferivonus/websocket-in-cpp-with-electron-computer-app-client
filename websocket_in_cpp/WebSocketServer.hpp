#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif

#ifndef WEBSOCKETSERVER_HPP
#define WEBSOCKETSERVER_HPP

#pragma once
#include <boost/beast.hpp>
#include <boost/asio.hpp>
#include <sqlite3.h>
#include <set>
#include <unordered_map>
#include <mutex>
#include <nlohmann/json.hpp>
#include <memory>
#include <sstream>
#include <iomanip>
#include <iostream>

namespace net = boost::asio;
namespace beast = boost::beast;
namespace websocket = beast::websocket;
using tcp = net::ip::tcp;

class WebSocketServer {
public:
    WebSocketServer(const std::string& port);
    ~WebSocketServer();

    void run();

private:
    std::string port;
    sqlite3* db;
    bool running;

    std::set<std::shared_ptr<websocket::stream<tcp::socket>>> clients;
    std::mutex client_mutex;

    std::unordered_map<std::string, std::shared_ptr<websocket::stream<tcp::socket>>> connected_users;
    std::mutex users_mutex;

    void session(tcp::socket socket);
    void broadcast(std::shared_ptr<websocket::stream<tcp::socket>> sender, const std::string& message);
    void remove_client(std::shared_ptr<websocket::stream<tcp::socket>> ws);
    bool validate_token(const std::string& username, const std::string& token);
    std::shared_ptr<websocket::stream<tcp::socket>> find_user_ws(const std::string& username);
};
#endif // WEBSOCKETSERVER_HPP