#include <iostream>
#include <string>
#include <pistache/endpoint.h>
#include <nlohmann/json.hpp>
#include <essentia/essentia.h>



void print_help() {
    std::cout << "Usage: analyzer [options]\n"
              << "Options:\n"
              << "  -h, --help            Display this help message\n"
              << "  -p, --port <number>   Specify port number (required)\n"
              << "  -db, --database <path> Specify database path (required)\n";
}

void process_arguments(const std::string& port, const std::string& database, int argc, char* argv[]) {
    // Placeholder for processing the arguments
    std::cout << "Port: " << port << "\n";
    std::cout << "Database: " << database << "\n";
}

int main(int argc, char* argv[]) {
    // three flags:
    // -h or --help: display help message 
    // -p or --port: specify port number REQUIRED
    // -db or --database: specify database path REQUIRED

    if (argc == 1) {
        print_help();
        return 0;
    }

    std::string port;
    std::string database;

    process_arguments(port, database, argc, argv);

    return 0;

}