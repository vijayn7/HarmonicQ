#include <pistache/endpoint.h>
#include <pistache/router.h>
#include <pistache/http.h>

#include <nlohmann/json.hpp>

#include <iostream>
#include <string>
#include "embeddingBuilder.cpp"

using namespace Pistache;

class Server {
private:
    Rest::Router router;
    std::shared_ptr<Http::Endpoint> endpoint;
    Port listenPort;

    embeddingBuilder embedder;

public:
    Server(Address addr)
        : listenPort(addr.port())
    {
        endpoint = std::make_shared<Http::Endpoint>(addr);
    }

    void init(size_t threads = 2) {
        auto opts = Http::Endpoint::options()
                .threads(threads)
            .maxRequestSize(50 * 1024 * 1024)
                .flags(Tcp::Options::ReuseAddr);
        endpoint->init(opts);
        setupRoutes();
        endpoint->setHandler(router.handler());

        // Log server start
        std::cout << "Server initialized on port " << listenPort.toString() << " with " << threads << " threads." << std::endl;
    }

    void start() {
        std::cout << "Starting server..." << std::endl;
        endpoint->serve();
    }

    void setupRoutes() {
        Rest::Routes::Post(router, "/analyze", Rest::Routes::bind(&Server::analyze, this));
        Rest::Routes::Post(router, "/recommend", Rest::Routes::bind(&Server::recommend, this));
        Rest::Routes::Post(router, "/ingest", Rest::Routes::bind(&Server::ingest, this));
        Rest::Routes::Get(router, "/health", Rest::Routes::bind(&Server::health, this));
    }

    void health(const Rest::Request& request, Http::ResponseWriter response) {
        (void)request;
        std::cout << "Health check requested." << std::endl;
        response.send(Http::Code::Ok, "Server is running");
    }

    void analyze(const Rest::Request& request, Http::ResponseWriter response) {
        std::cout << "Analyze endpoint hit." << std::endl;

        float embedding[28];
        if (auto validationError = embedder.buildEmbedding(request, embedding)) {
            response.send(Http::Code::Bad_Request, validationError.value());
            return;
        }

        // send embedding back
        nlohmann::json jsonResponse;
        for (int i = 0; i < 28; ++i) {
            jsonResponse["embedding"][i] = embedding[i];
        }
        response.send(Http::Code::Ok, jsonResponse.dump());
    }

    void recommend(const Rest::Request& request, Http::ResponseWriter response) {
        (void)request;
        std::cout << "Recommend endpoint hit." << std::endl;
        // Placeholder for recommend logic
        response.send(Http::Code::Ok, "Recommend endpoint hit");
    }

    void ingest(const Rest::Request& request, Http::ResponseWriter response) {
        (void)request;
        std::cout << "Ingest endpoint hit." << std::endl;
        // Placeholder for ingest logic
        response.send(Http::Code::Ok, "Ingest endpoint hit");
    }

};

int main() {
    Port port(9080);
    Address addr(Ipv4::any(), port);

    Server server(addr);
    try {
        server.init();
        server.start();
        // Log server shutdown
        std::cout << "Server shutting down..." << std::endl;
    } catch (const std::exception& ex) {
        std::cerr << "Failed to start server: " << ex.what() << std::endl;
        return 1;
    }

    return 0;
}