#include <pistache/router.h>

#include <filesystem>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include <essentia/algorithmfactory.h>
#include <essentia/essentia.h>
#include <essentia/essentiamath.h>

using essentia::standard::Algorithm;
using essentia::standard::AlgorithmFactory;
using essentia::Real;

class embeddingBuilder {
private:
    AlgorithmFactory* factory = nullptr;

    static constexpr float kSampleRate = 44100.0f;

    static int keyNameToIndex(const std::string& key) {
        static const std::unordered_map<std::string, int> keyMap = {
            {"C", 0}, {"C#", 1}, {"D", 2}, {"D#", 3}, {"E", 4}, {"F", 5},
            {"F#", 6}, {"G", 7}, {"G#", 8}, {"A", 9}, {"A#", 10}, {"B", 11},
            {"Db", 1}, {"Eb", 3}, {"Gb", 6}, {"Ab", 8}, {"Bb", 10}
        };

        const auto it = keyMap.find(key);
        if (it == keyMap.end()) {
            throw std::runtime_error("unsupported key returned by KeyExtractor: " + key);
        }

        return it->second;
    }

    static void appendVector(std::vector<float>& destination, const std::vector<float>& values) {
        destination.insert(destination.end(), values.begin(), values.end());
    }

    std::vector<Real> loadAudio(const std::string& inputPath) {
        Algorithm* loader = factory->create("EasyLoader",
            "filename", inputPath,
            "sampleRate", kSampleRate,
            "downmix", "mix"
        );

        std::vector<Real> audio;

        loader->output("audio").set(audio);
        loader->compute();

        delete loader;

        if (audio.empty()) {
            throw std::runtime_error("audio decoder returned no samples");
        }

        return audio;
    }

    void extractRhythm(const std::vector<Real>& audio, float& bpm, std::vector<float>& beats) {
        Algorithm* rhythm = factory->create("RhythmExtractor2013",
            "method", "multifeature"
        );

        float confidence = 0.0f;
        std::vector<float> bpmEstimates;
        std::vector<float> bpmIntervals;

        rhythm->input("signal").set(audio);
        rhythm->output("bpm").set(bpm);
        rhythm->output("confidence").set(confidence);
        rhythm->output("ticks").set(beats);
        rhythm->output("estimates").set(bpmEstimates);
        rhythm->output("bpmIntervals").set(bpmIntervals);
        rhythm->compute();

        delete rhythm;

        if (bpm <= 0.0f) {
            throw std::runtime_error("RhythmExtractor2013 returned an invalid BPM");
        }
    }

    void extractKey(const std::vector<Real>& audio, std::string& key, std::string& scale, float& strength) {
        Algorithm* keyExtractor = factory->create("KeyExtractor",
            "frameSize", 4096,
            "hopSize", 2048,
            "profileType", "temperley"
        );

        keyExtractor->input("audio").set(audio);
        keyExtractor->output("key").set(key);
        keyExtractor->output("scale").set(scale);
        keyExtractor->output("strength").set(strength);
        keyExtractor->compute();

        delete keyExtractor;
    }

    struct FrameFeatureResult {
        std::vector<float> chroma;
        std::vector<float> mfcc;
        float energy = 0.0f;
        float centroid = 0.0f;
    };

