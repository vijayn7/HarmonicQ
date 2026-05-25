#include "database.h"

#include <filesystem>
#include <stdexcept>

namespace {
std::runtime_error makeSqliteError(sqlite3* db, const std::string& prefix) {
    return std::runtime_error(prefix + ": " + (db ? sqlite3_errmsg(db) : "sqlite error"));
}
}

Database::Database(const std::string& dbPath) {
    if (sqlite3_open_v2(
            dbPath.c_str(),
            &db_,
            SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX,
            nullptr) != SQLITE_OK) {
        throw makeSqliteError(db_, "failed to open sqlite database");
    }

    executeOrThrow("PRAGMA journal_mode=WAL;");
    executeOrThrow("PRAGMA synchronous=NORMAL;");

    executeOrThrow(
        "CREATE TABLE IF NOT EXISTS songs ("
        " song_id   INTEGER PRIMARY KEY AUTOINCREMENT,"
        " file_path TEXT NOT NULL UNIQUE,"
        " title     TEXT,"
        " artist    TEXT,"
        " bpm       REAL,"
        " key       TEXT,"
        " mode      TEXT,"
        " camelot   TEXT,"
        " energy    REAL,"
        " centroid  REAL,"
        " duration  REAL,"
        " faiss_id  INTEGER NOT NULL"
        ");"
    );

    executeOrThrow("CREATE INDEX IF NOT EXISTS idx_songs_faiss_id ON songs(faiss_id);");
}

Database::~Database() {
    if (db_) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
}

void Database::executeOrThrow(const std::string& sql) const {
    char* err = nullptr;
    if (sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &err) != SQLITE_OK) {
        std::string msg = err ? err : "sqlite exec failed";
        sqlite3_free(err);
        throw std::runtime_error(msg);
    }
}

bool Database::songExists(const std::string& filePath) const {
    const char* sql = "SELECT 1 FROM songs WHERE file_path = ? LIMIT 1;";
    sqlite3_stmt* stmt = nullptr;

    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        throw makeSqliteError(db_, "failed to prepare songExists");
    }

    sqlite3_bind_text(stmt, 1, filePath.c_str(), -1, SQLITE_TRANSIENT);
    const int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    return rc == SQLITE_ROW;
}

int Database::insertSong(const std::string& filePath, const FeatureResult& features, int faissId) {
    const char* sql =
        "INSERT INTO songs(file_path, title, artist, bpm, key, mode, camelot, energy, centroid, duration, faiss_id) "
        "VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        throw makeSqliteError(db_, "failed to prepare insertSong");
    }

    const std::string title = std::filesystem::path(filePath).stem().string();
    const std::string artist = "";

    sqlite3_bind_text(stmt, 1, filePath.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, title.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, artist.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt, 4, features.bpm);
    sqlite3_bind_text(stmt, 5, features.key.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 6, features.mode.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 7, features.camelot.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt, 8, features.energy);
    sqlite3_bind_double(stmt, 9, features.centroid);
    sqlite3_bind_double(stmt, 10, features.duration);
    sqlite3_bind_int(stmt, 11, faissId);

    if (sqlite3_step(stmt) != SQLITE_DONE) {
        sqlite3_finalize(stmt);
        throw makeSqliteError(db_, "failed to insert song row");
    }

    sqlite3_finalize(stmt);
    return static_cast<int>(sqlite3_last_insert_rowid(db_));
}

SongRow Database::getSong(int songId) const {
    const char* sql =
        "SELECT song_id, file_path, title, artist, bpm, key, mode, camelot, energy, centroid, duration, faiss_id "
        "FROM songs WHERE song_id = ? LIMIT 1;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        throw makeSqliteError(db_, "failed to prepare getSong");
    }

    sqlite3_bind_int(stmt, 1, songId);
    const int rc = sqlite3_step(stmt);
    if (rc != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        throw std::runtime_error("song not found for song_id=" + std::to_string(songId));
    }

    SongRow row;
    row.songId = sqlite3_column_int(stmt, 0);
    row.filePath = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
    row.title = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
    row.artist = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
    row.bpm = static_cast<float>(sqlite3_column_double(stmt, 4));
    row.key = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
    row.mode = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));
    row.camelot = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7));
    row.energy = static_cast<float>(sqlite3_column_double(stmt, 8));
    row.centroid = static_cast<float>(sqlite3_column_double(stmt, 9));
    row.duration = static_cast<float>(sqlite3_column_double(stmt, 10));
    row.faissId = sqlite3_column_int(stmt, 11);

    sqlite3_finalize(stmt);
    return row;
}

std::unordered_map<int, int> Database::buildFaissIdMap() const {
    std::unordered_map<int, int> out;
    const char* sql = "SELECT faiss_id, song_id FROM songs ORDER BY faiss_id ASC;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        throw makeSqliteError(db_, "failed to prepare buildFaissIdMap");
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const int faissId = sqlite3_column_int(stmt, 0);
        const int songId = sqlite3_column_int(stmt, 1);
        out[faissId] = songId;
    }

    sqlite3_finalize(stmt);
    return out;
}
