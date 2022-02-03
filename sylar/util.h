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

namespace sylar {

pid_t GetThreadId();

uint32_t GetFiberId();

// 用来获取某个位置的函数调用堆栈信息
void Backtrace(std::vector<std::string>& bt, int size, int skip);
std::string Backtrace_to_string(int size, int skip, const std::string& prefix = "");//常引用可以默认初始化
}

#endif