    FrameFeatureResult extractFrameFeatures(const std::vector<Real>& audio) {
        Algorithm* frameCutter = factory->create("FrameCutter",
            "frameSize", 32768,
            "hopSize", 2048,
            "startFromZero", true,
            "validFrameThresholdRatio", 0.5f
        );
        Algorithm* windowing = factory->create("Windowing",
            "type", "hann"
        );
        Algorithm* spectrum = factory->create("Spectrum",
            "size", 32768
        );
        Algorithm* chromagram = factory->create("Chromagram",
            "sampleRate", kSampleRate,
            "binsPerOctave", 12
        );
        Algorithm* mfcc = factory->create("MFCC",
            "inputSize", 16385,
            "numberBands", 26,
            "numberCoefficients", 13,
            "lowFrequencyBound", 20.0f,
            "highFrequencyBound", 8000.0f,
            "sampleRate", kSampleRate
        );
        Algorithm* energy = factory->create("Energy");
        Algorithm* centroid = factory->create("SpectralCentroidTime",
            "sampleRate", kSampleRate
        );

        std::vector<Real> frame;
        std::vector<Real> windowedFrame;
        std::vector<Real> spectrumFrame;
        std::vector<Real> chromaFrame;
        std::vector<Real> mfccFrame;
        std::vector<Real> bandsFrame;
        std::vector<float> chromaSum(12, 0.0f);
        std::vector<float> mfccSum(13, 0.0f);
        float energySum = 0.0f;
        float centroidSum = 0.0f;
        size_t frameCount = 0;

        float energyValue = 0.0f;
        float centroidValue = 0.0f;

        frameCutter->input("signal").set(audio);
        frameCutter->output("frame").set(frame);

        windowing->input("frame").set(frame);
        windowing->output("frame").set(windowedFrame);

        spectrum->input("frame").set(windowedFrame);
        spectrum->output("spectrum").set(spectrumFrame);

        chromagram->input("frame").set(windowedFrame);
        chromagram->output("chromagram").set(chromaFrame);

        mfcc->input("spectrum").set(spectrumFrame);
        mfcc->output("bands").set(bandsFrame);
        mfcc->output("mfcc").set(mfccFrame);

        energy->input("array").set(frame);
        energy->output("energy").set(energyValue);

        centroid->input("array").set(frame);
        centroid->output("centroid").set(centroidValue);

        while (true) {
            frameCutter->compute();
            if (frame.empty()) {
                break;
            }

            windowing->compute();
            spectrum->compute();
            chromagram->compute();
            mfcc->compute();
            energy->compute();
            centroid->compute();

            if (chromaFrame.size() != chromaSum.size()) {
                throw std::runtime_error("Chromagram did not produce 12 bins");
            }

            if (mfccFrame.size() != mfccSum.size()) {
                throw std::runtime_error("MFCC did not produce 13 coefficients");
            }

            for (size_t i = 0; i < chromaSum.size(); ++i) {
                chromaSum[i] += static_cast<float>(chromaFrame[i]);
            }

            for (size_t i = 0; i < mfccSum.size(); ++i) {
                mfccSum[i] += static_cast<float>(mfccFrame[i]);
            }

            energySum += energyValue;
            centroidSum += centroidValue;
            ++frameCount;
        }

        delete frameCutter;
        delete windowing;
        delete spectrum;
        delete chromagram;
        delete mfcc;
        delete energy;
        delete centroid;

        if (frameCount == 0) {
            throw std::runtime_error("no analysis frames were produced from the audio input");
        }

        FrameFeatureResult features;
        features.chroma = chromaSum;
        features.mfcc = mfccSum;
        features.energy = energySum / static_cast<float>(frameCount);
        features.centroid = centroidSum / static_cast<float>(frameCount);

        for (float& value : features.chroma) {
            value /= static_cast<float>(frameCount);
        }

        for (float& value : features.mfcc) {
            value /= static_cast<float>(frameCount);
        }

        return features;
    }

    std::vector<float> buildFaissVector(float bpm, const std::vector<float>& chroma, const std::vector<float>& mfcc, float energy, float centroid) {
        std::vector<float> embedding;
        embedding.reserve(28);

        embedding.push_back(bpm);
        appendVector(embedding, chroma);
        appendVector(embedding, mfcc);
        embedding.push_back(energy);
        embedding.push_back(centroid);

        if (embedding.size() != 28) {
            throw std::runtime_error("embedding vector must contain 28 values");
        }

        return embedding;
    }

public:
    embeddingBuilder() {
        essentia::init();
        factory = &AlgorithmFactory::instance();
    }

    ~embeddingBuilder() {
        essentia::shutdown();
    }

    std::optional<std::string> buildEmbedding(const Pistache::Rest::Request& request, float* output) {
        if (output == nullptr) {
            return std::string("embedding output buffer is required");
        }

        const std::string body = request.body();
        if (body.empty()) {
            return std::string("audio file is required");
        }

        const std::filesystem::path tempFilePath =
            std::filesystem::temp_directory_path() /
            ("audio_input_" + std::to_string(std::hash<std::string>{}(body)) + ".tmp");

        std::ofstream tempFile(tempFilePath, std::ios::binary);
        if (!tempFile) {
            throw std::runtime_error("failed to create temporary file");
        }

        tempFile.write(body.data(), static_cast<std::streamsize>(body.size()));
        if (!tempFile) {
            std::error_code removeError;
            std::filesystem::remove(tempFilePath, removeError);
            throw std::runtime_error("failed to write audio data to temporary file");
        }
        tempFile.close();

        try {
            const std::vector<Real> audio = loadAudio(tempFilePath.string());

            float bpm = 0.0f;
            std::vector<float> beats;
            extractRhythm(audio, bpm, beats);

            std::string key;
            std::string scale;
            float strength = 0.0f;
            extractKey(audio, key, scale, strength);

            const int keyIndex = keyNameToIndex(key);
            (void)keyIndex;
            (void)scale;
            (void)strength;
            (void)beats;

            const FrameFeatureResult features = extractFrameFeatures(audio);
            const std::vector<float> embedding = buildFaissVector(
                bpm,
                features.chroma,
                features.mfcc,
                features.energy,
                features.centroid
            );

            for (size_t i = 0; i < embedding.size(); ++i) {
                output[i] = embedding[i];
            }

            std::error_code removeError;
            std::filesystem::remove(tempFilePath, removeError);
            return std::nullopt;
        } catch (const std::exception& ex) {
            std::error_code removeError;
            std::filesystem::remove(tempFilePath, removeError);
            return std::string(ex.what());
        }
    }
};