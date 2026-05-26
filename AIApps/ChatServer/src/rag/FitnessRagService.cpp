#include "../../include/rag/FitnessRagService.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <utility>

namespace
{

std::string trimAscii(const std::string& value)
{
    auto begin = std::find_if_not(value.begin(), value.end(), [](unsigned char ch) {
        return std::isspace(ch);
    });
    auto end = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char ch) {
        return std::isspace(ch);
    }).base();
    if (begin >= end)
    {
        return "";
    }
    return std::string(begin, end);
}

size_t utf8Length(const std::string& text)
{
    size_t count = 0;
    for (size_t i = 0; i < text.size();)
    {
        unsigned char ch = static_cast<unsigned char>(text[i]);
        size_t len = 1;
        if ((ch & 0x80) == 0) len = 1;
        else if ((ch & 0xE0) == 0xC0 && i + 1 < text.size()) len = 2;
        else if ((ch & 0xF0) == 0xE0 && i + 2 < text.size()) len = 3;
        else if ((ch & 0xF8) == 0xF0 && i + 3 < text.size()) len = 4;
        i += len;
        ++count;
    }
    return count;
}

std::string currentTimestamp()
{
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm local {};
#ifdef _WIN32
    localtime_s(&local, &t);
#else
    localtime_r(&t, &local);
#endif
    std::ostringstream oss;
    oss << std::put_time(&local, "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

std::string idPrefix()
{
    auto now = std::chrono::system_clock::now().time_since_epoch();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
    return std::to_string(ms);
}

int clampTopK(int topK)
{
    if (topK <= 0)
    {
        return 5;
    }
    return std::min(topK, 10);
}

} // namespace

namespace rag
{

FitnessRagService::FitnessRagService(std::string storePath)
    : embeddingClient_(256),
      store_(std::move(storePath), embeddingClient_)
{
}

IndexResult FitnessRagService::indexText(const std::string& title,
    const std::string& source,
    const std::string& content)
{
    std::string cleanContent = trimAscii(content);
    if (cleanContent.empty())
    {
        return { false, "content 不能为空", 0 };
    }
    if (utf8Length(cleanContent) < 20)
    {
        return { false, "content 至少需要 20 个字符", 0 };
    }

    std::vector<std::string> texts = splitter_.split(cleanContent, 600, 80);
    if (texts.empty())
    {
        return { false, "未生成有效知识片段", 0 };
    }

    std::string cleanTitle = trimAscii(title).empty() ? "未命名知识" : trimAscii(title);
    std::string cleanSource = trimAscii(source).empty() ? "manual" : trimAscii(source);
    std::string docId = "doc-" + idPrefix();
    std::string createdAt = currentTimestamp();

    std::vector<DocumentChunk> chunks;
    for (size_t i = 0; i < texts.size(); ++i)
    {
        DocumentChunk chunk;
        chunk.docId = docId;
        chunk.chunkId = docId + "-chunk-" + std::to_string(i + 1);
        chunk.title = cleanTitle;
        chunk.source = cleanSource;
        chunk.content = texts[i];
        chunk.embedding = embeddingClient_.embed(texts[i]);
        chunk.createdAt = createdAt;
        chunks.push_back(std::move(chunk));
    }

    if (!store_.load())
    {
        return { false, "读取本地知识库失败", 0 };
    }
    if (!store_.addChunks(chunks) || !store_.save())
    {
        return { false, "保存本地知识库失败", 0 };
    }

    return { true, "知识导入成功", chunks.size() };
}

std::vector<SearchResult> FitnessRagService::search(const std::string& query, int topK)
{
    std::string cleanQuery = trimAscii(query);
    if (cleanQuery.empty())
    {
        return {};
    }
    store_.load();
    return store_.search(cleanQuery, clampTopK(topK));
}

std::string FitnessRagService::buildRagPrompt(const std::string& question,
    const std::vector<SearchResult>& retrievedChunks) const
{
    std::ostringstream prompt;
    prompt
        << "你是 AI 私人健身教练。请基于下方健身知识库参考片段回答用户问题。\n\n"
        << "参考片段：\n";

    if (retrievedChunks.empty())
    {
        prompt << "（本次没有检索到可用参考片段）\n";
    }
    else
    {
        for (size_t i = 0; i < retrievedChunks.size(); ++i)
        {
            const auto& result = retrievedChunks[i];
            prompt
                << "[" << (i + 1) << "] 标题：" << result.chunk.title
                << "；来源：" << result.chunk.source
                << "；相关度：" << std::fixed << std::setprecision(4) << result.score << "\n"
                << result.chunk.content << "\n\n";
        }
    }

    prompt
        << "用户问题：\n" << question << "\n\n"
        << "回答规则：\n"
        << "1. 优先依据参考片段回答。\n"
        << "2. 如果参考片段不足，要说明“知识库中没有足够依据”，再给一般性建议。\n"
        << "3. 不要编造参考片段中没有的来源。\n"
        << "4. 对伤病、疼痛、胸闷、头晕等问题，不做医疗诊断。\n"
        << "5. 如涉及伤病或严重不适，建议咨询医生或专业人士。\n"
        << "6. 不建议危险训练、极端节食或过度训练。\n"
        << "7. 回答使用中文。\n"
        << "8. 回答结构清晰，适合普通健身用户理解。\n"
        << "9. 最后列出“参考片段摘要”。\n";

    return prompt.str();
}

bool FitnessRagService::loadStore()
{
    return store_.load();
}

bool FitnessRagService::saveStore()
{
    return store_.save();
}

size_t FitnessRagService::size() const
{
    return store_.size();
}

const std::string& FitnessRagService::storePath() const
{
    return store_.path();
}

} // namespace rag
