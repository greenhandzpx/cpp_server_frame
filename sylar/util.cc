#include "util.h"
#include "log.h"
#include "fiber.h"

#include <execinfo.h>

namespace sylar {

pid_t GetThreadId()
{
    return (pid_t) syscall(SYS_gettid);
}

uint32_t GetFiberId()
{
    return Fiber::GetFiberId();
}

sylar::Logger::ptr g_logger = SYLAR_LOG_NAME("system");

void Backtrace(std::vector<std::string> &bt, int size, int skip)
{
    void **array = (void **) malloc(sizeof(void *) * size);
    size_t s = ::backtrace(array, size);

    // 把栈的调用信息输出到strings里
    char **strings = backtrace_symbols(array, s);
    if (strings == nullptr) {
        SYLAR_LOG_ERROR(g_logger) << "backtrace_symbols error!";
        free(strings);
        free(array);
        return;
    }

    for (size_t i = skip; i < s; ++i) {
        bt.emplace_back(strings[i]);
    }

    free(strings);
    free(array);
}
std::string Backtrace_to_string(int size, int skip, const std::string &prefix)
{
    std::vector<std::string> bt;
    Backtrace(bt, size, skip);
    std::stringstream ss;
    for (const auto &i : bt) {
        ss << prefix << i << std::endl;
    }
    return ss.str();
}

uint64_t Get_current_ms()
{
    struct timeval tv{};
    gettimeofday(&tv, nullptr);
    return tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

uint64_t Get_current_us()
{
    struct timeval tv{};
    gettimeofday(&tv, nullptr);
    return tv.tv_sec * 1000 * 1000 + tv.tv_usec;
}
}