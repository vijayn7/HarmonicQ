#include <pistache/router.h>

#include <optional>
#include <string>

class embeddingBuilder {
public:

    std::optional<std::string> buildEmbedding(const Pistache::Rest::Request& request, float* output) {
        const std::string body = request.body();
        if (body.empty()) {
            return std::string("audio file is required");
        }

        // Placeholder for actual embedding logic
        // For demonstration, we'll just fill the output with dummy values
        for (size_t i = 0; i < 28; ++i) {
            output[i] = static_cast<float>(i) / 28.0f; // Dummy embedding values
        }
        return std::nullopt;
    }

};