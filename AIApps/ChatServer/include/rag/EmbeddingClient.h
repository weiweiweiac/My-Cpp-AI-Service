#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace rag
{

class EmbeddingClient
{
public:
    struct Config
    {
        std::string apiKey;
        std::string baseUrl;
        std::string model;
        size_t mockDimensions { 256 };
    };

    EmbeddingClient();
    explicit EmbeddingClient(Config config);
    virtual ~EmbeddingClient() = default;

    virtual std::vector<float> embed(const std::string& text) const;
    std::vector<float> embedText(const std::string& text) const;
    std::vector<std::vector<float>> embedBatch(const std::vector<std::string>& texts) const;

    bool isMockMode() const;
    const Config& config() const { return config_; }

private:
    std::vector<float> requestRemoteEmbedding(const std::string& text) const;
    std::vector<float> buildMockEmbedding(const std::string& text) const;

    Config config_;
};

} // namespace rag
