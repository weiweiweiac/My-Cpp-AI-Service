#include "AIApps/ChatServer/include/rag/FitnessRagService.h"
#include "AIApps/ChatServer/include/rag/HashEmbeddingClient.h"
#include "AIApps/ChatServer/include/rag/TextSplitter.h"

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <numeric>
#include <string>
#include <vector>

namespace
{

void require(bool condition, const std::string& message)
{
    if (!condition)
    {
        std::cerr << message << std::endl;
        std::exit(1);
    }
}

double dot(const std::vector<float>& a, const std::vector<float>& b)
{
    double value = 0.0;
    for (size_t i = 0; i < a.size() && i < b.size(); ++i)
    {
        value += static_cast<double>(a[i]) * static_cast<double>(b[i]);
    }
    return value;
}

double norm(const std::vector<float>& values)
{
    return std::sqrt(dot(values, values));
}

} // namespace

int main()
{
    rag::TextSplitter splitter;
    auto chunks = splitter.split(
        "卧推时保持肩胛后缩下沉，控制下放幅度，避免肩部前顶。\n\n"
        "   \n\n"
        "深蹲时保持核心收紧，膝盖方向与脚尖一致，动作全程稳定。\n\n"
        "硬拉时保持背部中立，先建立张力，再让杠铃贴近身体上拉。",
        28,
        6);

    require(chunks.size() >= 2, "TextSplitter should split non-empty Chinese paragraphs into chunks");
    require(chunks[0].find("卧推") != std::string::npos, "First chunk should keep original Chinese content");

    rag::HashEmbeddingClient embedding(256);
    auto bench = embedding.embed("卧推 肩胛 后缩 肩膀 疼痛");
    auto benchSimilar = embedding.embed("卧推时肩胛后缩下沉可以减少肩部前顶");
    auto unrelated = embedding.embed("睡眠和蛋白质摄入会影响恢复");

    require(bench.size() == 256, "HashEmbeddingClient should return configured vector size");
    require(std::abs(norm(bench) - 1.0) < 0.0001, "HashEmbeddingClient should L2-normalize vectors");
    require(dot(bench, benchSimilar) > dot(bench, unrelated),
        "HashEmbeddingClient should give related fitness text higher similarity");

    const std::filesystem::path storePath = std::filesystem::current_path() / "rag_core_test_store.json";
    std::filesystem::remove(storePath);

    rag::FitnessRagService service(storePath.string());
    auto indexResult = service.indexText(
        "卧推动作要点",
        "unit-test",
        "卧推时应保持肩胛后缩下沉，控制下放幅度，避免肩部前顶。"
        "如果出现明显疼痛，应停止训练并咨询医生或专业人士。");

    require(indexResult.success, "FitnessRagService should index valid text");
    require(indexResult.chunkCount > 0, "FitnessRagService should create at least one chunk");
    require(std::filesystem::exists(storePath), "FitnessRagService should persist JSON vector store");

    auto benchResult = service.indexText(
        "卧推动作要点",
        "unit-test",
        "卧推时应保持肩胛后缩下沉，控制下放幅度，避免肩部前顶。如果出现明显疼痛，应停止训练并咨询医生或专业人士。");
    require(benchResult.success, "FitnessRagService should index bench knowledge");

    auto squatResult = service.indexText(
        "深蹲动作要点",
        "unit-test",
        "深蹲时保持核心收紧，膝盖方向与脚尖一致，髋膝同步屈伸，动作全程稳定，不要为了重量牺牲动作质量。");
    require(squatResult.success, "FitnessRagService should index squat knowledge");

    auto proteinResult = service.indexText(
        "蛋白质摄入建议",
        "unit-test",
        "增肌或减脂期间都应关注蛋白质摄入，优先选择鸡蛋、鱼肉、瘦肉、奶制品和豆制品，并结合总热量安排。");
    require(proteinResult.success, "FitnessRagService should index nutrition knowledge");

    auto concreteResults = service.search("卧推肩膀疼怎么办", 5);
    bool hasBenchChunk = false;
    for (const auto& result : concreteResults)
    {
        hasBenchChunk = hasBenchChunk || result.chunk.title.find("卧推动作要点") != std::string::npos;
    }
    require(hasBenchChunk, "FitnessRagService should retrieve bench knowledge for shoulder pain query");

    auto results = service.search("卧推肩膀疼怎么办", 3);
    require(!results.empty(), "FitnessRagService should retrieve indexed knowledge");
    require(results.front().chunk.title == "卧推动作要点", "Search result should include chunk metadata");
    require(results.front().score > 0.0, "Search result should include positive similarity score");

    std::string prompt = service.buildRagPrompt("卧推肩膀疼怎么办？", results);
    require(prompt.find("你是 AI 私人健身教练") != std::string::npos,
        "RAG prompt should include the coach role");
    require(prompt.find("知识库中没有足够依据") != std::string::npos,
        "RAG prompt should include insufficient-evidence rule");
    require(prompt.find("参考片段摘要") != std::string::npos,
        "RAG prompt should ask for reference summaries");

    std::filesystem::remove(storePath);
    return 0;
}
