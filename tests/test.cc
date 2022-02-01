#include <iostream>
#include "../sylar/log.h"  
#include "../sylar/util.h"

int main() 
{
    sylar::Logger::ptr logger(new sylar::Logger);
    logger->addAppender(sylar::LogAppender::ptr(new sylar::StdoutLogAppender));

    sylar::LogAppender::ptr file_appender(new sylar::FileLogAppender("sth.txt"));
    logger->addAppender(file_appender);

    sylar::LogFormatter::ptr formatter(new sylar::LogFormatter("%d%T%p%T%m%n"));
    file_appender->setFormatter(formatter);
    file_appender->setLevel(sylar::LogLevel::ERROR);

    // //以下方式得手动定义一个事件，然后再主动调用log，有点麻烦，可以采用宏的形式
    // sylar::LogEvent::ptr event(new sylar::LogEvent(logger, __FILE__, __LINE__, 0, sylar::GetThreadId(), sylar::GetFiberId(), time(nullptr)));
    // logger->log(sylar::LogLevel::DEBUG, event);

    SYLAR_LOG_FATAL(logger) << "Hello let me test.";

    SYLAR_LOG_FMT_ERROR(logger, "oh I just test %s", "this big guy");

    // 利用模板类创建一个logger,比较方便
    auto l = sylar::LoggerMgr::GetInstance()->getLogger("xx");
    SYLAR_LOG_INFO(l) << "try this template";

    std::cout << "This is just a test.\n";
    return 0;
}