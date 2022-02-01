//
// Created by greenhandzpx on 1/25/22.
//

#include "../sylar/sylar.h"
#include "../sylar/macro.h"
#include <cassert>

sylar::Logger::ptr Get_G_Logger()
{
    static sylar::Logger::ptr g_logger = SYLAR_LOGGER_ROOT();
    return g_logger;
}
//sylar::Logger::ptr g_logger = SYLAR_LOGGER_ROOT();

void test_assert()
{
    SYLAR_ASSERT(1 == 2)
}

void func()
{
    test_assert();
}
int main()
{
    func();
    return 0;
}