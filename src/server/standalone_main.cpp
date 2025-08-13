#include <iostream>
#include "RestServer.h"
int main(int argc, char** argv){
    int port = 8080;
    if (argc > 1) { try { port = std::stoi(argv[1]); } catch(...) {} }
    std::cout << "Starting fwo_rest_server on port " << port << " ...\n";
    return run_rest_server(port);
}
