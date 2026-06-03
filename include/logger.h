#ifndef LOGGER_H
#define LOGGER_H

#include <iostream>
#include <fstream>
#include <string>
#include <mutex>

using namespace std;

class Logger
{
private:
    ofstream log_file;          // 日志文件输出流
    mutex mtx;                  // 保护日志文件，防止多个线程同时写日志

private:
    Logger() = default;         // 构造函数私有化，配合单例使用
    string get_current_time();  // 获取当前时间字符串

public:
    static Logger& get_instance();

    bool init(const string& file_name);
    void write_log(const string& level, const string& message);
    void flush();

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    ~Logger();
};

#endif
