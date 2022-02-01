#include "../sylar/sylar.h"

sylar::Logger::ptr g_logger = SYLAR_LOGGER_ROOT();

void run_in_fiber()
{
    SYLAR_LOG_INFO(g_logger) << "Child Fiber starts";
    // 将该子协程搁置到后台, 主协程回到当前线程
    sylar::Fiber::Yield_to_Hold();
    SYLAR_LOG_INFO(g_logger) << "Child Fiber comes again";
    sylar::Fiber::Yield_to_Hold();
}
void test_fiber()
{
    // 创建一个主协程
    sylar::Fiber::GetThis();
    {
        SYLAR_LOG_INFO(g_logger) << "Main Fiber starts";
        // 创建一个子协程并将其切换到当前线程
        sylar::Fiber::ptr one_fiber(new sylar::Fiber(run_in_fiber));
        one_fiber->swapIn();
        SYLAR_LOG_INFO(g_logger) << "Main Fiber comes again when child is held";
        one_fiber->swapIn();
        SYLAR_LOG_INFO(g_logger) << "Main Fiber comes again when child is held again";
        one_fiber->swapIn();
    }
    SYLAR_LOG_INFO(g_logger) << "Main Fiber comes again again when child is held again";
}
int main(int argc, char* argv[])
{
    sylar::Thread::SetName("MainThread");
    std::vector<sylar::Thread::ptr> thread_pool;
    for (int i = 0; i < 3; ++i) {
        thread_pool.emplace_back(std::make_shared<sylar::Thread>(&test_fiber, "thread_" + std::to_string(i)));
    }
    for (auto& i: thread_pool) {
        i->join();
    }
    return  0;
}