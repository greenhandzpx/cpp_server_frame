//
// Created by greenhandzpx on 1/25/22.
//

#ifndef SYLAR_MACRO_H
#define SYLAR_MACRO_H


#include <cstring>
#include <cassert>
#include "util.h"

#define SYLAR_ASSERT(x) \
    if (!(x)) { \
        SYLAR_LOG_ERROR(SYLAR_LOGGER_ROOT()) << "ASSERTION: " #x \
            << "\nbacktrace:\n" \
            << sylar::Backtrace_to_string(100, 2, "   "); \
        assert(x); \
    }


#define SYLAR_ASSERT2(x, w) \
    if (!(x)) { \
        SYLAR_LOG_ERROR(SYLAR_LOGGER_ROOT()) << "ASSERTION: " #x \
            << "\n" << w \
            << "\nbacktrace:\n" \
            << sylar::Backtrace_to_string(100, 2, "   "); \
        assert(x); \
    }



#endif //SYLAR_MACRO_H
