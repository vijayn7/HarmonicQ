#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <random>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#include <iostream>
#include <unistd.h>

#include "embeddingBuilder.h"

namespace {
using Algorithm = essentia::standard::Algorithm;

struct AlgorithmDeleter {
    void operator()(Algorithm* algo) const noexcept {
        delete algo;
    }
};

using AlgoPtr = std::unique_ptr<Algorithm, AlgorithmDeleter>;

template <typename... Args>
AlgoPtr makeAlgo(AlgorithmFactory* factory, const std::string& name, Args&&... args) {
    return AlgoPtr(factory->create(name, std::forward<Args>(args)...));
}
}  // namespace

// Camelot map (kept in the implementation file)
static const std::unordered_map<int, std::unordered_map<std::string, std::string>> CAMELOT = {
    {0,  {{"major","8B"},  {"minor","5A"}}},
    {1,  {{"major","3B"},  {"minor","10A"}}},
    {2,  {{"major","10B"}, {"minor","7A"}}},
    {3,  {{"major","5B"},  {"minor","2A"}}},
    {4,  {{"major","12B"}, {"minor","9A"}}},
    {5,  {{"major","7B"},  {"minor","4A"}}},
    {6,  {{"major","2B"},  {"minor","11A"}}},
    {7,  {{"major","9B"},  {"minor","6A"}}},
    {8,  {{"major","4B"},  {"minor","1A"}}},
    {9,  {{"major","11B"}, {"minor","8A"}}},
    {10, {{"major","6B"},  {"minor","3A"}}},
    {11, {{"major","1B"},  {"minor","6A"}}},
};

EmbeddingBuilder::EmbeddingBuilder() {
    factory_ = &AlgorithmFactory::instance();
}

EmbeddingBuilder::~EmbeddingBuilder() {
}

FeatureResult EmbeddingBuilder::build(const std::string& audioBytes) {
    if (audioBytes.empty()) {
        throw std::runtime_error("audio data is empty");
    }

    const std::filesystem::path tempPath = makeTempPath();

    {
        std::ofstream f(tempPath, std::ios::binary);
        if (!f) {
            throw std::runtime_error("failed to create temp file: " + tempPath.string());
        }
        f.write(audioBytes.data(), static_cast<std::streamsize>(audioBytes.size()));
        if (!f) {
            std::filesystem::remove(tempPath);
            throw std::runtime_error("failed to write audio data to temp file");
        }
    }

    try {
        FeatureResult result = runPipeline(tempPath.string());
        std::filesystem::remove(tempPath);
        return result;
    } catch (...) {
        std::filesystem::remove(tempPath);
        throw;
    }
}

// Safe compute wrapper
void EmbeddingBuilder::safeCompute(essentia::standard::Algorithm* algo, const std::string& name, int frameIndex) {
    try {
        algo->compute();
    } catch (const std::exception& ex) {
        std::string msg = "Algorithm '" + name + "' failed";
        if (frameIndex >= 0) msg += " at frame " + std::to_string(frameIndex);
        msg += ": ";
        msg += ex.what();
        std::cerr << "[EmbeddingBuilder] " << msg << std::endl;
        throw std::runtime_error(msg);
    } catch (...) {
        std::string msg = "Algorithm '" + name + "' failed with unknown exception";
        if (frameIndex >= 0) msg += " at frame " + std::to_string(frameIndex);
        std::cerr << "[EmbeddingBuilder] " << msg << std::endl;
        throw std::runtime_error(msg);
    }
}

std::filesystem::path EmbeddingBuilder::makeTempPath() {
    thread_local std::mt19937_64 rng(std::random_device{}());
    thread_local std::uniform_int_distribution<uint64_t> dist;
    const std::string name =
        "hq_" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
        "_" +
        std::to_string(dist(rng)) +
        ".tmp";
    return std::filesystem::temp_directory_path() / name;
}

int EmbeddingBuilder::keyNameToIndex(const std::string& key) {
    static const std::unordered_map<std::string, int> kMap = {
        {"C",0}, {"C#",1}, {"D",2}, {"D#",3}, {"E",4},  {"F",5},
        {"F#",6},{"G",7},  {"G#",8},{"A",9},  {"A#",10},{"B",11},
        {"Db",1},{"Eb",3},{"Gb",6},{"Ab",8},{"Bb",10}
    };
    const auto it = kMap.find(key);
    if (it == kMap.end()) {
        throw std::runtime_error("unsupported key from KeyExtractor: " + key);
    }
    return it->second;
}

void EmbeddingBuilder::appendVec(std::vector<float>& dst, const std::vector<float>& src) {
    dst.insert(dst.end(), src.begin(), src.end());
}

