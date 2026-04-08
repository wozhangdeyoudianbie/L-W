#include<iostream>
#include"include/logger.h"

using namespace std;
#define endl '\n'
using ll = long long;

int main()
{
    cin.tie(0)->sync_with_stdio(0);

    Logger logger;
    if (!logger.init("server.log"))
    {
        cout << "日志文件打开失败" << endl;
        return 0;
    }

    logger.write_log(Log_level::INFO, "server start");
    logger.write_log(Log_level::WARNING, "this is a warning message");
    logger.write_log(Log_level::ERROR, "this is an error message");

    cout << "日志写入完成" << endl;

    return 0;
}
