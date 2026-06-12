#include "http.h"
#include <sstream>
#include <cctype>

using namespace std;

// 去掉字符串左右两边的空白字符
static string trim(const string &s)
{
    size_t left = 0;
    size_t right = s.size();
    while (left < right && isspace((unsigned char)s[left]))
    {
        left++;
    }
    while (right > left && isspace((unsigned char)s[right - 1]))
    {
        right--;
    }
    return s.substr(left, right - left);
}

// 根据状态码返回状态描述
static string reason_phrase(int status_code)
{
    if (status_code == 200)
        return "OK";
    if (status_code == 400)
        return "Bad Request";
    if (status_code == 404)
        return "Not Found";
    if (status_code == 405)
        return "Method Not Allowed";
    if (status_code == 431)
        return "Request Header Fields Too Large";
    if (status_code == 500)
        return "Internal Server Error";
    return "Unknown";
}

// HTTP 请求头结束标志：\r\n\r\n
bool http_header_complete(const string &request_text)
{
    return request_text.find("\r\n\r\n") != string::npos;
}

// 解析 HTTP 请求
bool parse_http_request(const string &request_text, HttpRequest &request)
{
    request = HttpRequest();
    size_t header_end = request_text.find("\r\n\r\n");
    if (header_end == string::npos)
    {
        return false;
    }
    string header_text = request_text.substr(0, header_end);
    request.body = request_text.substr(header_end + 4);
    stringstream ss(header_text);
    string line;
    // 解析第一行：GET / HTTP/1.1
    if (!getline(ss, line))
    {
        return false;
    }
    if (!line.empty() && line.back() == '\r')
    {
        line.pop_back();
    }
    stringstream first_line(line);
    first_line >> request.method >> request.path >> request.version;
    if (request.method.empty() || request.path.empty() || request.version.empty())
    {
        return false;
    }
    // 解析请求头
    while (getline(ss, line))
    {
        if (!line.empty() && line.back() == '\r')
        {
            line.pop_back();
        }
        size_t pos = line.find(':');
        if (pos == string::npos)
        {
            continue;
        }
        string key = trim(line.substr(0, pos));
        string value = trim(line.substr(pos + 1));
        request.headers[key] = value;
    }
    // 判断是否 keep-alive
    if (request.headers.count("Connection"))
    {
        string connection = request.headers["Connection"];
        for (char &c : connection)
        {
            c = tolower((unsigned char)c);
        }
        request.keep_alive = (connection == "keep-alive");
    }
    else
    {
        request.keep_alive = (request.version == "HTTP/1.1");
    }
    return true;
}
// 构造 HTTP 响应
string build_http_response(
    int status_code,
    const string &body,
    const string &content_type,
    bool keep_alive
)
{
    string response;
    response += "HTTP/1.1 ";
    response += to_string(status_code);
    response += " ";
    response += reason_phrase(status_code);
    response += "\r\n";
    response += "Content-Type: ";
    response += content_type;
    response += "\r\n";
    response += "Content-Length: ";
    response += to_string(body.size());
    response += "\r\n";
    if (keep_alive)
    {
        response += "Connection: keep-alive\r\n";
    }
    else
    {
        response += "Connection: close\r\n";
    }
    response += "\r\n";
    response += body;
    return response;
}
