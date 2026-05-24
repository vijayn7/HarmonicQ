#include <pistache/endpoint.h>
#include <pistache/router.h>
#include <pistache/http.h>
#include <pistache/http_headers.h>

#include <nlohmann/json.hpp>
#include <essentia/essentia.h>

#include <iostream>
#include <string>
#include "embeddingBuilder.h"
#include <unistd.h>

using namespace Pistache;

class Server {
private:
    Rest::Router router;
    std::shared_ptr<Http::Endpoint> endpoint;
    Port listenPort;

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
        std::cout << "Server initialized on port " << static_cast<int>(listenPort) << " with " << threads << " threads." << std::endl;
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
        nlohmann::json j = {{"status", "ok"}};
        response.headers().add<Http::Header::ContentType>(MIME(Application, Json));
        response.send(Http::Code::Ok, j.dump());
    }

    void analyze(const Rest::Request& request, Http::ResponseWriter response) {
        std::cout << "Analyze endpoint hit." << std::endl;

        const pid_t pid = getpid();
        std::cerr << "[Server] PID=" << pid << " Analyze called; content-length=" << request.body().size() << std::endl;

        try {
            // Essentia algorithm instances are not thread-safe; keep one builder per worker thread.
            thread_local EmbeddingBuilder embedder;
            const std::string body = request.body();
            FeatureResult fr = embedder.build(body);

            nlohmann::json jsonResponse;
            jsonResponse["embedding"] = fr.faissVector;
            jsonResponse["bpm"] = fr.bpm;
            jsonResponse["key"] = fr.key;
            jsonResponse["mode"] = fr.mode;
            jsonResponse["camelot"] = fr.camelot;
            jsonResponse["energy"] = fr.energy;
            jsonResponse["centroid"] = fr.centroid;
            jsonResponse["duration"] = fr.duration;
            response.headers().add<Http::Header::ContentType>(MIME(Application, Json));
            response.send(Http::Code::Ok, jsonResponse.dump());
            std::cerr << "[Server] PID=" << pid << " Analyze succeeded; bpm=" << fr.bpm << " key=" << fr.key << std::endl;
            return;
        } catch (const std::exception& ex) {
            std::cerr << "[Server] PID=" << pid << " Analyze failed: " << ex.what() << std::endl;
            nlohmann::json err = {{"error", ex.what()}};
            response.headers().add<Http::Header::ContentType>(MIME(Application, Json));
            response.send(Http::Code::Bad_Request, err.dump());
            return;
        } catch (...) {
            std::cerr << "[Server] PID=" << pid << " Analyze failed with unknown exception" << std::endl;
            nlohmann::json err = {{"error", "Unknown error"}};
            response.headers().add<Http::Header::ContentType>(MIME(Application, Json));
            response.send(Http::Code::Internal_Server_Error, err.dump());
            return;
        }
    }

    void recommend(const Rest::Request& request, Http::ResponseWriter response) {
        (void)request;
        std::cout << "Recommend endpoint hit." << std::endl;
        nlohmann::json j = {{"message", "Recommend endpoint hit"}};
        response.headers().add<Http::Header::ContentType>(MIME(Application, Json));
        response.send(Http::Code::Ok, j.dump());
    }

    void ingest(const Rest::Request& request, Http::ResponseWriter response) {
        (void)request;
        std::cout << "Ingest endpoint hit." << std::endl;
        nlohmann::json j = {{"message", "Ingest endpoint hit"}};
        response.headers().add<Http::Header::ContentType>(MIME(Application, Json));
        response.send(Http::Code::Ok, j.dump());
    }

};

int main() {
    essentia::init();

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
        essentia::shutdown();
        return 1;
    }

    essentia::shutdown();
    return 0;
}