#pragma once
#include <string>

using namespace std;

// 根据 HTTP 请求文本构造响应
string build_response_by_request(const string &request_text);
