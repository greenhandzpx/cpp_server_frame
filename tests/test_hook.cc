#include "sylar/sylar.h"

sylar::Logger::ptr g_logger = SYLAR_LOGGER_ROOT();


void test_hook()
{
    sylar::IOManager iom = sylar::IOManager(1, true, "hook!");
    iom.schedule([]() {
        SYLAR_LOG_INFO(g_logger) << "Sleep for 2s...";
        sleep(2);
        SYLAR_LOG_INFO(g_logger) << "Sleep after 2s!";
    });
    iom.schedule([]() {
        SYLAR_LOG_INFO(g_logger) << "Sleep for 4s...";
        sleep(4);
        SYLAR_LOG_INFO(g_logger) << "Sleep after 4s!";
    });

}

int main()
{
    //sylar::Set_S_Level()

    //sylar::LogLevel::Level level(sylar::LogLevel::INFO);
    sylar::Filter(sylar::LogLevel::INFO);
    test_hook();
}