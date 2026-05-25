#pragma once

#include <string>
#include <vector>

// Public result returned by the embedding builder and persisted in SQLite.
struct FeatureResult {
    float              bpm        = 0.0f;
    std::string        key;
    std::string        mode;
    std::string        camelot;
    int                keyIndex   = 0;
    float              strength   = 0.0f;
    float              energy     = 0.0f;
    float              centroid   = 0.0f;
    float              duration   = 0.0f;
    std::vector<float> chroma;      // 12 values
    std::vector<float> mfcc;        // 13 values
    std::vector<float> faissVector; // 28 normalized values
};
