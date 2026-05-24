#include "../include/logger.h"

Logger::Logger() : level_(LOG_DEBUG), console_(true) {}

Logger::~Logger() { if (file_.is_open()) file_.close(); }

Logger& Logger::getInstance() {
    static Logger instance;
    return instance;
}

void Logger::init(const char* filename, LogLevel level, bool console) {
    level_ = level;
    console_ = console;
    if (filename && strlen(filename) > 0) {
        file_.open(filename, std::ios::app);
    }
}

void Logger::setLevel(LogLevel level) { level_ = level; }

void Logger::log(LogLevel level, const char* file, int line, const char* func, const char* fmt, ...) {
    if (level < level_) return;

    const char* levelStr[] = {"DEBUG", "INFO", "WARN", "ERROR", "FATAL"};

    time_t now = time(nullptr);
    struct tm* local = localtime(&now);
    char timeStr[32];
    strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", local);

    char msg[2048];
    va_list args;
    va_start(args, fmt);
    vsnprintf(msg, sizeof(msg), fmt, args);
    va_end(args);

    char buf[4096];
    snprintf(buf, sizeof(buf), "[%s] [%s] [%s:%d %s] %s\n",
             timeStr, levelStr[level], file, line, func, msg);

    std::lock_guard<std::mutex> lock(mutex_);
    if (console_) {
        if (level >= LOG_ERROR) {
            fprintf(stderr, "%s", buf);
        } else {
            printf("%s", buf);
        }
    }
    if (file_.is_open()) {
        file_ << buf;
        file_.flush();
    }
}