std::vector<Real> EmbeddingBuilder::loadAudio(const std::string& path, float& durationOut) {
    AlgoPtr loader = makeAlgo(factory_, "MonoLoader",
        "filename",   path,
        "sampleRate", kSampleRate
    );

    std::vector<Real> audio;
    loader->output("audio").set(audio);
    safeCompute(loader.get(), "MonoLoader");

    if (audio.empty()) {
        throw std::runtime_error("MonoLoader returned no samples for: " + path);
    }

    durationOut = static_cast<float>(audio.size()) / kSampleRate;
    return audio;
}

void EmbeddingBuilder::extractRhythm(const std::vector<Real>& audio, float& bpm, std::vector<float>& beats) {
    AlgoPtr rhythm = makeAlgo(factory_, "RhythmExtractor2013",
        "method", "multifeature"
    );

    float confidence = 0.0f;
    std::vector<float> bpmEstimates, bpmIntervals;

    rhythm->input("signal").set(audio);
    rhythm->output("bpm").set(bpm);
    rhythm->output("confidence").set(confidence);
    rhythm->output("ticks").set(beats);
    rhythm->output("estimates").set(bpmEstimates);
    rhythm->output("bpmIntervals").set(bpmIntervals);
    safeCompute(rhythm.get(), "RhythmExtractor2013");

    if (bpm <= 0.0f) {
        throw std::runtime_error("RhythmExtractor2013 returned invalid BPM");
    }
}

void EmbeddingBuilder::extractKey(const std::vector<Real>& audio, std::string& key, std::string& scale, float& strength) {
    AlgoPtr keyExt = makeAlgo(factory_, "KeyExtractor",
        "frameSize",   kFrameSize,
        "hopSize",     kHopSize,
        "profileType", "temperley"
    );

    keyExt->input("audio").set(audio);
    keyExt->output("key").set(key);
    keyExt->output("scale").set(scale);
    keyExt->output("strength").set(strength);
    safeCompute(keyExt.get(), "KeyExtractor");
}

EmbeddingBuilder::FrameFeatures EmbeddingBuilder::extractFrameFeatures(const std::vector<Real>& audio) {
    AlgoPtr frameCutter = makeAlgo(factory_, "FrameCutter",
        "frameSize",                kFrameSize,
        "hopSize",                  kHopSize,
        "startFromZero",            true,
        "validFrameThresholdRatio", 0.5f
    );
    AlgoPtr windowing = makeAlgo(factory_, "Windowing",
        "type", "hann"
    );
    AlgoPtr spectrum = makeAlgo(factory_, "Spectrum",
        "size", kFrameSize
    );
    AlgoPtr spectralPeaks = makeAlgo(factory_, "SpectralPeaks",
        "sampleRate",         kSampleRate,
        "maxPeaks",           60,
        "magnitudeThreshold", 0.00001f,
        "minFrequency",       40.0f,
        "maxFrequency",       5000.0f
    );
    AlgoPtr hpcp = makeAlgo(factory_, "HPCP",
        "size",         12,
        "sampleRate",   kSampleRate,
        "referenceFrequency", 440.0f,
        "minFrequency", 40.0f,
        "maxFrequency", 5000.0f,
        "harmonics",    8,
        "bandPreset",   false
    );
    AlgoPtr mfccAlgo = makeAlgo(factory_, "MFCC",
        "inputSize",           kSpectrumSize,
        "numberBands",         26,
        "numberCoefficients",  13,
        "lowFrequencyBound",   20.0f,
        "highFrequencyBound",  8000.0f,
        "sampleRate",          kSampleRate
    );
    AlgoPtr energyAlgo   = makeAlgo(factory_, "Energy");
    AlgoPtr centroidAlgo = makeAlgo(factory_, "SpectralCentroidTime",
        "sampleRate", kSampleRate
    );

    std::vector<Real> frame, windowedFrame, spectrumFrame;
    std::vector<Real> mfccFrame, bandsFrame;  // bandsFrame is required by MFCC output wiring but not used downstream.
    std::vector<Real> peakFreqs, peakMags, hpcpFrame;
    float energyValue   = 0.0f;
    float centroidValue = 0.0f;

    frameCutter->input("signal").set(audio);
    frameCutter->output("frame").set(frame);

    windowing->input("frame").set(frame);
    windowing->output("frame").set(windowedFrame);

    spectrum->input("frame").set(windowedFrame);
    spectrum->output("spectrum").set(spectrumFrame);

    spectralPeaks->input("spectrum").set(spectrumFrame);
    spectralPeaks->output("frequencies").set(peakFreqs);
    spectralPeaks->output("magnitudes").set(peakMags);

    hpcp->input("frequencies").set(peakFreqs);
    hpcp->input("magnitudes").set(peakMags);
    hpcp->output("hpcp").set(hpcpFrame);

    mfccAlgo->input("spectrum").set(spectrumFrame);
    mfccAlgo->output("mfcc").set(mfccFrame);
    mfccAlgo->output("bands").set(bandsFrame);

    energyAlgo->input("array").set(frame);
    energyAlgo->output("energy").set(energyValue);

    centroidAlgo->input("array").set(frame);
    centroidAlgo->output("centroid").set(centroidValue);

    std::vector<float> chromaSum(12, 0.0f);
    std::vector<float> mfccSum(13, 0.0f);
    float energySum   = 0.0f;
    float centroidSum = 0.0f;
    size_t frameCount = 0;

    while (true) {
        int currentFrame = static_cast<int>(frameCount);
        safeCompute(frameCutter.get(), "FrameCutter", currentFrame);
        if (frame.empty()) break;

        safeCompute(windowing.get(), "Windowing", currentFrame);
        safeCompute(spectrum.get(), "Spectrum", currentFrame);
        safeCompute(spectralPeaks.get(), "SpectralPeaks", currentFrame);
        safeCompute(hpcp.get(), "HPCP", currentFrame);
        if (hpcpFrame.size() != 12) {
            throw std::runtime_error("HPCP returned " + std::to_string(hpcpFrame.size()) + " bins, expected 12");
        }

        safeCompute(mfccAlgo.get(), "MFCC", currentFrame);
        if (mfccFrame.size() != 13) {
            throw std::runtime_error("MFCC returned " + std::to_string(mfccFrame.size()) + " coefficients, expected 13");
        }

        safeCompute(energyAlgo.get(), "Energy", currentFrame);
        safeCompute(centroidAlgo.get(), "SpectralCentroidTime", currentFrame);

        for (size_t i = 0; i < 12; ++i) chromaSum[i]  += static_cast<float>(hpcpFrame[i]);
        for (size_t i = 0; i < 13; ++i) mfccSum[i]    += static_cast<float>(mfccFrame[i]);
        energySum   += energyValue;
        centroidSum += centroidValue;
        ++frameCount;
    }

    if (frameCount == 0) {
        throw std::runtime_error("no valid frames produced from audio");
    }

    const float n = static_cast<float>(frameCount);
    FrameFeatures out;
    out.chroma.resize(12);
    out.mfcc.resize(13);
    for (size_t i = 0; i < 12; ++i) out.chroma[i] = chromaSum[i] / n;
    for (size_t i = 0; i < 13; ++i) out.mfcc[i]   = mfccSum[i]   / n;
    out.energy   = energySum   / n;
    out.centroid = centroidSum / n;
    return out;
}

