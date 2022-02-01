#include "../sylar/sylar.h"
#include <unistd.h>

sylar::Logger::ptr g_logger = SYLAR_LOGGER_ROOT();


void test_fiber()
{
    static int count = 1;
    SYLAR_LOG_DEBUG(g_logger) << "Test whether fiber can be added.";

    //sleep(1);
    // schedule函数可以接受一个协程或者一个函数指针
//    if (count) {
//        sylar::Scheduler::GetThis()->schedule(&test_fiber);
//        --count;
//
//    }

}
int main()
{
    sylar::Scheduler sc(1, true, "scooo");
    //sylar::Fiber::ptr one_sub_fiber(new sylar::Fiber(&test_fiber));
    sc.schedule(&test_fiber);
    sc.start();

    sc.stop();
    return 0;
}