#include "HttpServer/include/http/HttpStreamWriter.h"

#include <cstdlib>
#include <iostream>
#include <string>

namespace
{

void assertEqual(const std::string& actual, const std::string& expected, const std::string& label)
{
    if (actual != expected)
    {
        std::cerr << label << "\nexpected:\n" << expected << "\nactual:\n" << actual << std::endl;
        std::exit(1);
    }
}

} // namespace

int main()
{
    assertEqual(
        http::HttpStreamWriter::formatEvent("message", "{\"content\":\"hi\"}"),
        "event: message\ndata: {\"content\":\"hi\"}\n\n",
        "formats a single SSE event");

    assertEqual(
        http::HttpStreamWriter::jsonData("content", "line1\n\"quoted\"\\slash"),
        "{\"content\":\"line1\\n\\\"quoted\\\"\\\\slash\"}",
        "escapes JSON data payloads");

    assertEqual(
        http::HttpStreamWriter::formatEvent("done", http::HttpStreamWriter::jsonData("message", "done")),
        "event: done\ndata: {\"message\":\"done\"}\n\n",
        "formats done event with JSON data");

    return 0;
}
