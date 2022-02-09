#ifndef __SYLAR_UTIL_H__
#define __SYLAR_UTIL_H__

#include <pthread.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <cstdio>
#include <cstdint>
#include <vector>
#include <string>
#include <sys/time.h>

namespace sylar {

// 获取当前线程id
pid_t GetThreadId();

// 获取当前协程id
uint32_t GetFiberId();

// 用来获取某个位置的函数调用堆栈信息
void Backtrace(std::vector<std::string>& bt, int size, int skip);
std::string Backtrace_to_string(int size, int skip, const std::string& prefix = "");//常引用可以默认初始化


// 获取当前的时间(ms)
uint64_t Get_current_ms();
// 获取当前时间(us)
uint64_t Get_current_us();
}

#endif