std::vector<float> EmbeddingBuilder::buildFaissVector(float bpm, const std::vector<float>& chroma, const std::vector<float>& mfcc, float energy, float centroid) {
    std::vector<float> v;
    v.reserve(28);

    v.push_back(bpm / 200.0f);
    appendVec(v, chroma);
    appendVec(v, mfcc);
    v.push_back(energy);
    v.push_back(centroid / 10000.0f);

    if (v.size() != 28) {
        throw std::runtime_error("FAISS vector has wrong dimension: " + std::to_string(v.size()));
    }
    return v;
}

FeatureResult EmbeddingBuilder::runPipeline(const std::string& path) {
    FeatureResult result;

    std::cerr << "[EmbeddingBuilder] PID=" << getpid() << " runPipeline path=" << path << std::endl;
    result.duration = 0.0f;
    const std::vector<Real> audio = loadAudio(path, result.duration);
    std::cerr << "[EmbeddingBuilder] loaded audio samples=" << audio.size() << " duration=" << result.duration << std::endl;

    std::vector<float> beats;
    extractRhythm(audio, result.bpm, beats);
    std::cerr << "[EmbeddingBuilder] bpm=" << result.bpm << " beats=" << beats.size() << std::endl;

    extractKey(audio, result.key, result.mode, result.strength);
    std::cerr << "[EmbeddingBuilder] key=" << result.key << " mode=" << result.mode << " strength=" << result.strength << std::endl;

    result.keyIndex = keyNameToIndex(result.key);
    const auto keyEntry = CAMELOT.find(result.keyIndex);
    if (keyEntry == CAMELOT.end()) {
        throw std::runtime_error("no Camelot entry for key index: " + std::to_string(result.keyIndex));
    }
    const auto modeEntry = keyEntry->second.find(result.mode);
    if (modeEntry == keyEntry->second.end()) {
        throw std::runtime_error("no Camelot entry for mode: '" + result.mode + "' (key=" + result.key + ")");
    }
    result.camelot = modeEntry->second;

    const FrameFeatures ff = extractFrameFeatures(audio);
    std::cerr << "[EmbeddingBuilder] frame features computed: chroma=" << ff.chroma.size() << " mfcc=" << ff.mfcc.size() << std::endl;
    result.chroma   = ff.chroma;
    result.mfcc     = ff.mfcc;
    result.energy   = ff.energy;
    result.centroid = ff.centroid;

    result.faissVector = buildFaissVector(result.bpm, result.chroma, result.mfcc, result.energy, result.centroid);

    return result;
}