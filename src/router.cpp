#include "router.h"
#include <fstream>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include "http.h"
#include "logger.h"

using namespace std;

// 请求路径映射为磁盘文件路径（根路径指向首页）
string get_file_path(const string &request_path)
{
    string path = request_path;
    if (path == "/")
    {
        path = "/index.html";
    }
    return "www" + path;
}

// 判断：是否为普通文件
bool is_regular_file(const string &file_path)
{
    struct stat st;
    if (stat(file_path.c_str(), &st) == -1)
    {
        return false;
    }
    return S_ISREG(st.st_mode);
}

// 读取：文件内容读入字符串（二进制）
bool read_file(const string &file_path, string &content)
{
    ifstream file(file_path, ios::binary);
    if (!file.is_open())
    {
        return false;
    }
    stringstream ss;
    ss << file.rdbuf();
    content = ss.str();
    return true;
}

// 提取：文件扩展名（无扩展名返回空串）
string get_file_extension(const string &file_path)
{
    size_t pos = file_path.rfind('.');
    if (pos == string::npos)
    {
        return "";
    }
    return file_path.substr(pos + 1);
}

// 映射：扩展名 → Content-Type（未知返回二进制流）
string get_content_type(const string &file_path)
{
    string ext = get_file_extension(file_path);
    if (ext == "html" || ext == "htm")
    {
        return "text/html; charset=utf-8";
    }
    if (ext == "css")
    {
        return "text/css; charset=utf-8";
    }
    if (ext == "js")
    {
        return "application/javascript; charset=utf-8";
    }
    if (ext == "txt")
    {
        return "text/plain; charset=utf-8";
    }
    if (ext == "jpg" || ext == "jpeg")
    {
        return "image/jpeg";
    }
    if (ext == "png")
    {
        return "image/png";
    }
    if (ext == "gif")
    {
        return "image/gif";
    }
    return "application/octet-stream";
}

// 构造静态文件响应（找不到 404 / 读取失败 500 / 成功 200）
string build_static_file_response(const string &request_path)
{
    string file_path = get_file_path(request_path);
    if (!is_regular_file(file_path))
    {
        string body = "404 Not Found\n";
        return build_http_response(
            404,
            body,
            "text/plain; charset=utf-8",
            false);
    }
    string body;
    if (!read_file(file_path, body))
    {
        string error_body = "500 Internal Server Error\n";
        return build_http_response(
            500,
            error_body,
            "text/plain; charset=utf-8",
            false);
    }
    string content_type = get_content_type(file_path);
    return build_http_response(
            200,
            body,
            content_type,
            false);
}

// 根据 HTTP 请求文本构造响应
string build_response_by_request(const string &request_text)
{
    HttpRequest request;
    if (!parse_http_request(request_text, request))
    {
        string body = "400 Bad Request\n";
        return build_http_response(
            400,
            body,
            "text/plain; charset=utf-8",
            false
        );
    }
    Logger::get_instance().write_log(
        "INFO",
        "解析HTTP请求：method = " + request.method +
        "，path = " + request.path +
        "，version = " + request.version
    );
    if (request.method != "GET")
    {
        string body = "405 Method Not Allowed\n";
        return build_http_response(
            405,
            body,
            "text/plain; charset=utf-8",
            false
        );
    }

    if (request.path == "/hello")
    {
        string body = "hello from C++ WebServer\n";
        return build_http_response(
            200,
            body,
            "text/plain; charset=utf-8",
            false
        );
    }
    return build_static_file_response(request.path);
}
