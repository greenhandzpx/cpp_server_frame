#include "../sylar/sylar.h"


sylar::Logger::ptr g_logger = SYLAR_LOGGER_ROOT();

sylar::RWMutex s_rw_mutex;
sylar::Mutex s_mutex;
int count;
void func1()
{
    SYLAR_LOG_INFO(g_logger) << "name: " << sylar::Thread::GetName()
                             << " this.name: " << sylar::Thread::GetThis()->getName()
                             << " id: " << sylar::GetThreadId()
                             << " this.id: " << sylar::Thread::GetThis()->getId();

//    for (int i = 0; i < 100000; ++i) {
//        sylar::Mutex::Lock mutex(s_mutex);
//        ++count;
//    }
}

int main()
{
    SYLAR_LOG_INFO(g_logger) << "thread test begins";
    std::vector<sylar::Thread::ptr> thrs;
    for (int i = 0; i < 6; ++i) {
        sylar::Thread::ptr thr(new sylar::Thread(&func1, "name_" + std::to_string(i)));
        thrs.push_back(thr);
    }
    for (auto& t: thrs) {
       t->join();
    }

    SYLAR_LOG_INFO(g_logger) << "Count: " << count;
    SYLAR_LOG_INFO(g_logger) << "thread test ends";

    return 0;
}