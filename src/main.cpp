#include <iostream>
#include <string>
#include "RestServer.h"

static void usage(){
    std::cout << "FinWorkOptimizer CLI\n"
                 "Usage:\n"
                 "  FinWorkOptimizer --server [port]     Start REST API server (default 8080)\n"
                 "  FinWorkOptimizer --help              Show this help\n";
}

int main(int argc, char* argv[]){
    if (argc >= 2 && std::string(argv[1]) == "--server"){
        int port = 8080;
        if (argc >= 3){
            try { port = std::stoi(argv[2]); } catch(...) {}
        }
        std::cout << "Launching REST server on port " << port << " (env FWO_API_KEY for auth)\n";
        return run_rest_server(port);
    }
    usage();
    return 0;
}
