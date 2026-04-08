#ifndef LOGGER_H
#define LOGGER_H

#include<iostream>
#include<string>
#include<fstream>
#include<mutex>

using namespace std;

#define endl '\n'

enum class Log_level//日志级别（Log Level，日志等级）
{
    INFO,
    WARNING,
    ERROR
};

class Logger
{
public:
    Logger();
    ~Logger();

    bool init(const string &file_name);//file_name（File Name，文件名）
    void write_log(Log_level level, const string &message);//message（Message，消息内容）
    void flush();

private:
    ofstream fout;//fout（File Output Stream，文件输出流）
    mutex mtx;//mtx（Mutex，互斥锁）
    string get_time_stamp();//time_stamp（Time Stamp，时间戳）
    string level_to_string(Log_level level);
};

#endif
