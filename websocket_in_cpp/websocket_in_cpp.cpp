#include "WebSocketServer.hpp"

int main(int argc, char* argv[]) {

    WebSocketServer server("8080");
    server.run();
    return 0;
}