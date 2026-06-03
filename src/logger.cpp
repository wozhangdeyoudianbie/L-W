#include "../include/logger.h"

#include <chrono>
#include <iomanip>
#include <sstream>

Logger &Logger::get_instance()
{
    static Logger instance;
    return instance;
}

bool Logger::init(const string &file_name)
{
    unique_lock<mutex> lock(mtx);

    log_file.open(file_name, ios::app);

    return log_file.is_open();
}

string Logger::get_current_time()
{
    auto now = chrono::system_clock::now();
    auto time_t_now = chrono::system_clock::to_time_t(now);

    auto ms = chrono::duration_cast<chrono::milliseconds>(
        now.time_since_epoch()
    ) % 1000;

    tm local_time;

    localtime_r(&time_t_now, &local_time);

    stringstream ss;

    ss << put_time(&local_time, "%Y-%m-%d %H:%M:%S");
    ss << "." << setw(3) << setfill('0') << ms.count();

    return ss.str();
}

void Logger::write_log(const string &level, const string &message)
{
    unique_lock<mutex> lock(mtx);

    if (!log_file.is_open())
    {
        return;
    }

    log_file << "[" << get_current_time() << "] "
        << "[" << level << "] "
        << message << endl;
}

void Logger::flush()
{
    unique_lock<mutex> lock(mtx);

    if (log_file.is_open())
    {
        log_file.flush();
    }
}

Logger::~Logger()
{
    unique_lock<mutex> lock(mtx);

    if (log_file.is_open())
    {
        log_file.flush();
        log_file.close();
    }
}
