#pragma once
#include <string>
#include <unordered_map>
using namespace std;

struct HttpRequest
{
    string method;
    string path;
    string version;
    unordered_map<string, string> headers;
    string body;
    bool keep_alive = false;
};

bool http_header_complete(const string &request_text);   // 判断：请求头是否完整（含 \r\n\r\n）

bool parse_http_request(const string &request_text, HttpRequest &request);   // 解析 HTTP 请求（请求行+头+body）

string build_http_response(                                                   // 构造 HTTP 响应
    int status_code,
    const string &body,
    const string &content_type = "text/plain; charset=utf-8",
    bool keep_alive = false
);
