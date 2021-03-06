#ifndef __SYLAR_LOG_H__
#define __SYLAR_LOG_H__

#include <string>
#include <memory>
#include <list>
#include <fstream>
#include <utility>
#include <vector>
#include <sstream>
#include <unordered_map>
#include "singleton.h"
#include "util.h"
#include "thread.h"

// 普通输入信息
#define SYLAR_LOG_LEVEL(logger, level) \
    if (logger->getLevel() <= level) \
        sylar::LogEventWrap(sylar::LogEvent::ptr(new sylar::LogEvent(logger, level, __FILE__, __LINE__, 0, sylar::GetThreadId(), \
                            sylar::Thread::GetName(), sylar::GetFiberId(), time(nullptr)))).getSS()

#define SYLAR_LOG_DEBUG(logger) SYLAR_LOG_LEVEL(logger, sylar::LogLevel::DEBUG)
#define SYLAR_LOG_INFO(logger) SYLAR_LOG_LEVEL(logger, sylar::LogLevel::INFO)
#define SYLAR_LOG_WARN(logger) SYLAR_LOG_LEVEL(logger, sylar::LogLevel::WARN)
#define SYLAR_LOG_ERROR(logger) SYLAR_LOG_LEVEL(logger, sylar::LogLevel::ERROR)
#define SYLAR_LOG_FATAL(logger) SYLAR_LOG_LEVEL(logger, sylar::LogLevel::FATAL)


// 格式化输入信息
#define SYLAR_LOG_FMT_LEVEL(logger, level, fmt, ...) \
    if (logger->getLevel() <= level) \
        sylar::LogEventWrap(sylar::LogEvent::ptr(new sylar::LogEvent(logger, level, \
                                                __FILE__, __LINE__, 0, sylar::GetThreadId(), \
                                                sylar::Thread::GetName(), sylar::GetFiberId(), time(nullptr)))).getEvent()->format(fmt, __VA_ARGS__)

#define SYLAR_LOG_FMT_DEBUG(logger, fmt, ...) SYLAR_LOG_FMT_LEVEL(logger, sylar::LogLevel::DEBUG, fmt, __VA_ARGS__)
#define SYLAR_LOG_FMT_INFO(logger, fmt, ...) SYLAR_LOG_FMT_LEVEL(logger, sylar::LogLevel::INFO, fmt, __VA_ARGS__)
#define SYLAR_LOG_FMT_WARN(logger, fmt, ...) SYLAR_LOG_FMT_LEVEL(logger, sylar::LogLevel::WARN, fmt, __VA_ARGS__)
#define SYLAR_LOG_FMT_ERROR(logger, fmt, ...) SYLAR_LOG_FMT_LEVEL(logger, sylar::LogLevel::ERROR, fmt, __VA_ARGS__)
#define SYLAR_LOG_FMT_FATAL(logger, fmt, ...) SYLAR_LOG_FMT_LEVEL(logger, sylar::LogLevel::FATAL, fmt, __VA_ARGS__)

// 获取logger管理器的root logger
#define SYLAR_LOGGER_ROOT() \
    sylar::LoggerMgr::GetInstance()->getRoot()

// 按logger的名字查找获取对应的logger
#define SYLAR_LOG_NAME(name) \
    sylar::LoggerMgr::GetInstance()->getLogger(name)


namespace sylar {

// 先声明，以便其他类使用
class Logger;

// 日志级别
class LogLevel {
public:
    enum Level {
        UNKNOWN = 0,
        DEBUG = 1,
        INFO = 2,
        WARN = 3,
        ERROR = 4,
        FATAL = 5
    };

    static const char* toString(LogLevel::Level level);
    static LogLevel::Level fromString(const std::string&) ;
};

// 日志事件
class LogEvent {
public:
    typedef std::shared_ptr<LogEvent> ptr;
    LogEvent(std::shared_ptr<Logger> logger, LogLevel::Level level, const char* file,
             int32_t line,uint32_t elapse, uint32_t threadId, std::string  threadName
            , uint32_t fiberId, uint64_t time);

    const char* getFile() const { return m_file; } // 这里返回的是指针，故前面得加const,以下函数返回的都是拷贝，故不用加const
    int32_t getLine() const { return m_line; }
    uint32_t getElapse() const { return m_elapse; }
    uint32_t getThreadId() const { return m_threadId; }
    const std::string& getThreadName() const { return m_threadName; }
    uint32_t getFiberId() const { return m_fiberId; }
    uint64_t getTime() const { return m_time; }
    std::string getContent() const { return m_ss.str(); } 
    std::stringstream& getSS()  { return m_ss; }
    std::shared_ptr<Logger> getLogger() const { return m_logger; }
    LogLevel::Level getLevel() const { return m_level; }

