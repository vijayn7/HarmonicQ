#pragma once

#include <sqlite3.h>

#include <string>
#include <unordered_map>

#include "featureResult.h"

struct SongRow {
    int songId = 0;
    std::string filePath;
    std::string title;
    std::string artist;
    float bpm = 0.0f;
    std::string key;
    std::string mode;
    std::string camelot;
    float energy = 0.0f;
    float centroid = 0.0f;
    float duration = 0.0f;
    int faissId = -1;
};

class Database {
public:
    explicit Database(const std::string& dbPath);
    ~Database();

    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    bool songExists(const std::string& filePath) const;
    int insertSong(const std::string& filePath, const FeatureResult& features, int faissId);
    SongRow getSong(int songId) const;
    std::unordered_map<int, int> buildFaissIdMap() const;

private:
    sqlite3* db_ = nullptr;

    void executeOrThrow(const std::string& sql) const;
};
