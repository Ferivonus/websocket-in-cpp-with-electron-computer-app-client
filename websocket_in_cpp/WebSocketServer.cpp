#include "WebSocketServer.hpp"
#include <iostream>
#include <thread>
#include <mutex>
#include <string>
#include <sstream>
#include <random>
#include <chrono>
#include <sqlite3.h>
#include <nlohmann/json.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/asio.hpp>
#include <boost/optional.hpp>

using namespace std;
using namespace beast;
using namespace net;
using namespace websocket;

// Token üretme fonksiyonu
static string generate_token() {
    random_device rd;
    mt19937 gen(rd());
    uniform_int_distribution<> distrib(0, 15);
    string token;
    for (int i = 0; i < 32; ++i) {
        token += "0123456789ABCDEF"[distrib(gen)];
    }
    return token;
}

WebSocketServer::WebSocketServer(const string& port) : port(port), db(nullptr), running(true) {
    // Veritabaný optimizasyonlarý
    int rc = sqlite3_open("users.db", &db);
    if (rc != SQLITE_OK) {
        cerr << "Database error: " << sqlite3_errmsg(db) << endl;
        sqlite3_close(db);
        db = nullptr;
        return;
    }

    // SQLite performans ayarlarý
    sqlite3_exec(db, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);
    sqlite3_exec(db, "PRAGMA synchronous=NORMAL;", nullptr, nullptr, nullptr);
    sqlite3_exec(db, "PRAGMA cache_size=-10000;", nullptr, nullptr, nullptr);

    // Tablo ve indeksleri oluþtur
    const char* sql =
        "CREATE TABLE IF NOT EXISTS users ("
        "username TEXT PRIMARY KEY,"
        "password TEXT NOT NULL,"
        "token TEXT,"
        "token_expiry DATETIME);"
        "CREATE INDEX IF NOT EXISTS idx_token ON users(token);"
        "CREATE INDEX IF NOT EXISTS idx_username ON users(username);";

    char* errMsg = nullptr;
    rc = sqlite3_exec(db, sql, nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
        cerr << "SQL error: " << errMsg << endl;
        sqlite3_free(errMsg);
    }
}

WebSocketServer::~WebSocketServer() {
    if (db) {
        sqlite3_close(db);
    }
    running = false;
}

bool WebSocketServer::validate_token(const string& username, const string& token) {
    sqlite3_stmt* stmt;
    const char* sql = "SELECT token FROM users WHERE username = ? AND token = ? "
        "AND token_expiry > datetime('now') LIMIT 1;";

    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return false;

    sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, token.c_str(), -1, SQLITE_STATIC);

    bool valid = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        valid = true;
    }

    sqlite3_finalize(stmt);
    return valid;
}

void WebSocketServer::broadcast(shared_ptr<stream<tcp::socket>> sender_ws, const string& message) {
    lock_guard<mutex> lock(client_mutex);
    auto it = clients.begin();
    while (it != clients.end()) {
        auto& ws = *it;
        if (ws == sender_ws || !ws->is_open()) {
            ++it;
            continue;
        }
        try {
            ws->write(net::buffer(message));
            ++it;
        }
        catch (...) {
            it = clients.erase(it);
        }
    }
}

void WebSocketServer::remove_client(shared_ptr<stream<tcp::socket>> ws) {
    {
        lock_guard<mutex> lock(client_mutex);
        clients.erase(ws);
    }

    {
        lock_guard<mutex> lock(users_mutex);
        for (auto it = connected_users.begin(); it != connected_users.end();) {
            if (it->second == ws) {
                it = connected_users.erase(it);
            }
            else {
                ++it;
            }
        }
    }
}

shared_ptr<websocket::stream<tcp::socket>> WebSocketServer::find_user_ws(const string& username) {
    lock_guard<mutex> lock(users_mutex);
    auto it = connected_users.find(username);
    return (it != connected_users.end()) ? it->second : nullptr;
}

