#ifndef LOGGER_H
#define LOGGER_H

#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <ctime>
#include <string>
#include <mutex>
#include <fstream>
#include <iostream>

enum LogLevel {
    LOG_DEBUG = 0,
    LOG_INFO,
    LOG_WARN,
    LOG_ERROR,
    LOG_FATAL
};

class Logger {
public:
    static Logger& getInstance();

    void init(const char* filename, LogLevel level = LOG_DEBUG, bool console = true);
    void setLevel(LogLevel level);
    void log(LogLevel level, const char* file, int line, const char* func, const char* fmt, ...);

private:
    Logger();
    ~Logger();
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    LogLevel level_;
    bool console_;
    std::ofstream file_;
    std::mutex mutex_;
};

#define LOG_DEBUG(fmt, ...) Logger::getInstance().log(LOG_DEBUG, __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)
#define LOG_INFO(fmt, ...)  Logger::getInstance().log(LOG_INFO,  __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)
#define LOG_WARN(fmt, ...)  Logger::getInstance().log(LOG_WARN,  __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...) Logger::getInstance().log(LOG_ERROR, __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)
#define LOG_FATAL(fmt, ...) Logger::getInstance().log(LOG_FATAL, __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)

#endif
