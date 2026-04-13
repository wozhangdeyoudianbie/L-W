#include"../include/logger.h"
#include<chrono>
#include<iomanip>
#include<sstream>
#include<ctime>
#include "logger.h"

Logger::Logger()
{
}

Logger::~Logger()
{
    if (fout.is_open())
        fout.close();
}

bool Logger::init(const string &file_name)
{
    fout.open(file_name, ios::app);
    return fout.is_open();
}

void Logger::write_log(Log_level level, const string &message)
{
    lock_guard<mutex> lock(mtx);

    if (!fout.is_open())
        return;

    fout << "[" << get_time_stamp() << "] "
        << "[" << level_to_string(level) << "] "
        << message << endl;

    fout.flush();
}

void Logger::flush()
{
    lock_guard<mutex> lock(mtx);
    if (fout.is_open())
        fout.flush();
}

string Logger::get_time_stamp()
{
    using namespace chrono;

    auto now = system_clock::now();
    auto ms = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;

    time_t now_c = system_clock::to_time_t(now);
    tm local_tm;
    localtime_r(&now_c, &local_tm);

    stringstream ss;
    ss << put_time(&local_tm, "%Y-%m-%d %H:%M:%S")
        << "."
        << setw(3) << setfill('0') << ms.count();

    return ss.str();
}

string Logger::level_to_string(Log_level level)
{
    if (level == Log_level::INFO)
        return "INFO";
    else if (level == Log_level::WARNING)
        return "WARNING";
    else
        return "ERROR";
}
