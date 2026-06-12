#include "router.h"
#include "http.h"
#include "logger.h"

using namespace std;

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

    if (request.path == "/")
    {
        string body;
        body += "<!DOCTYPE html>\n";
        body += "<html>\n";
        body += "<head>\n";
        body += "    <meta charset=\"UTF-8\">\n";
        body += "    <title>My C++ WebServer</title>\n";
        body += "</head>\n";
        body += "<body>\n";
        body += "    <h1>你好，这是我的 C++ WebServer</h1>\n";
        body += "    <p>HTTP 模块已经拆分成功。</p>\n";
        body += "    <p>Router 模块也已经接入。</p>\n";
        body += "</body>\n";
        body += "</html>\n";
        return build_http_response(
            200,
            body,
            "text/html; charset=utf-8",
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

    string body = "404 Not Found\n";
    return build_http_response(
        404,
        body,
        "text/plain; charset=utf-8",
        false
    );
}
