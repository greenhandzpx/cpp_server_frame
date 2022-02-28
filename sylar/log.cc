#include <iostream>
#include <map>
#include <functional>
#include <tuple>
#include <set>
#include <cstdarg>
#include <utility>
#include "log.h"
#include "config.h"

namespace sylar {

static LogLevel::Level &G_Level()
{
    static LogLevel::Level S_level = LogLevel::DEBUG;
    return S_level;
}

void Filter(LogLevel::Level level)
{
    G_Level() = level;
}

LogEvent::LogEvent(std::shared_ptr<Logger> logger,
                   LogLevel::Level level,
                   const char *file,
                   int32_t line,
                   uint32_t elapse,
                   uint32_t threadId,
                   std::string threadName,
                   uint32_t fiberId,
                   uint64_t time)
    : m_file(file), m_line(line), m_elapse(elapse), m_threadId(threadId), m_threadName(std::move(threadName)),
      m_fiberId(fiberId), m_time(time), m_logger(std::move(logger)), m_level(level)
//, m_ss(ss)
{
    //std::cout << m_time << std::endl;
}

// 传入可变参数（类似于printf）
void LogEvent::format(const char *fmt, ...)
{
    va_list al;
    va_start(al, fmt); // 逐个获取可变参数
    format(fmt, al);
    va_end(al);
}

void LogEvent::format(const char *fmt, va_list al)
{
    char *buf = nullptr;
    // 将al格式化输入到buf缓冲区
    int len = vasprintf(&buf, fmt, al);
    if (len != -1) {
        // 再将buf输入到m_ss
        m_ss << std::string(buf, len);
        free(buf);
    }
}

LogEventWrap::LogEventWrap(LogEvent::ptr e)
    : m_event(e) {}

LogEventWrap::~LogEventWrap()
{
    m_event->getLogger()->log(m_event->getLevel(), m_event);
}

std::stringstream &LogEventWrap::getSS()
{
    return m_event->getSS();
}

class MessageFormatItem : public LogFormatter::FormatItem {
public:
    explicit MessageFormatItem(const std::string &str = "") {}
    void format(std::ostream &os, std::shared_ptr<Logger> logger, LogLevel::Level level, LogEvent::ptr event) override
    {
        os << event->getContent();
        //os << m_message;
    }
};

class NormalStringFormatItem : public LogFormatter::FormatItem {
public:
    explicit NormalStringFormatItem(const std::string &str = "")
        : m_string(str) {}
    void format(std::ostream &os, std::shared_ptr<Logger> logger, LogLevel::Level level, LogEvent::ptr event) override
    {
        //os << event->getContent();
        os << m_string;
    }
private:
    std::string m_string;
};

class LevelFormatItem : public LogFormatter::FormatItem {
public:
    explicit LevelFormatItem(const std::string &str = "") {}
    void format(std::ostream &os, std::shared_ptr<Logger> logger, LogLevel::Level level, LogEvent::ptr event) override
    {
        os << LogLevel::toString(level);
    }
};

class FilenameFormatItem : public LogFormatter::FormatItem {
public:
    explicit FilenameFormatItem(const std::string &str = "") {}
    void format(std::ostream &os, std::shared_ptr<Logger> logger, LogLevel::Level level, LogEvent::ptr event) override
    {
        os << event->getFile();
    }
};

// 日志名称
class NameFormatItem : public LogFormatter::FormatItem {
public:
    explicit NameFormatItem(const std::string &str = "") {}
    void format(std::ostream &os, std::shared_ptr<Logger> logger, LogLevel::Level level, LogEvent::ptr event) override
    {
        os << event->getLogger()->getName();
    }
};

// 回车换行
class NewLineFormatItem : public LogFormatter::FormatItem {
public:
    explicit NewLineFormatItem(const std::string &str = "") {}
    void format(std::ostream &os, std::shared_ptr<Logger> logger, LogLevel::Level level, LogEvent::ptr event) override
    {
        os << std::endl;
    }
};

// 该条日志的行号
class LineFormatItem : public LogFormatter::FormatItem {
public:
    explicit LineFormatItem(const std::string &str = "") {}
    void format(std::ostream &os, std::shared_ptr<Logger> logger, LogLevel::Level level, LogEvent::ptr event) override
    {
        os << event->getLine();
    }
};

class ElapseFormatItem : public LogFormatter::FormatItem {
public:
    explicit ElapseFormatItem(const std::string &str = "") {}
    void format(std::ostream &os, std::shared_ptr<Logger> logger, LogLevel::Level level, LogEvent::ptr event) override
    {
        os << event->getElapse();
    }
};

class ThreadIdFormatItem : public LogFormatter::FormatItem {
public:
    explicit ThreadIdFormatItem(const std::string &str = "") {}
    void format(std::ostream &os, std::shared_ptr<Logger> logger, LogLevel::Level level, LogEvent::ptr event) override
    {
        os << event->getThreadId();
    }
};

class ThreadNameFormatItem : public LogFormatter::FormatItem {
public:
    explicit ThreadNameFormatItem(const std::string &str = "") {}
    void format(std::ostream &os, std::shared_ptr<Logger> logger, LogLevel::Level level, LogEvent::ptr event) override
    {
        os << event->getThreadName();
    }
};

class FiberIdFormatItem : public LogFormatter::FormatItem {
public:
    explicit FiberIdFormatItem(const std::string &str = "") {}
    void format(std::ostream &os, std::shared_ptr<Logger> logger, LogLevel::Level level, LogEvent::ptr event) override
    {
        os << event->getFiberId();
    }
};

class TabFormatItem : public LogFormatter::FormatItem {
public:
    explicit TabFormatItem(const std::string &str = "") {}
    void format(std::ostream &os, std::shared_ptr<Logger> logger, LogLevel::Level level, LogEvent::ptr event) override
    {
        os << "\t";
    }
};

class DateTimeFormatItem : public LogFormatter::FormatItem {
public:
    explicit DateTimeFormatItem(const std::string &format = "%Y-%m-%d %H:%M:%S")
        : m_format(format)
    {
        if (m_format.empty()) {
            m_format = "%Y-%m-%d %H:%M:%S";
        }
    }
    void format(std::ostream &os, std::shared_ptr<Logger> logger, LogLevel::Level level, LogEvent::ptr event) override
    {
        struct tm tm{};
        time_t time = event->getTime();
        //tm = localtime(&time);
        localtime_r(&time, &tm);
        char buf[64];
        //std::cout << m_format << std::endl;
        strftime(buf, sizeof(buf), m_format.c_str(), &tm);
        os << buf;
        //std::cout << "hhhh\n";
    }
private:
    std::string m_format;
};

const char *LogLevel::toString(LogLevel::Level level)
{
    switch (level) {
        // 宏定义，直接替换类成员名称
#define XX(name) \
        case LogLevel::name: \
            return #name; \
            break;

    XX(DEBUG);
    XX(INFO);
    XX(WARN);
    XX(ERROR);
    XX(FATAL);

#undef XX
    default:return "UNKNOWN";
    }
    return "UNKNOWN";
}

LogLevel::Level LogLevel::fromString(const std::string &str)
{

#define XX(level, name) \
        if (str == #name) { \
            return LogLevel::level; \
        }
    XX(DEBUG, debug);
    XX(INFO, info);
    XX(WARN, warn);
    XX(ERROR, error);
    XX(FATAL, fatal);

    XX(DEBUG, DEBUG);
    XX(INFO, INFO);
    XX(WARN, WARN);
    XX(ERROR, ERROR);
    XX(FATAL, FATAL);
    return LogLevel::UNKNOWN;
#undef XX
}

Logger::Logger(std::string name)
    : m_name(std::move(name))
//, m_level(LogLevel::DEBUG)
{
    // 每次构建一个logger都会默认初始化一个formatter
    m_formatter.reset(new LogFormatter("%d%T%t%T%N%T%F%T[%p]%T[%c]%T<%f:%l>%T%m%n"));
}

// %m -- 消息体
// %p -- level
// %r -- 启动后的时间
// %t -- 线程id
// %F -- 协程id
// %n -- 回车换行
// %d -- 时间
// %f -- 文件名
// %l -- 行号
// %T -- 缩进
void Logger::addAppender(LogAppender::ptr appender)
{
    Mutex::Lock lock(m_mutex);
    if (!appender->getFormatter()) {
        // 将logger自带的formatter给appender
        // 保证添加的每一个appender都有一个formatter
        appender->setFormatter(m_formatter);
    }
    m_appenders.push_back(appender);
}

void Logger::delAppender(LogAppender::ptr appender)
{
    Mutex::Lock lock(m_mutex);
    for (auto i = m_appenders.begin(); i != m_appenders.end(); ++i) {
        if (*i == appender) {
            m_appenders.erase(i);
            break;
        }
    }
}

void Logger::clearAppenders()
{
    Mutex::Lock lock(m_mutex);
    m_appenders.clear();
}

void Logger::setFormatter(LogFormatter::ptr val)
{
    Mutex::Lock lock(m_mutex);
    m_formatter = std::move(val);

    // TODO
    // 更新logger拥有的appender的formatter
    //    for (auto& a: m_appenders) {
    //        ;
    //
    //    }
}
void Logger::setFormatter(const std::string &val)
{
    LogFormatter::ptr new_val(new LogFormatter(val));
    // 构造formatter的时候会调用init,从而可能修改isError
    if (new_val->isError()) {
        std::cout << "Logger name=" << m_name << " setFormatter"
                  << " value=" << val << " invalid formatter"
                  << std::endl;
        return;
    }
    setFormatter(new_val);
}
LogFormatter::ptr Logger::getFormatter()
{
    return m_formatter;
}
std::string Logger::toYamlString()
{
    Mutex::Lock lock(m_mutex);
    YAML::Node node;
    node["name"] = m_name;
    node["level"] = LogLevel::toString(m_level);
    if (m_formatter) {
        node["formatter"] = m_formatter->getPattern();
    }
    for (auto &a : m_appenders) {
        node["appenders"].push_back(YAML::Load(a->toYamlString()));
    }
    std::stringstream ss;
    ss << node;
    return ss.str();
}

void Logger::log(LogLevel::Level level, const LogEvent::ptr &event)
{
    if (level >= m_level) {
        auto self = shared_from_this();
        Mutex::Lock lock(m_mutex);
        if (!m_appenders.empty()) { // 如果存在appender
            for (auto &i : m_appenders) {
                //std::cout << event->getTime() << std::endl;
                i->log(self, level, event);
            }
        } else if (m_root) { // 否则如果存在m_root6
            m_root->log(level, event);
        }
    }
}

void Logger::debug(LogEvent::ptr event)
{
    log(LogLevel::DEBUG, std::move(event));
}

void Logger::info(LogEvent::ptr event)
{
    log(LogLevel::INFO, std::move(event));
}

void Logger::warn(LogEvent::ptr event)
{
    log(LogLevel::WARN, std::move(event));
}

void Logger::error(LogEvent::ptr event)
{
    log(LogLevel::ERROR, std::move(event));
}

void Logger::fatal(LogEvent::ptr event)
{
    log(LogLevel::FATAL, std::move(event));
}

void StdoutLogAppender::log(std::shared_ptr<Logger> logger, LogLevel::Level level, LogEvent::ptr event)
{
    if (level >= m_level && level >= G_Level()) {
        Mutex::Lock lock(m_mutex);
        //std::cout << event->getTime() << std::endl;
        m_formatter->format(std::cout, logger, level, event);
    }
}

std::string StdoutLogAppender::toYamlString()
{
    Mutex::Lock lock(m_mutex);
    YAML::Node node;
    node["type"] = "StdoutLogAppender";
    node["level"] = LogLevel::toString(m_level);
    if (m_formatter) {
        node["formatter"] = m_formatter->getPattern();
    }
    std::stringstream ss;
    ss << node;
    return ss.str();
}

FileLogAppender::FileLogAppender(const std::string &filename)
    : m_filename(filename) {}

std::string FileLogAppender::toYamlString()
{
    Mutex::Lock lock(m_mutex);
    YAML::Node node;
    node["type"] = "StdoutLogAppender";
    node["file"] = m_filename;
    node["level"] = LogLevel::toString(m_level);
    if (m_formatter) {
        node["formatter"] = m_formatter->getPattern();
    }
    std::stringstream ss;
    ss << node;
    return ss.str();
}
void FileLogAppender::reopen()
{
    Mutex::Lock lock(m_mutex);
    if (m_filestream) {
        // 打开了的话就先关闭掉
        m_filestream.close();
    }
    m_filestream.open(m_filename);
    //printf("I can open\n");
}

void FileLogAppender::log(std::shared_ptr<Logger> logger, LogLevel::Level level, LogEvent::ptr event)
{
    if (level >= m_level && level >= G_Level()) {
        // 每次log一条日志就reopen一次，防止文件中途被删掉
        reopen();
        Mutex::Lock lock(m_mutex);
        m_formatter->format(m_filestream, logger, level, event);
        //m_filestream.close();
    }
}

LogFormatter::LogFormatter(const std::string &pattern)
    : m_pattern(pattern)
{
    init();
}

std::string LogFormatter::format(std::shared_ptr<Logger> logger, LogLevel::Level level, LogEvent::ptr event)
{
    std::basic_stringstream<char> ss;
    //std:stringstream ss;
    for (auto &i : m_items) {

        i->format(ss, logger, level, event);
    }
    return ss.str();
}

// 重载
std::ostream &LogFormatter::format(std::ostream &ofs,
                                   std::shared_ptr<Logger> logger,
                                   LogLevel::Level level,
                                   LogEvent::ptr event)
{
    for (auto &i : m_items) {
        //std::cout << event->getTime() << std::endl;
        i->format(ofs, logger, level, event);
    }
    return ofs;
}

// 共有三种情况：
// 1) %xxx  2) %xxx{xxx}  3) %%
void LogFormatter::init()
{
    // 以一个三元组的形式存储信息
    std::vector<std::tuple<std::string, std::string, int>> vec;
    std::string nstr;
    for (size_t i = 0; i < m_pattern.size(); ++i) {
        if (m_pattern[i] != '%') {
            nstr.append(1, m_pattern[i]);
            continue;
        }
        if (i + 1 < m_pattern.size()) {
            if (m_pattern[i + 1] == '%') {
                nstr.append(1, '%');
                continue;
            }
        }
        size_t n = i + 1;
        int fmt_status = 0;
        size_t fmt_begin = 0;

        std::string str; // 存储字符串
        std::string fmt; // 存储格式
        while (n < m_pattern.size()) {
            if (!isalpha(m_pattern[n]) && m_pattern[n] != '{'
                && m_pattern[n] != '}') {
                break;
            }
            if (fmt_status == 0) {
                if (m_pattern[n] == '{') {
                    str = m_pattern.substr(i + 1, n - i - 1); // 字符串结束
                    fmt_status = 1; // 开始解析格式
                    fmt_begin = n;
                    ++n;
                    continue;
                }
            }
            if (fmt_status == 1) {
                if (m_pattern[n] == '}') {
                    fmt = m_pattern.substr(fmt_begin + 1, n - fmt_begin - 1); // 格式结束
                    fmt_status = 2;
                    break;
                }
            }
            ++n;
        }

        if (fmt_status == 0) {
            // 说明是第一种情况，不含有fmt
            if (!nstr.empty()) {
                vec.emplace_back(nstr, "", 0);
                nstr.clear();
            }
            str = m_pattern.substr(i + 1, n - i - 1);
            vec.emplace_back(str, fmt, 1);
            i = n - 1;

        } else if (fmt_status == 1) {
            // 这种是错误的情况
            std::cout << "pattern parse error: " << m_pattern << " - " << m_pattern.substr(i) << "\n";
            vec.emplace_back("<<pattern_error>>", fmt, 0);
            m_isError = true;

        } else if (fmt_status == 2) {
            // 第二种情况
            if (!nstr.empty()) {
                vec.emplace_back(nstr, "", 0);
                nstr.clear();
            }
            vec.emplace_back(str, fmt, 1);
            i = n;
        }
    }
    if (!nstr.empty()) {
        vec.emplace_back(nstr, "", 0);
        nstr.clear();
    }
    static std::map<std::string, std::function<FormatItem::ptr(const std::string &)>> s_format_items = {
#define XX(str, C) \
        {#str, [](const std::string& fmt) { return FormatItem::ptr(new C(fmt)); }}

        XX(m, MessageFormatItem),
        XX(p, LevelFormatItem),
        XX(r, ElapseFormatItem),
        XX(c, NameFormatItem),
        XX(t, ThreadIdFormatItem),
        XX(n, NewLineFormatItem),
        XX(F, FiberIdFormatItem),
        XX(T, TabFormatItem),
        XX(d, DateTimeFormatItem),
        XX(f, FilenameFormatItem),
        XX(l, LineFormatItem),
        XX(N, ThreadNameFormatItem),

#undef XX
    };
    // %m -- 消息体
    // %p -- level
    // %r -- 启动后的时间
    // %t -- 线程id
    // %F -- 协程id
    // %n -- 回车换行
    // %d -- 时间
    // %f -- 文件名
    // %l -- 行号
    for (auto &i : vec) {
        if (std::get<2>(i) == 0) {
            // 说明是正常文本
            m_items.push_back(FormatItem::ptr(new NormalStringFormatItem(std::get<0>(i))));
        } else {
            auto it = s_format_items.find(std::get<0>(i)); // 查找对应的fmt是否存在
            if (it == s_format_items.end()) {
                m_items.push_back(FormatItem::ptr(new MessageFormatItem("<<error_format %" + std::get<0>(i) + ">>")));
                m_isError = true;
            } else {
                m_items.push_back(it->second(std::get<1>(i)));
            }
        }

        //std::cout << std::get<0>(i) << " - " << std::get<1>(i) << " - " << std::get<2>(i) << std::endl;
    }
}

LoggerManager::LoggerManager()
{
    m_root.reset(new Logger);
    m_root->addAppender(LogAppender::ptr(new StdoutLogAppender));
    m_loggers[m_root->getName()] = m_root;
}

Logger::ptr LoggerManager::getLogger(const std::string &loggerName)
{
    Mutex::Lock lock(m_mutex);
    auto it = m_loggers.find(loggerName);
    if (it != m_loggers.end()) {
        return it->second;
    }
    Logger::ptr logger(new Logger(loggerName));
    logger->m_root = m_root;
    m_loggers[loggerName] = logger;
    return logger;

}

bool LoggerManager::addLogger(const std::string &name, Logger::ptr logger)
{
    Mutex::Lock lock(m_mutex);
    m_loggers[name] = std::move(logger);
    return true;
}

Logger::ptr LoggerManager::getRoot()
{
    return m_root;
}

struct LogAppenderDefine {
    int type = 0; // 1--File, 2--Stdout
    LogLevel::Level level = LogLevel::UNKNOWN;
    std::string formatter;
    std::string file;

    bool operator==(const LogAppenderDefine &oth) const
    {
        return type == oth.type
            && level == oth.level
            && formatter == oth.formatter
            && file == oth.file;
    }
};
// 添加logger时需要提供的定义信息
struct LogDefine {
    std::string name;
    LogLevel::Level level = LogLevel::UNKNOWN;
    std::string formatter;
    std::vector<LogAppenderDefine> appenders;

    bool operator==(const LogDefine &oth) const
    {
        return name == oth.name
            && level == oth.level
            && formatter == oth.formatter
            && appenders == oth.appenders;
    }
    // 定义“<"是因为后面要用到set来存LogDefine, set按照<来排列元素
    bool operator<(const LogDefine &oth) const
    {
        return name < oth.name;
    }
};

template<>
class LexicalCast<std::string, std::set<LogDefine>> {
public:
    std::set<LogDefine> operator()(const std::string &v)
    {
        YAML::Node node = YAML::Load(v);
        std::set<LogDefine> vec;
        for (auto n : node) {
            if (!n["name"].IsDefined()) {
                std::cout << "log config error: name is NULL, " << n << std::endl;
                continue;
            }
            LogDefine ld;
            ld.name = n["name"].as<std::string>();
            ld.level = LogLevel::fromString(n["level"].IsDefined() ? n["level"].as<std::string>() : "");
            if (n["formatter"].IsDefined()) {
                ld.formatter = n["formatter"].as<std::string>();
            }
            if (n["appenders"].IsDefined()) {
                for (size_t x = 0; x < n["appenders"].size(); ++x) {
                    auto a = n["appenders"][x];
                    if (!a["type"].IsDefined()) {
                        std::cout << "log config error: appender type is NULL, " << a << std::endl;
                        continue;
                    }
                    std::string type = a["type"].as<std::string>();
                    LogAppenderDefine lad;
                    if (type == "FileLogAppender") {
                        lad.type = 1;
                        if (!a["file"].IsDefined()) {
                            std::cout << "log config error: fileLogAppender file is NULL, " << std::endl;
                            continue;
                        }
                        lad.file = a["file"].as<std::string>();
                        if (a["formatter"].IsDefined()) {
                            lad.formatter = a["formatter"].as<std::string>();
                        }
                    } else if (type == "StdoutLogAppender") {
                        lad.type = 2;
                    } else {
                        std::cout << "log config error: appender type is invalid " << std::endl;
                        continue;
                    }
                    ld.appenders.push_back(lad);
                }
            }
            vec.insert(ld);
        }
        return vec;
    }
};

template<>
class LexicalCast<std::set<LogDefine>, std::string> {
public:
    std::string operator()(const std::set<LogDefine> &v)
    {
        YAML::Node node;
        for (auto &i : v) {
            YAML::Node n;
            n["name"] = i.name;
            n["level"] = LogLevel::toString(i.level);
            if (!i.formatter.empty()) {
                n["formatter"] = i.formatter;
            }
            for (auto &a : i.appenders) {
                YAML::Node na;
                if (a.type == 1) {
                    na["type"] = "FileLogAppender";
                    na["file"] = a.file;
                } else if (a.type == 2) {
                    na["type"] = "StdoutLogAppender";
                }
                if (!a.formatter.empty()) {
                    na["formatter"] = a.formatter;
                }
                na["level"] = LogLevel::toString(a.level);
                n["appenders"].push_back(na);
            }
            node.push_back(n);
        }
        std::stringstream ss;
        ss << node;
        return ss.str();
    }
};

sylar::ConfigVar<std::set<LogDefine>>::ptr g_log_defines =
    sylar::Config::Lookup("logs", std::set<LogDefine>(), "logs config");

struct LogIniter {
    LogIniter()
    {
        g_log_defines->addListener([](const std::set<LogDefine> &old_value,
                                      const std::set<LogDefine> &new_value) {
            SYLAR_LOG_INFO(SYLAR_LOGGER_ROOT()) << "one_logger_conf_changed";

            for (auto &i : new_value) {
                Logger::ptr logger;
                auto it = old_value.find(i);
                if (it == old_value.end()) {
                    // 新增logger
                    //std::cout << "Helllllo" + i.name << "\n";
                    logger.reset(new sylar::Logger(i.name));
                } else {
                    if (!(i == *it)) {
                        // 修改原有的logger
                        logger = SYLAR_LOG_NAME(i.name);
                    }
                }
                logger->setLevel(i.level);
                //std::cout << "wwwww\n";
                if (!i.formatter.empty()) {
                    //std::cout << "hhhhhh\n";
                    logger->setFormatter(i.formatter);
                }

                for (auto &a : i.appenders) {
                    LogAppender::ptr ap;
                    if (a.type == 1) {
                        ap.reset(new FileLogAppender(a.file));
                    } else if (a.type == 2) {
                        ap.reset(new StdoutLogAppender);
                    }
                    ap->setLevel(a.level);
                    logger->addAppender(ap);
                }
                LoggerMgr::GetInstance()->addLogger(i.name, logger);

            }
            for (auto &i : old_value) {
                // find调用的operator<，判定条件是名字，跟operator==不一样
                auto it = new_value.find(i);
                if (it == new_value.end()) {
                    // 删除logger
                    auto logger = SYLAR_LOG_NAME(i.name);
                    logger->setLevel((LogLevel::Level) 100); // 将该logger的日志级别设为特别高，从而不能接受任何日志
                    logger->clearAppenders(); // 当日志器的appenders为空时，调用该日志器会转为调用其root日志器成员
                }
            }
        });

    }
};

static LogIniter __log_init;

std::string LoggerManager::toYamlString()
{
    Mutex::Lock lock(m_mutex);
    YAML::Node node;
    for (auto &l : m_loggers) {
        node.push_back(YAML::Load(l.second->toYamlString()));
    }
    std::stringstream ss;
    ss << node;
    return ss.str();
}

void LoggerManager::init()
{

}

}