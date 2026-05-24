#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <filesystem>

#include <essentia/algorithmfactory.h>
#include <essentia/essentia.h>

using essentia::standard::AlgorithmFactory;
using essentia::Real;

// Public result returned by the embedding builder. Lightweight POD-like
// structure so callers (server) can read fields directly.
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
	std::vector<float> chroma;         // 12 values
	std::vector<float> mfcc;           // 13 values
	std::vector<float> faissVector;    // 28 normalized values
};

class EmbeddingBuilder {
public:
	EmbeddingBuilder();
	~EmbeddingBuilder();

	EmbeddingBuilder(const EmbeddingBuilder&)            = delete;
	EmbeddingBuilder& operator=(const EmbeddingBuilder&) = delete;

	// Build FeatureResult from raw audio bytes (WAV, PCM, etc.). Throws
	// std::runtime_error on failure.
	FeatureResult build(const std::string& audioBytes);

private:
	AlgorithmFactory* factory_ = nullptr;

	static constexpr float kSampleRate  = 44100.0f;
	static constexpr int   kFrameSize   = 4096;
	static constexpr int   kHopSize     = 2048;
	static constexpr int   kSpectrumSize = kFrameSize / 2 + 1;

	struct FrameFeatures {
		std::vector<float> chroma;   // 12
		std::vector<float> mfcc;     // 13
		float energy   = 0.0f;
		float centroid = 0.0f;
	};

	// helpers
	static std::filesystem::path makeTempPath();
	static int keyNameToIndex(const std::string& key);
	static void appendVec(std::vector<float>& dst, const std::vector<float>& src);
	static std::vector<float> buildFaissVector(float bpm,
											   const std::vector<float>& chroma,
											   const std::vector<float>& mfcc,
											   float energy,
											   float centroid);

	// pipeline stages
	std::vector<Real> loadAudio(const std::string& path, float& durationOut);
	void extractRhythm(const std::vector<Real>& audio, float& bpm, std::vector<float>& beats);
	void extractKey(const std::vector<Real>& audio, std::string& key, std::string& scale, float& strength);
	FrameFeatures extractFrameFeatures(const std::vector<Real>& audio);
	FeatureResult runPipeline(const std::string& path);

	// compute wrapper that annotates algorithm failures
	void safeCompute(essentia::standard::Algorithm* algo, const std::string& name, int frameIndex = -1);
};