    void format(const char* fmt, ...);
    void format(const char* fmt, va_list al);

private:
    const char* m_file = nullptr; // 文件名
    int32_t m_line = 0; // 行号
    uint32_t m_elapse = 0; // 程序启动开始到现在的毫秒数
    uint32_t m_threadId = 0; // 线程号
    std::string m_threadName; // 线程名字
    uint32_t m_fiberId = 0; // 协程号
    uint64_t m_time = 0; // 时间戳
    std::stringstream m_ss;
    std::shared_ptr<Logger> m_logger;
    LogLevel::Level m_level;
};


// 使用RAII
class LogEventWrap {
public:
    explicit LogEventWrap(LogEvent::ptr e);
    ~LogEventWrap();

    LogEvent::ptr getEvent() const { return m_event; }

    std::stringstream& getSS();

private:
    LogEvent::ptr m_event;
};



// 日志格式器
class LogFormatter {
public:
    typedef std::shared_ptr<LogFormatter> ptr;
    explicit LogFormatter(const std::string& pattern);

    // 将日志消息进行格式化
    std::string format(std::shared_ptr<Logger> logger, LogLevel::Level level, LogEvent::ptr event);
    std::ostream& format(std::ostream& ofs, std::shared_ptr<Logger> logger, LogLevel::Level level, LogEvent::ptr event);
    void init();
    bool isError() const { return m_isError; }
    std::string getPattern() { return m_pattern; }

public:
    class FormatItem {
    public:
        typedef std::shared_ptr<FormatItem> ptr;
        virtual ~FormatItem() = default;
        virtual void format(std::ostream& os, std::shared_ptr<Logger> logger, LogLevel::Level level, LogEvent::ptr event) = 0;
    };
private:
    std::string m_pattern;
    std::vector<FormatItem::ptr> m_items;
    bool m_isError = false;
}; 


// 日志输出地
class LogAppender {
public:
    typedef std::shared_ptr<LogAppender> ptr;
    virtual ~LogAppender() = default;

    virtual void log(std::shared_ptr<Logger> logger, LogLevel::Level level, LogEvent::ptr event) = 0;

    virtual std::string toYamlString() = 0;

    void setFormatter(LogFormatter::ptr formatter)
    {
        Mutex::Lock lock(m_mutex);
        m_formatter = std::move(formatter);
    }
    LogFormatter::ptr getFormatter()
    {
        Mutex::Lock lock(m_mutex);
        return m_formatter;
    }

    void setLevel(LogLevel::Level level) { m_level = level; }
    LogLevel::Level getLevel() const { return m_level; }

protected:
    LogLevel::Level m_level = LogLevel::DEBUG;
    LogFormatter::ptr m_formatter;
    Mutex m_mutex;

};


class LoggerManager;

// 日志器
class Logger: public std::enable_shared_from_this<Logger> { // 以便于使用指向自身的智能指针
friend class LoggerManager;
public:
    typedef std::shared_ptr<Logger> ptr;

    explicit Logger(std::string  name = "root");
    void log( LogLevel::Level level, const LogEvent::ptr& event);

    void debug(LogEvent::ptr event);
    void info(LogEvent::ptr event);
    void warn(LogEvent::ptr event);
    void error(LogEvent::ptr event);
    void fatal(LogEvent::ptr event);

    void addAppender(LogAppender::ptr appender);
    void delAppender(LogAppender::ptr appender);
    void clearAppenders();
    
    LogLevel::Level getLevel() const { return m_level; }
    void setLevel(LogLevel::Level val) { m_level = val; }

    const std::string& getName() const { return m_name; }

    void setFormatter(LogFormatter::ptr val);
    void setFormatter(const std::string& val);
    LogFormatter::ptr getFormatter();

    std::string toYamlString();

private:
    std::string m_name;
    LogLevel::Level m_level = LogLevel::DEBUG;
    std::list<LogAppender::ptr> m_appenders;
    LogFormatter::ptr m_formatter;
    // 主日志器
    Logger::ptr m_root;
    Mutex m_mutex;
};


// 输出到控制台的appender
class StdoutLogAppender: public LogAppender {
public:
    typedef std::shared_ptr<StdoutLogAppender> ptr;
    void log(std::shared_ptr<Logger> logger, LogLevel::Level Level, LogEvent::ptr event) override;
    std::string toYamlString() override;
};

// 输出到文件的appender
class FileLogAppender: public LogAppender {
public:
    typedef std::shared_ptr<FileLogAppender> ptr;

    explicit FileLogAppender(const std::string& filename);
    void log(std::shared_ptr<Logger> logger, LogLevel::Level Level, LogEvent::ptr event) override;

    std::string toYamlString() override;

    void reopen();
private:
    std::string m_filename;
    std::ofstream m_filestream;
};


class LoggerManager {
public:
    LoggerManager();
    Logger::ptr getLogger(const std::string& loggerName);
    bool addLogger(const std::string& name, Logger::ptr logger);
    Logger::ptr getRoot();
    std::string toYamlString();
    void init();
private:
    std::unordered_map<std::string, Logger::ptr> m_loggers;
    Logger::ptr m_root;
    Mutex m_mutex;
};

typedef sylar::Singleton<LoggerManager> LoggerMgr;

// G_Level用来表示全局的日志等级
// Filter用来控制全局日志等级
static LogLevel::Level& G_Level();
void Filter(LogLevel::Level level);

}

#endif