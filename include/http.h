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
    bool keep_alive = false; // 是否保持长连接
};

bool http_header_complete(const string &request_text);

bool parse_http_request(const string &request_text, HttpRequest &request);

string build_http_response(
    int status_code,
    const string &body,
    const string &content_type = "text/plain; charset=utf-8",
    bool keep_alive = false
);