void WebSocketServer::session(tcp::socket socket) {
    auto ws = make_shared<stream<tcp::socket>>(move(socket));
    try {
        // Accept WebSocket connection
        ws->accept();
        {
            lock_guard<mutex> lock(client_mutex);
            clients.insert(ws);
        }

        flat_buffer buffer;
        while (true) {
            buffer.clear();
            ws->read(buffer);
            string msg = buffers_to_string(buffer.data());

            try {
                // Parse the received message into JSON
                auto json = nlohmann::json::parse(msg, nullptr, false);

                if (json.is_discarded()) {
                    throw runtime_error("Invalid JSON format");
                }

                // Ensure that the message contains the expected 'type'
                if (!json.contains("type") || !json["type"].is_string()) {
                    //throw runtime_error("Missing or invalid 'type' in message");
                }

                string type = json["type"];
                cout << "Received message type: " << type << endl; // Debug log
                cout << "Received JSON data: " << json.dump(6) << endl; // Pretty print with indent level of 4


                string username = json["username"];
                string password = json.value("password", "");  // Default to empty if password is not provided

                if (type == "register") {
                    // Register new user
                    cout << "Handling registration for user: " << username << endl;

                    sqlite3_stmt* stmt;
                    const char* sql = "INSERT INTO users (username, password) VALUES (?, ?);";
                    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
                    sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_STATIC);
                    sqlite3_bind_text(stmt, 2, password.c_str(), -1, SQLITE_STATIC);

                    nlohmann::json res;
                    if (sqlite3_step(stmt) == SQLITE_DONE) {
                        res["status"] = "success";
                        res["responsefrom"] = "register";
                    }
                    else {
                        res["status"] = "error";
                        res["responsefrom"] = "register";
                        res["message"] = sqlite3_errmsg(db);
                    }
                    sqlite3_finalize(stmt);
                    ws->write(net::buffer(res.dump()));
                }
                else if (type == "login") {
                    // Login existing user
                    cout << "Handling login for user: " << username << endl;

                    sqlite3_stmt* stmt;
                    const char* check_sql = "SELECT COUNT(*) FROM users WHERE username = ? AND password = ?;";

                    int rc = sqlite3_prepare_v2(db, check_sql, -1, &stmt, nullptr);
                    sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_STATIC);
                    sqlite3_bind_text(stmt, 2, password.c_str(), -1, SQLITE_STATIC);

                    int user_exists = 0;
                    if (sqlite3_step(stmt) == SQLITE_ROW) {
                        user_exists = sqlite3_column_int(stmt, 0);
                    }
                    sqlite3_finalize(stmt);

                    nlohmann::json res;

                    if (user_exists > 0) {
                        // User exists, generate token and assign it
                        string token = generate_token();
                        const char* update_sql = "UPDATE users SET token = ?, token_expiry = datetime('now','+1 hour') WHERE username = ?;";

                        rc = sqlite3_prepare_v2(db, update_sql, -1, &stmt, nullptr);
                        sqlite3_bind_text(stmt, 1, token.c_str(), -1, SQLITE_STATIC);
                        sqlite3_bind_text(stmt, 2, username.c_str(), -1, SQLITE_STATIC);

                        if (sqlite3_step(stmt) == SQLITE_DONE) {
                            res["responsefrom"] = "login";
                            res["status"] = "success";
                            res["token"] = token;
                            cout << "Login successful: Token assigned to " << username << " -> " << token << endl;

                            lock_guard<mutex> lock(users_mutex);
                            connected_users[username] = ws;
                        }
                        else {
                            res["status"] = "error";
                            res["responsefrom"] = "login";
                            res["message"] = sqlite3_errmsg(db);
                        }
                        sqlite3_finalize(stmt);
                    }
                    else {
                        res["status"] = "error";
                        res["responsefrom"] = "login";
                        res["message"] = "Invalid credentials";
                    }

                    cout << "I am sending that message to client: " << type << endl; // Debug log
                    cout << "Sended JSON data: " << res.dump(4) << endl; // Pretty print with indent level of 4


                    ws->write(net::buffer(res.dump()));
                }
                else if (type == "message") {

                    string username = json["username"];
                    string token = json["token"];
                    string content = json["content"];

                    if (username.empty() || token.empty() || !validate_token(username, token)) {
                        nlohmann::json res;
                        res["status"] = "error";
                        res["error_type"] = "Authentication failed";
                        res["responsefrom"] = "message";
                        res["userMessage"] = content;
                        ws->write(net::buffer(res.dump()));
                        continue;
                    }
                    else {
                        // Process message from client

                        nlohmann::json broadcast_msg;
                        broadcast_msg["type"] = "message";
                        broadcast_msg["responsefrom"] = "message";
                        broadcast_msg["userMessage"] = username + ": " + content;

                        // Broadcast the JSON message to all clients except sender
                        broadcast(ws, broadcast_msg.dump());

                        // Send success response to sender with own message
                        nlohmann::json res;
                        res["status"] = "success";
                        res["responsefrom"] = "message";
                        res["userMessage"] = "You (Me):" + content;  // Mark as sender's message
                        ws->write(net::buffer(res.dump()));
                    }

                }
            }
            catch (const exception& e) {

                nlohmann::json json;  // Declare json here to make it accessible

                cerr << "Error processing message: " << e.what() << endl;

                // Log the incoming JSON message to help debug
                cerr << "Received JSON data: " << json.dump(4) << endl; // Pretty print with indent level of 4

                nlohmann::json res;
                res["status"] = "error";
                res["message"] = "Invalid message format";
                ws->write(net::buffer(res.dump()));
            }
        }
    }
    catch (...) {
        cerr << "WebSocket session error occurred" << endl;
        remove_client(ws);
    }
}


void WebSocketServer::run() {
    try {
        io_context ioc;
        tcp::acceptor acceptor(ioc, tcp::endpoint(tcp::v4(), stoi(port)));
        cout << "WebSocket server listening on port " << port << endl;

        while (running) {
            tcp::socket socket(ioc);
            acceptor.accept(socket);
            thread([this, sock = move(socket)]() mutable {
                session(move(sock));
                }).detach();
        }
    }
    catch (const exception& e) {
        cerr << "Server error: " << e.what() << endl;
    }
}