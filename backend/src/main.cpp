#include <pistache/endpoint.h>
#include <pistache/router.h>
#include <pistache/http.h>
#include <pistache/http_headers.h>

#include <nlohmann/json.hpp>
#include <essentia/essentia.h>
#include <faiss/IndexFlat.h>
#include <faiss/index_io.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>
#include "embeddingBuilder.h"
#include "database.h"
#include <unistd.h>

using namespace Pistache;

class Server {
private:
    Rest::Router router;
    std::shared_ptr<Http::Endpoint> endpoint;
    Port listenPort;

    std::unique_ptr<faiss::Index> index_;
    std::mutex indexMutex_;
    std::unordered_map<int, int> faissIdToSongId_;
    Database db_;
    std::string indexPath_;

    static void sendJson(Http::ResponseWriter& response, Http::Code code, const nlohmann::json& body) {
        response.headers().add<Http::Header::ContentType>(MIME(Application, Json));
        response.send(code, body.dump());
    }

    static std::string toLower(std::string s) {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        return s;
    }

    static float computeCamelotScore(const std::string& query, const std::string& candidate) {
        if (query == candidate) return 1.0f;

        auto parse = [](const std::string& code, int& num, char& letter) -> bool {
            if (code.size() < 2) return false;
            try {
                num = std::stoi(code.substr(0, code.size() - 1));
            } catch (...) {
                return false;
            }
            letter = code.back();
            return true;
        };

        int qNum = 0, cNum = 0;
        char qLet = '\0', cLet = '\0';
        if (!parse(query, qNum, qLet) || !parse(candidate, cNum, cLet)) return 0.0f;

        const int diff = std::abs(qNum - cNum);
        const bool adjacent = (qLet == cLet) && (diff == 1 || diff == 11);
        if (adjacent) return 0.7f;

        const bool relative = (qNum == cNum) && (qLet != cLet);
        if (relative) return 0.6f;

        return 0.0f;
    }

public:
    Server(Address addr, const std::string& dbPath, const std::string& indexPath)
        : listenPort(addr.port())
        , db_(dbPath)
        , indexPath_(indexPath)
    {
        endpoint = std::make_shared<Http::Endpoint>(addr);

        if (std::filesystem::exists(indexPath_)) {
            index_.reset(faiss::read_index(indexPath_.c_str()));
            if (index_->d != 28) {
                throw std::runtime_error("loaded FAISS index dimension mismatch; expected 28");
            }
        } else {
            index_ = std::make_unique<faiss::IndexFlatL2>(28);
        }

        faissIdToSongId_ = db_.buildFaissIdMap();
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
        sendJson(response, Http::Code::Ok, j);
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
            sendJson(response, Http::Code::Ok, jsonResponse);
            std::cerr << "[Server] PID=" << pid << " Analyze succeeded; bpm=" << fr.bpm << " key=" << fr.key << std::endl;
            return;
        } catch (const std::exception& ex) {
            std::cerr << "[Server] PID=" << pid << " Analyze failed: " << ex.what() << std::endl;
            nlohmann::json err = {{"error", ex.what()}};
            sendJson(response, Http::Code::Bad_Request, err);
            return;
        } catch (...) {
            std::cerr << "[Server] PID=" << pid << " Analyze failed with unknown exception" << std::endl;
            nlohmann::json err = {{"error", "Unknown error"}};
            sendJson(response, Http::Code::Internal_Server_Error, err);
            return;
        }
    }

    void recommend(const Rest::Request& request, Http::ResponseWriter response) {
        std::cout << "Recommend endpoint hit." << std::endl;

        nlohmann::json body;
        try {
            body = nlohmann::json::parse(request.body());
        } catch (...) {
            sendJson(response, Http::Code::Bad_Request, { {"error", "invalid JSON body"} });
            return;
        }

        if (!body.contains("embedding") || !body.contains("camelot") || !body.contains("bpm")) {
            sendJson(response, Http::Code::Bad_Request, { {"error", "missing required fields: embedding, camelot, bpm"} });
            return;
        }

        std::vector<float> queryVec;
        try {
            queryVec = body.at("embedding").get<std::vector<float>>();
        } catch (...) {
            sendJson(response, Http::Code::Bad_Request, { {"error", "embedding must be an array of floats"} });
            return;
        }

        if (queryVec.size() != 28) {
            sendJson(response, Http::Code::Bad_Request, { {"error", "embedding must have 28 dimensions"} });
            return;
        }

        const std::string queryCamelot = body.at("camelot").get<std::string>();
        const float queryBpm = body.at("bpm").get<float>();
        const int topK = body.value("top_k", 20);

        std::vector<faiss::idx_t> labels;
        std::vector<float> dists;
        std::unordered_map<int, int> localMap;
        int searchK = 0;

        {
            std::lock_guard<std::mutex> lk(indexMutex_);
            if (!index_ || index_->ntotal == 0) {
                sendJson(response, Http::Code::Bad_Request, { {"error", "index is empty - run /ingest first"} });
                return;
            }

            searchK = std::min(50, static_cast<int>(index_->ntotal));
            localMap = faissIdToSongId_;
        }

        labels.assign(searchK, -1);
        dists.assign(searchK, 0.0f);
        index_->search(1, queryVec.data(), searchK, dists.data(), labels.data());

        struct Candidate {
            int songId = 0;
            std::string title;
            std::string artist;
            std::string filePath;
            float bpm = 0.0f;
            float bpmDelta = 0.0f;
            std::string camelot;
            bool camelotCompatible = false;
            float energy = 0.0f;
            float score = 0.0f;
        };

        float maxDist = 0.0f;
        for (float d : dists) maxDist = std::max(maxDist, d);

        std::vector<Candidate> candidates;
        candidates.reserve(labels.size());

        for (size_t i = 0; i < labels.size(); ++i) {
            const faiss::idx_t label = labels[i];
            if (label < 0) continue;

            const auto it = localMap.find(static_cast<int>(label));
            if (it == localMap.end()) continue;

            SongRow row;
            try {
                row = db_.getSong(it->second);
            } catch (...) {
                continue;
            }

            const float camelotScore = computeCamelotScore(queryCamelot, row.camelot);
            const float bpmDelta = row.bpm - queryBpm;
            const float bpmScore = std::max(0.0f, 1.0f - std::abs(bpmDelta) / 20.0f);
            const float distScore = (maxDist > 0.0f) ? (1.0f - dists[i] / maxDist) : 1.0f;
            const float combined = 0.5f * camelotScore + 0.3f * bpmScore + 0.2f * distScore;

            candidates.push_back(Candidate{
                row.songId,
                row.title,
                row.artist,
                row.filePath,
                row.bpm,
                bpmDelta,
                row.camelot,
                camelotScore >= 0.6f,
                row.energy,
                combined
            });
        }

        std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
            return a.score > b.score;
        });

        if (static_cast<int>(candidates.size()) > topK) {
            candidates.resize(topK);
        }

        nlohmann::json result;
        result["query"] = {{"bpm", queryBpm}, {"camelot", queryCamelot}};
        result["candidates"] = nlohmann::json::array();
        for (const auto& c : candidates) {
            result["candidates"].push_back({
                {"song_id", c.songId},
                {"title", c.title},
                {"artist", c.artist},
                {"file_path", c.filePath},
                {"bpm", c.bpm},
                {"bpm_delta", c.bpmDelta},
                {"camelot", c.camelot},
                {"camelot_compatible", c.camelotCompatible},
                {"energy", c.energy},
                {"score", c.score}
            });
        }

        sendJson(response, Http::Code::Ok, result);
    }

    void ingest(const Rest::Request& request, Http::ResponseWriter response) {
        std::cout << "Ingest endpoint hit." << std::endl;

        nlohmann::json body;
        try {
            body = nlohmann::json::parse(request.body());
        } catch (...) {
            sendJson(response, Http::Code::Bad_Request, { {"error", "invalid JSON body"} });
            return;
        }

        if (!body.contains("directory")) {
            sendJson(response, Http::Code::Bad_Request, { {"error", "missing field: directory"} });
            return;
        }

        const std::string dir = body.at("directory").get<std::string>();
        if (!std::filesystem::exists(dir)) {
            sendJson(response, Http::Code::Bad_Request, { {"error", "directory does not exist: " + dir} });
            return;
        }

        int added = 0;
        int skipped = 0;
        int errors = 0;

        for (const auto& entry : std::filesystem::recursive_directory_iterator(dir)) {
            if (!entry.is_regular_file()) continue;

            const std::string ext = toLower(entry.path().extension().string());
            if (ext != ".mp3" && ext != ".wav") continue;

            const std::string filePath = entry.path().string();
            try {
                if (db_.songExists(filePath)) {
                    ++skipped;
                    continue;
                }

                thread_local EmbeddingBuilder embedder;
                FeatureResult fr = embedder.buildFromPath(filePath);

                std::lock_guard<std::mutex> lk(indexMutex_);
                const int faissId = static_cast<int>(index_->ntotal);
                index_->add(1, fr.faissVector.data());
                const int songId = db_.insertSong(filePath, fr, faissId);
                faissIdToSongId_[faissId] = songId;
                ++added;
            } catch (const std::exception& ex) {
                std::cerr << "[ingest] failed on " << filePath << ": " << ex.what() << std::endl;
                ++errors;
            }
        }

        int finalTotal = 0;
        {
            std::lock_guard<std::mutex> lk(indexMutex_);
            faiss::write_index(index_.get(), indexPath_.c_str());
            finalTotal = static_cast<int>(index_->ntotal);
        }

        nlohmann::json out = {
            {"added", added},
            {"skipped", skipped},
            {"errors", errors},
            {"total", finalTotal}
        };
        sendJson(response, Http::Code::Ok, out);
    }

};

int main() {
    essentia::init();

    Port port(9080);
    Address addr(Ipv4::any(), port);

    Server server(addr, "./hq.db", "./hq.index");
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